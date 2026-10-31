#include "capture_controller.hpp"

#include "map_view.hpp"
#include "player_controller.hpp"
#include "video_view.hpp"

#include "gb/export/framing.hpp"
#include "gb/export/settings.hpp"
#include "gb/image/photo.hpp"
#include "gb/media/playback.hpp"
#include "gb/media/position.hpp"
#include "gb/render/panel_painter.hpp"
#include "gb/render/still.hpp"
#include "gb/telemetry/telemetry.hpp"
#include "gb/tiles/tile_fetcher.hpp"

#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

namespace ex = gb::exporting;

constexpr std::array<std::pair<int, int>, 4> kAspects{{{16, 9}, {4, 3}, {1, 1}, {9, 16}}};
constexpr int kFreeAspect = static_cast<int>(kAspects.size());
constexpr std::array<ex::PanelSide, 5> kPanelSides{ex::PanelSide::none, ex::PanelSide::left,
                                                   ex::PanelSide::right, ex::PanelSide::top,
                                                   ex::PanelSide::bottom};
// The smallest rectangle, in canvas heights.
constexpr double kMinimumCropHeight = 0.05;
// Above this many output pixels per source pixel, a capture is reported as softer than it looks.
constexpr double kUpscaleWarning = 1.25;
const QString kFolderSetting = QStringLiteral("capture/folder");
// How long a capture waits for the panel's map tiles, and how often it asks for them again (the
// map pane drops queued requests for tiles it no longer shows).
constexpr qint64 kTileWaitMs = 5000;
constexpr int kTileRetryMs = 500;

quint64 packed(const gb::geo::TileKey key) {
  return (static_cast<quint64>(key.zoom) << 56U) | (static_cast<quint64>(key.x) << 28U) |
         static_cast<quint64>(key.y);
}

ex::Rect toRect(const QRectF &rect) { return {rect.x(), rect.y(), rect.width(), rect.height()}; }
QRectF toQRect(const ex::Rect &rect) { return {rect.x, rect.y, rect.width, rect.height}; }

long double seconds(const std::int64_t ticks, const gb::media::Rational base) {
  return static_cast<long double>(ticks) * base.numerator / base.denominator;
}

} // namespace

struct CaptureController::Prepared {
  QString path;
  QImage frame;
  ex::FrameLayout layout;
  ex::PanelSide side{ex::PanelSide::none};
  gb::render::PanelContent content;
  gb::render::StillOptions options;
  QStringList warnings;
};

CaptureController::CaptureController(PlayerController &player, QString version, QObject *parent)
    : QObject(parent), player_(player), version_(std::move(version)) {
  folder_ = QSettings().value(kFolderSetting).toString();
  if (folder_.isEmpty() || !QFileInfo(folder_).isDir())
    folder_ = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  if (folder_.isEmpty() || !QFileInfo(folder_).isDir())
    folder_ = QDir::homePath();

  connect(&player_, &PlayerController::positionChanged, this, [this] {
    if (!active_)
      return;
    emit rangeChanged();
    updateGps();
    updateSettings();
    emit panelChanged();
  });
  connect(&player_, &PlayerController::mapPositionChanged, this, [this] {
    if (active_)
      emit panelChanged();
  });
  auto *fetcher = player_.tileFetcher();
  connect(fetcher, &gb::tiles::TileFetcher::tileReady, this,
          [this](const int zoom, const int x, const int y, const QByteArray &data) {
            onTileAnswered(zoom, x, y, &data);
          });
  connect(fetcher, &gb::tiles::TileFetcher::tileUnavailable, this,
          [this](const int zoom, const int x, const int y, const QString &) {
            onTileAnswered(zoom, x, y, nullptr);
          });
  tile_timer_.setInterval(kTileRetryMs);
  connect(&tile_timer_, &QTimer::timeout, this, [this] {
    if (!prepared_)
      return;
    if (tile_clock_.elapsed() >= kTileWaitMs || requestPanelTiles(*prepared_) == 0)
      finishCapture();
  });
  connect(&player_, &PlayerController::fileChanged, this, [this] {
    cancelCapture(QStringLiteral("Capture cancelled: another file was opened"));
    const auto recording = player_.recordingPaths();
    if (recording.isEmpty() || recording != range_recording_) {
      start_.reset();
      end_.reset();
    }
    range_recording_ = recording;
    emit rangeChanged();
    if (active_) {
      updateGps();
      updateSettings();
    }
  });
  connect(&player_, &PlayerController::telemetryChanged, this, [this] {
    if (active_) {
      updateGps();
      updateSettings();
    }
  });
}

CaptureController::~CaptureController() = default;

void CaptureController::setActive(const bool active) {
  if (active_ == active)
    return;
  active_ = active;
  emit activeChanged();
  if (active_) {
    emit rangeChanged();
    updateGps();
    updateSettings();
  }
}

QObject *CaptureController::view() const { return view_; }

void CaptureController::setView(QObject *view) {
  auto *typed = qobject_cast<VideoView *>(view);
  if (typed == view_)
    return;
  if (view_ != nullptr)
    disconnect(view_, nullptr, this, nullptr);
  view_ = typed;
  if (view_ != nullptr) {
    connect(view_, &VideoView::viewChanged, this, [this] {
      if (active_)
        updateSettings();
    });
  }
  emit viewObjectChanged();
  if (active_)
    updateSettings();
}

double CaptureController::canvasAspect() const {
  return square_ ? 1.0 : frozen_aspect_.value_or(16.0 / 9.0);
}

void CaptureController::setSquare(const bool square) {
  if (square_ == square)
    return;
  square_ = square;
  emit canvasChanged();
  refitCrop();
}

void CaptureController::freezeCanvas(const double available_aspect) {
  if (frozen_aspect_.has_value() || !std::isfinite(available_aspect) || available_aspect <= 0.0)
    return;
  frozen_aspect_ = std::clamp(available_aspect, 0.1, 10.0);
  emit canvasChanged();
  refitCrop();
}

QStringList CaptureController::aspectNames() const {
  QStringList names;
  for (const auto &[width, height] : kAspects)
    names << QStringLiteral("%1:%2").arg(width).arg(height);
  names << QStringLiteral("Free");
  return names;
}

void CaptureController::setAspectIndex(const int index) {
  if (index == aspect_index_ || index < 0 || index > kFreeAspect)
    return;
  // Keep the same class of resolution (720p, 1080p, ...) when it was a preset.
  const auto old_presets = widthPresets();
  const auto old_index = old_presets.indexOf(output_width_);
  aspect_index_ = index;
  const auto presets = widthPresets();
  if (old_index >= 0 && old_index < presets.size())
    output_width_ = presets[old_index].toInt();
  emit frameChanged();
  refitCrop();
}

void CaptureController::setFreeWidth(const int value) {
  if (value == free_width_ || value < 1 || value > 10000)
    return;
  free_width_ = value;
  emit frameChanged();
  refitCrop();
}

void CaptureController::setFreeHeight(const int value) {
  if (value == free_height_ || value < 1 || value > 10000)
    return;
  free_height_ = value;
  emit frameChanged();
  refitCrop();
}

namespace {
ex::AspectRatio aspectFor(const int index, const int free_width, const int free_height) {
  if (index >= 0 && index < kFreeAspect) {
    const auto &[width, height] = kAspects[static_cast<std::size_t>(index)];
    return {width, height};
  }
  // A free ratio outside 1:10..10:1 is clamped into it.
  const auto parsed =
      ex::parse_aspect(std::to_string(free_width) + ":" + std::to_string(free_height));
  if (parsed)
    return *parsed;
  return free_width > free_height ? ex::AspectRatio{10, 1} : ex::AspectRatio{1, 10};
}
} // namespace

QString CaptureController::aspectText() const {
  return QString::fromStdString(aspectFor(aspect_index_, free_width_, free_height_).text());
}

QString CaptureController::panelSide() const {
  return QString::fromStdString(
      ex::panel_side_name(kPanelSides[static_cast<std::size_t>(panel_side_)]));
}

void CaptureController::setPanelSide(const QString &side) {
  const auto parsed = ex::parse_panel_side(side.toStdString());
  if (!parsed)
    return;
  const auto index = static_cast<int>(std::find(kPanelSides.begin(), kPanelSides.end(), *parsed) -
                                      kPanelSides.begin());
  if (index == panel_side_)
    return;
  panel_side_ = index;
  emit frameChanged();
  refitCrop();
}

void CaptureController::setPanelSize(const double size) {
  const auto clamped = std::clamp(size, ex::kMinimumPanelSize, 0.6);
  if (!std::isfinite(size) || std::abs(clamped - panel_size_) < 1e-9)
    return;
  panel_size_ = clamped;
  emit frameChanged();
  refitCrop();
}

void CaptureController::setPanelMap(const bool enabled) {
  if (panel_map_ == enabled)
    return;
  panel_map_ = enabled;
  emit frameChanged();
  updateSettings();
  emit panelChanged();
}

void CaptureController::setPanelTelemetry(const bool enabled) {
  if (panel_telemetry_ == enabled)
    return;
  panel_telemetry_ = enabled;
  emit frameChanged();
  updateSettings();
  emit panelChanged();
}

QObject *CaptureController::mapView() const { return map_; }

void CaptureController::setMapView(QObject *map) {
  auto *typed = qobject_cast<MapView *>(map);
  if (typed == map_)
    return;
  if (map_ != nullptr)
    map_->disconnect(this);
  map_ = typed;
  if (map_ != nullptr) {
    // Zooming the map pane changes the panel's scale.
    connect(map_, &MapView::viewChanged, this, [this] {
      if (!active_)
        return;
      updateSettings();
      emit panelChanged();
    });
    connect(map_, &MapView::routeChanged, this, &CaptureController::panelChanged);
  }
  emit viewObjectChanged();
}

double CaptureController::currentPictureAspect() const {
  const auto aspect = aspectFor(aspect_index_, free_width_, free_height_);
  return ex::picture_aspect(aspect, kPanelSides[static_cast<std::size_t>(panel_side_)], panel_size_)
      .value_or(aspect.value());
}

double CaptureController::pictureAspect() const { return currentPictureAspect(); }

QVariantList CaptureController::widthPresets() const {
  QVariantList result;
  for (const auto width : ex::width_presets(aspectFor(aspect_index_, free_width_, free_height_)))
    result << width;
  return result;
}

QStringList CaptureController::widthPresetNames() const {
  QStringList names;
  const auto aspect = aspectFor(aspect_index_, free_width_, free_height_);
  for (const auto width : ex::width_presets(aspect)) {
    const auto layout = ex::frame_layout(aspect, width);
    names << QStringLiteral("%1 × %2").arg(layout.width).arg(layout.height);
  }
  return names;
}

void CaptureController::setOutputWidth(const int width) {
  const auto clamped = std::clamp(width, 16, 16384);
  if (clamped == output_width_)
    return;
  output_width_ = clamped;
  emit frameChanged();
  updateSettings();
}

QString CaptureController::outputSizeText() const {
  try {
    const auto layout =
        ex::frame_layout(aspectFor(aspect_index_, free_width_, free_height_), output_width_,
                         kPanelSides[static_cast<std::size_t>(panel_side_)], panel_size_);
    auto text = QStringLiteral("%1 × %2").arg(layout.width).arg(layout.height);
    if (layout.panel)
      text +=
          QStringLiteral(", picture %1 × %2").arg(layout.picture.width).arg(layout.picture.height);
    return text;
  } catch (const std::exception &error) {
    return QString::fromUtf8(error.what());
  }
}

void CaptureController::setPng(const bool png) {
  if (png_ == png)
    return;
  png_ = png;
  emit outputChanged();
}

void CaptureController::setJpegQuality(const int quality) {
  const auto clamped = std::clamp(quality, 1, 100);
  if (clamped == jpeg_quality_)
    return;
  jpeg_quality_ = clamped;
  emit outputChanged();
}

void CaptureController::setGpsExif(const bool enabled) {
  if (gps_exif_ == enabled)
    return;
  gps_exif_ = enabled;
  emit outputChanged();
}

void CaptureController::setCrop(const QRectF &crop) {
  crop_set_ = true;
  if (crop == crop_)
    return;
  crop_ = crop;
  emit cropChanged();
  updateSettings();
}

void CaptureController::refitCrop() {
  const auto canvas = canvasAspect();
  const auto picture = currentPictureAspect();
  const auto fitted = crop_set_ ? ex::fit_crop(canvas, picture, crop_.center().x(),
                                               crop_.center().y(), crop_.height())
                                : ex::fit_crop(canvas, picture, 0.5, 0.5, 1.0);
  crop_ = toQRect(fitted);
  emit cropChanged();
  updateSettings();
}

void CaptureController::resetCrop() {
  crop_set_ = false;
  refitCrop();
}

void CaptureController::moveCrop(const double dx, const double dy) {
  if (!std::isfinite(dx) || !std::isfinite(dy))
    return;
  setCrop(toQRect(ex::move_crop(toRect(crop_), dx, dy)));
}

void CaptureController::scaleCrop(const double factor) {
  if (!std::isfinite(factor) || factor <= 0.0)
    return;
  const auto centre = crop_.center();
  setCrop(
      toQRect(ex::scale_crop(toRect(crop_), factor, centre.x(), centre.y(), kMinimumCropHeight)));
}

void CaptureController::dragCorner(const int corner, const double x, const double y) {
  if (corner < 0 || corner > 3 || !std::isfinite(x) || !std::isfinite(y))
    return;
  // The opposite corner stays put.
  const auto right = corner == 1 || corner == 2;
  const auto bottom = corner == 2 || corner == 3;
  const auto anchor_x = right ? crop_.left() : crop_.right();
  const auto anchor_y = bottom ? crop_.top() : crop_.bottom();
  const auto ratio = currentPictureAspect() / canvasAspect(); // width per height, canvas units
  const auto room_x = right ? 1.0 - anchor_x : anchor_x;
  const auto room_y = bottom ? 1.0 - anchor_y : anchor_y;
  auto height = std::max(std::abs(y - anchor_y), std::abs(x - anchor_x) / ratio);
  height = std::clamp(height, kMinimumCropHeight,
                      std::max(kMinimumCropHeight, std::min(room_y, room_x / ratio)));
  const auto width = height * ratio;
  QRectF rect(right ? anchor_x : anchor_x - width, bottom ? anchor_y : anchor_y - height, width,
              height);
  setCrop(toQRect(ex::move_crop(toRect(rect), 0.0, 0.0)));
}

bool CaptureController::hasRange() const { return player_.hasMedia() && !player_.isPhoto(); }

QString CaptureController::positionText() const {
  return hasRange() ? player_.positionText() : QString();
}

void CaptureController::setStart() {
  if (!hasRange())
    return;
  start_ = player_.positionText();
  emit rangeChanged();
  updateSettings();
}

void CaptureController::setEnd() {
  if (!hasRange())
    return;
  end_ = player_.positionText();
  emit rangeChanged();
  updateSettings();
}

void CaptureController::clearRange() {
  start_.reset();
  end_.reset();
  emit rangeChanged();
  updateSettings();
}

void CaptureController::updateGps() {
  std::optional<gb::image::PhotoGps> fix;
  QString reason;
  if (!player_.hasMedia()) {
    reason = QStringLiteral("nothing is open");
  } else if (player_.isPhoto()) {
    fix = player_.photoGps();
    if (!fix)
      reason = QStringLiteral("the photo records no position");
  } else if (player_.metadataLoading()) {
    reason = QStringLiteral("GPS telemetry is still loading");
  } else if (const auto *telemetry = player_.telemetry();
             telemetry == nullptr || telemetry->gps.empty()) {
    reason = QStringLiteral("the recording has no GPS records");
  } else {
    const auto current = player_.playbackEngine().current();
    const auto base = telemetry->time_base;
    constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    if (!current.frames || base.numerator == 0U || base.denominator == 0U ||
        base.numerator > limit || base.denominator > limit) {
      reason = QStringLiteral("no frame is displayed");
    } else {
      const gb::media::Rational telemetry_base{static_cast<std::int32_t>(base.numerator),
                                               static_cast<std::int32_t>(base.denominator)};
      const auto ticks =
          gb::media::rescale_ticks(current.frames->pts, current.frames->time_base, telemetry_base);
      const auto *point = gb::telemetry::nearest_point(*telemetry, ticks);
      const auto index = static_cast<std::size_t>(point - telemetry->gps.data());
      // One record interval: the spacing to the nearest neighbouring record.
      std::int64_t interval = std::numeric_limits<std::int64_t>::max();
      if (index > 0U)
        interval = point->media_time_ticks - telemetry->gps[index - 1U].media_time_ticks;
      if (index + 1U < telemetry->gps.size()) {
        interval = std::min(interval,
                            telemetry->gps[index + 1U].media_time_ticks - point->media_time_ticks);
      }
      const auto distance = std::llabs(point->media_time_ticks - ticks);
      if (point->validity != gb::telemetry::Validity::valid) {
        reason = QStringLiteral("no valid GPS fix at this frame");
      } else if (telemetry->gps.size() > 1U && distance > std::max<std::int64_t>(interval, 1)) {
        reason =
            QStringLiteral("no GPS record within one record interval of this frame (%1 ms away)")
                .arg(static_cast<double>(seconds(distance, telemetry_base) * 1000.0L), 0, 'f', 0);
      } else {
        gb::image::PhotoGps gps;
        gps.latitude = static_cast<double>(point->latitude);
        gps.longitude = static_cast<double>(point->longitude);
        gps.altitude = static_cast<double>(point->altitude);
        gps.speed_mps = static_cast<double>(point->speed_2d);
        gps.utc = point->utc;
        fix = gps;
      }
    }
  }
  const auto available = fix.has_value();
  gps_fix_ = fix ? std::make_shared<const gb::image::PhotoGps>(*fix) : nullptr;
  if (available != gps_available_ || reason != gps_reason_) {
    gps_available_ = available;
    gps_reason_ = reason;
    emit gpsChanged();
  }
}

void CaptureController::updateSettings() {
  if (!active_)
    return;
  ex::CaptureSettings settings;
  settings.generator = "gb-player " + version_.toStdString();
  settings.source = player_.inputName().toStdString();
  settings.has_range = hasRange();
  if (start_)
    settings.start = start_->toStdString();
  if (end_)
    settings.end = end_->toStdString();
  settings.current = player_.positionText().toStdString();
  if (view_ != nullptr) {
    settings.mode = view_->mode();
    settings.yaw = view_->yaw();
    settings.pitch = view_->pitch();
    settings.roll = view_->roll();
    settings.field_of_view = view_->fieldOfView();
    settings.zoom = view_->zoom();
    settings.pan_x = view_->panX();
    settings.pan_y = view_->panY();
  }
  settings.canvas = canvasAspect();
  settings.aspect = aspectFor(aspect_index_, free_width_, free_height_);
  settings.crop = toRect(crop_);
  settings.width = output_width_;
  settings.panel = kPanelSides[static_cast<std::size_t>(panel_side_)];
  settings.panel_size = panel_size_;
  if (settings.panel != ex::PanelSide::none) {
    settings.telemetry = panel_telemetry_;
    try {
      const auto layout =
          ex::frame_layout(settings.aspect, output_width_, settings.panel, panel_size_);
      // Without a GPS track the map has nothing to show.
      const auto content = panelContent();
      if (content.scene.route != nullptr && !content.scene.route->empty()) {
        settings.map_width_metres =
            gb::render::panel_map_width_metres(layout, settings.panel, content);
      }
    } catch (const std::exception &) {
      // An impossible layout is reported by outputSizeText.
    }
  }
  auto text = QString::fromStdString(ex::write_capture_settings(settings));

  QString warning;
  const auto current = player_.playbackEngine().current();
  if (current.frames && !current.frames->pictures.empty() && view_ != nullptr) {
    try {
      const auto layout =
          ex::frame_layout(settings.aspect, output_width_, settings.panel, panel_size_);
      ex::ResolutionInput input;
      const auto layout_code = player_.sourceLayout();
      input.source = layout_code == gb::render::MaxDualEacLayout ? ex::SourceKind::max_dual_eac
                     : layout_code == gb::render::EquirectangularLayout
                         ? ex::SourceKind::equirectangular
                         : ex::SourceKind::flat;
      input.source_width = current.frames->pictures.front().width;
      input.source_height = current.frames->pictures.front().height;
      input.panorama_crop_width = view_->panoramaCrop().width();
      input.mode = view_->mode();
      input.field_of_view_degrees = view_->fieldOfView();
      input.zoom = view_->zoom();
      input.canvas_aspect = settings.canvas;
      input.crop = settings.crop;
      input.picture_width = layout.picture.width;
      input.picture_height = layout.picture.height;
      if (const auto scale = ex::output_scale(input); scale && *scale > kUpscaleWarning) {
        warning = QStringLiteral("The output has about %1× the pixels the source can resolve at "
                                 "the centre, so it will look soft.")
                      .arg(*scale, 0, 'f', 1);
      }
    } catch (const std::exception &) {
      // An impossible layout is reported by outputSizeText.
    }
  }
  if (text != settings_text_ || warning != resolution_warning_) {
    settings_text_ = text;
    resolution_warning_ = warning;
    emit settingsChanged();
  }
}

QString CaptureController::suggestedPath() const {
  const QFileInfo input(player_.inputPath());
  const auto chapter = player_.chapterNumber();
  const auto position = hasRange() ? std::optional<std::string>(gb::media::format_position_for_file(
                                         chapter, player_.positionMilliseconds()))
                                   : std::nullopt;
  const auto view = ex::view_mode_name(view_ != nullptr ? view_->mode() : 0);
  const auto extension = png_ ? std::string("png") : std::string("jpg");
  const auto stem = input.completeBaseName().toStdString();
  const QDir directory(folder_);
  auto name = QString::fromStdString(ex::still_file_name(stem, position, view, extension));
  for (int number = 2; QFileInfo::exists(directory.filePath(name)) && number < 10000; ++number) {
    name = QString::fromStdString(
        ex::still_file_name(stem, position, view + "_" + std::to_string(number), extension));
  }
  return directory.filePath(name);
}

gb::render::PanelContent CaptureController::panelContent() const {
  gb::render::PanelContent content;
  content.map = panel_map_;
  content.telemetry = panel_telemetry_;
  if (map_ != nullptr) {
    content.scene = map_->scene();
  } else {
    content.scene.min_zoom = player_.tileFetcher()->provider().min_zoom;
    content.scene.max_zoom = player_.tileFetcher()->provider().max_zoom;
  }
  auto &scene = content.scene;
  // North up and centred on the current position, as gb-render draws it; where there is none,
  // as the map pane shows it.
  if (scene.route != nullptr && scene.points != nullptr && scene.record.has_value() &&
      scene.marker != gb::render::MapMarker::none && *scene.record < scene.route->size() &&
      *scene.record < scene.points->size() && (*scene.points)[*scene.record].usable) {
    scene.center = (*scene.route)[*scene.record];
  }
  const auto pane = scene.tile;
  scene.tile = [this, pane](const gb::geo::TileKey key) -> const QImage * {
    if (const auto *image = tiles_.object(packed(key)); image != nullptr)
      return image;
    return pane ? pane(key) : nullptr;
  };
  content.attribution = player_.mapAttribution();
  if (panel_telemetry_) {
    content.lines = gb::render::telemetry_lines(
        gps_fix_ ? std::optional<gb::image::PhotoGps>(*gps_fix_) : std::nullopt);
  }
  return content;
}

void CaptureController::paintPanelPreview(QPainter &painter, const QSizeF size) {
  const auto side = kPanelSides[static_cast<std::size_t>(panel_side_)];
  if (side == ex::PanelSide::none || size.isEmpty())
    return;
  try {
    const auto layout = ex::frame_layout(aspectFor(aspect_index_, free_width_, free_height_),
                                         output_width_, side, panel_size_);
    if (!layout.panel || layout.panel->width <= 0 || layout.panel->height <= 0)
      return;
    const auto scale =
        std::min(size.width() / layout.panel->width, size.height() / layout.panel->height);
    painter.save();
    painter.scale(scale, scale);
    painter.translate(-layout.panel->x, -layout.panel->y);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    (void)gb::render::paint_panel(painter, layout, side, panelContent());
    painter.restore();
  } catch (const std::exception &) {
    // An impossible layout is reported by outputSizeText.
  }
}

std::unique_ptr<CaptureController::Prepared>
CaptureController::prepareCapture(const QString &path) {
  const auto current = player_.playbackEngine().current();
  if (!current.frames || current.frames->pictures.empty())
    throw std::runtime_error("no frame is displayed");
  if (view_ == nullptr)
    throw std::runtime_error("no view");
  const auto format = gb::render::still_format_for(path);
  if (!format)
    throw std::runtime_error("the file name must end in .jpg, .jpeg or .png");

  auto prepared = std::make_unique<Prepared>();
  prepared->path = path;
  prepared->side = kPanelSides[static_cast<std::size_t>(panel_side_)];
  const auto aspect = aspectFor(aspect_index_, free_width_, free_height_);
  prepared->layout = ex::frame_layout(aspect, output_width_, prepared->side, panel_size_);
  gb::render::ViewParameters parameters;
  parameters.mode = view_->mode();
  parameters.layout = view_->sourceLayout();
  parameters.overlap = static_cast<float>(view_->overlap());
  parameters.panorama_crop = view_->panoramaCrop();
  parameters.yaw = static_cast<float>(qDegreesToRadians(view_->yaw()));
  parameters.pitch = static_cast<float>(qDegreesToRadians(view_->pitch()));
  parameters.roll = static_cast<float>(qDegreesToRadians(view_->roll()));
  parameters.field_of_view = static_cast<float>(qDegreesToRadians(view_->fieldOfView()));
  parameters.zoom = static_cast<float>(view_->zoom());
  parameters.pan_x = static_cast<float>(view_->panX());
  parameters.pan_y = static_cast<float>(view_->panY());
  parameters.canvas = QSizeF(canvasAspect(), 1.0);
  parameters.view_crop = crop_;

  if (!renderer_)
    renderer_ = std::make_unique<gb::render::OffscreenRenderer>();
  prepared->frame =
      gb::render::render_still(*renderer_, *current.frames, parameters, prepared->layout);

  prepared->options.format = *format;
  prepared->options.jpeg_quality = jpeg_quality_;
  updateGps();
  if (gps_exif_) {
    if (*format == gb::render::StillFormat::png)
      prepared->warnings << QStringLiteral("PNG stills get no GPS position");
    else if (!gps_fix_)
      prepared->warnings << QStringLiteral("written without GPS: ") + gps_reason_;
    else
      prepared->options.gps = *gps_fix_;
  }
  if (prepared->side != ex::PanelSide::none)
    prepared->content = panelContent();
  return prepared;
}

int CaptureController::requestPanelTiles(const Prepared &prepared) {
  const auto &scene = prepared.content.scene;
  if (prepared.side == ex::PanelSide::none || !prepared.content.map || scene.route == nullptr ||
      scene.route->empty()) {
    return 0;
  }
  std::vector<gb::geo::TileKey> missing;
  for (const auto key : gb::render::panel_tiles(prepared.layout, prepared.side, prepared.content)) {
    if (scene.tile(key) != nullptr || unavailable_.contains(packed(key)))
      continue;
    missing.push_back(key);
  }
  if (!missing.empty())
    player_.tileFetcher()->request(missing);
  return static_cast<int>(missing.size());
}

void CaptureController::onTileAnswered(const int zoom, const int x, const int y,
                                       const QByteArray *data) {
  const auto key = packed({zoom, x, y});
  if (data != nullptr) {
    // Every answered tile is kept: the preview and later captures use them too.
    auto image = std::make_unique<QImage>();
    if (image->loadFromData(*data)) {
      tiles_.insert(key, image.release());
      if (active_)
        emit panelChanged();
    } else if (prepared_) {
      unavailable_.insert(key);
    }
  } else if (prepared_) {
    unavailable_.insert(key);
  }
  if (prepared_ && requestPanelTiles(*prepared_) == 0)
    finishCapture();
}

QStringList CaptureController::completeCapture(Prepared &prepared) {
  auto warnings = prepared.warnings;
  if (prepared.side != ex::PanelSide::none) {
    const auto result =
        gb::render::paint_panel(prepared.frame, prepared.layout, prepared.side, prepared.content);
    const auto &scene = prepared.content.scene;
    if (result.missing_tiles > 0 && scene.route != nullptr && !scene.route->empty()) {
      warnings << (result.missing_tiles == 1
                       ? QStringLiteral("1 map tile not available, drawn as a placeholder")
                       : QStringLiteral("%1 map tiles not available, drawn as placeholders")
                             .arg(result.missing_tiles));
    }
  }
  gb::render::write_still(prepared.frame, prepared.path, prepared.options);
  updateSettings();
  if (!resolution_warning_.isEmpty())
    warnings << resolution_warning_;
  return warnings;
}

QStringList CaptureController::captureTo(const QString &path) {
  auto prepared = prepareCapture(path);
  return completeCapture(*prepared);
}

void CaptureController::capture(const bool ask_name) {
  if (prepared_)
    return;
  if (!player_.hasMedia()) {
    player_.reportStatus(QStringLiteral("Open a recording or photo to capture"), true);
    return;
  }
  // Capture the frame on screen: stop it from changing first.
  if (player_.playing())
    player_.pause();
  auto path = suggestedPath();
  if (ask_name) {
    path = QFileDialog::getSaveFileName(nullptr, QStringLiteral("Capture still"), path,
                                        QStringLiteral("JPEG (*.jpg *.jpeg);;PNG (*.png)"));
    if (path.isEmpty())
      return;
    if (!gb::render::still_format_for(path))
      path += png_ ? QStringLiteral(".png") : QStringLiteral(".jpg");
  }
  startCapture(path);
}

bool CaptureController::startCapture(const QString &path) {
  if (prepared_)
    return false;
  try {
    prepared_ = prepareCapture(path);
  } catch (const std::exception &error) {
    const auto message = QString::fromUtf8(error.what());
    player_.reportStatus(QStringLiteral("Capture failed: %1").arg(message), true);
    emit captureFinished(path, {}, message);
    return true;
  }
  unavailable_.clear();
  emit busyChanged();
  if (const auto awaited = requestPanelTiles(*prepared_); awaited > 0) {
    player_.reportStatus(QStringLiteral("Fetching map tiles for the panel (%1)…").arg(awaited));
    tile_clock_.start();
    tile_timer_.start();
    return true;
  }
  finishCapture();
  return true;
}

void CaptureController::finishCapture() {
  tile_timer_.stop();
  if (!prepared_)
    return;
  const auto prepared = std::move(prepared_);
  unavailable_.clear();
  emit busyChanged();
  const auto path = prepared->path;
  QStringList warnings;
  try {
    warnings = completeCapture(*prepared);
  } catch (const std::exception &error) {
    const auto message = QString::fromUtf8(error.what());
    player_.reportStatus(QStringLiteral("Capture failed: %1").arg(message), true);
    emit captureFinished(path, {}, message);
    return;
  }
  const auto directory = QFileInfo(path).absolutePath();
  if (directory != folder_) {
    folder_ = directory;
    emit outputChanged();
  }
  QSettings().setValue(kFolderSetting, folder_);
  auto message =
      QStringLiteral("Captured %1 (%2)").arg(QFileInfo(path).fileName(), outputSizeText());
  if (!warnings.isEmpty())
    message += QStringLiteral(" — ") + warnings.join(QStringLiteral("; "));
  player_.reportStatus(message, false);
  emit captureFinished(path, warnings, {});
}

void CaptureController::cancelCapture(const QString &reason) {
  tile_timer_.stop();
  if (!prepared_)
    return;
  const auto path = prepared_->path;
  prepared_.reset();
  unavailable_.clear();
  emit busyChanged();
  player_.reportStatus(reason, true);
  emit captureFinished(path, {}, reason);
}

void CaptureController::chooseFolder() {
  const auto chosen =
      QFileDialog::getExistingDirectory(nullptr, QStringLiteral("Folder for captures"), folder_);
  if (chosen.isEmpty() || chosen == folder_)
    return;
  folder_ = chosen;
  QSettings().setValue(kFolderSetting, folder_);
  emit outputChanged();
}

void CaptureController::copySettings() {
  updateSettings();
  QGuiApplication::clipboard()->setText(settings_text_);
  player_.reportStatus(QStringLiteral("Settings copied to the clipboard"));
}

void CaptureController::saveSettingsAs() {
  updateSettings();
  const QFileInfo input(player_.inputPath());
  const auto suggested =
      QDir(folder_).filePath(input.completeBaseName() + QStringLiteral("_framing.ini"));
  const auto path =
      QFileDialog::getSaveFileName(nullptr, QStringLiteral("Save settings"), suggested,
                                   QStringLiteral("gb-render settings (*.ini)"));
  if (path.isEmpty())
    return;
  QSaveFile file(path);
  const auto data = settings_text_.toUtf8();
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
    player_.reportStatus(
        QStringLiteral("Cannot save %1: %2").arg(QFileInfo(path).fileName(), file.errorString()),
        true);
    return;
  }
  player_.reportStatus(QStringLiteral("Settings saved to %1").arg(QFileInfo(path).fileName()));
}
