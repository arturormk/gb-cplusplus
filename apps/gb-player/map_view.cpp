#include "map_view.hpp"

#include "gb/geo/route.hpp"
#include "gb/tiles/tile_fetcher.hpp"
#include "player_controller.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

constexpr int kMemoryTiles = 192;
constexpr int kAncestorLevels = 4;
constexpr qint64 kRetryUnavailableMs = 60000;
constexpr double kDragThreshold = 4.0;
constexpr double kClickTolerancePixels = 10.0;
constexpr double kMinimumClickToleranceMetres = 5.0;
constexpr int kAnimationIntervalMs = 16;
// Time constants of the display filters. GPS course from neighbouring fixes swings by tens of
// degrees between records at walking pace; ~0.5 s of smoothing keeps the arrow steady while still
// following real turns.
constexpr double kCourseTimeConstant = 0.5;
constexpr double kPanTimeConstant = 0.3;
// A record jump larger than this is a seek: the arrow takes the new course at once.
constexpr std::size_t kSnapRecords = 25;

quint64 packed(const gb::geo::TileKey key) {
  return (static_cast<quint64>(key.zoom) << 56U) | (static_cast<quint64>(key.x) << 28U) |
         static_cast<quint64>(key.y);
}

const QColor kBackground(0x1a, 0x23, 0x2b);
const QColor kPlaceholder(0x22, 0x2d, 0x36);
const QColor kRemaining(0x3b, 0x5b, 0xdb);
const QColor kTravelled(0xf7, 0x67, 0x07);

} // namespace

MapView::MapView(QQuickItem *parent) : QQuickPaintedItem(parent), images_(kMemoryTiles) {
  setAcceptedMouseButtons(Qt::LeftButton);
  setFillColor(kBackground);
  setOpaquePainting(true);
  clock_.start();
  animation_.setInterval(kAnimationIntervalMs);
  animation_.setTimerType(Qt::PreciseTimer);
  connect(&animation_, &QTimer::timeout, this, &MapView::animate);
}

QObject *MapView::controller() const { return player_; }

void MapView::setController(QObject *object) {
  auto *controller = qobject_cast<PlayerController *>(object);
  if (controller == player_)
    return;
  if (player_ != nullptr) {
    player_->disconnect(this);
    player_->tileFetcher()->disconnect(this);
  }
  player_ = controller;
  if (player_ != nullptr) {
    connect(player_, &PlayerController::mapRouteChanged, this, &MapView::onRouteChanged);
    connect(player_, &PlayerController::mapPositionChanged, this, &MapView::onPositionChanged);
    auto *fetcher = player_->tileFetcher();
    connect(fetcher, &gb::tiles::TileFetcher::tileReady, this, &MapView::onTileReady);
    connect(fetcher, &gb::tiles::TileFetcher::tileUnavailable, this, &MapView::onTileUnavailable);
    connect(fetcher, &gb::tiles::TileFetcher::statisticsChanged, this, &MapView::updateTileStatus);
  }
  emit controllerChanged();
  onRouteChanged();
}

double MapView::minimumZoom() const {
  return player_ != nullptr ? std::max(1, player_->tileFetcher()->provider().min_zoom) : 1.0;
}

// One level beyond the provider's deepest tiles, drawn by scaling them up.
double MapView::maximumZoom() const {
  return player_ != nullptr ? player_->tileFetcher()->provider().max_zoom + 1.0 : 19.0;
}

void MapView::setZoom(const double zoom) {
  const auto clamped = std::clamp(zoom, minimumZoom(), maximumZoom());
  if (!std::isfinite(clamped) || clamped == zoom_)
    return;
  zoom_ = clamped;
  emit viewChanged();
  refreshTiles();
}

void MapView::setFollow(const bool follow) {
  if (follow_ == follow)
    return;
  follow_ = follow;
  emit followChanged();
  if (follow_)
    onPositionChanged();
}

void MapView::zoomBy(const double steps) {
  zoomAround(zoom_ + steps, {width() / 2.0, height() / 2.0});
}

void MapView::zoomAround(const double new_zoom, const QPointF anchor) {
  const auto clamped = std::clamp(new_zoom, minimumZoom(), maximumZoom());
  if (clamped == zoom_)
    return;
  const auto before = viewport();
  const gb::geo::WorldPoint under{(before.left + anchor.x()) / before.world_pixels,
                                  (before.top + anchor.y()) / before.world_pixels};
  zoom_ = clamped;
  const auto world_pixels = 256.0 * std::pow(2.0, zoom_);
  const auto offset_x = (anchor.x() - width() / 2.0) / world_pixels;
  const auto offset_y = (anchor.y() - height() / 2.0) / world_pixels;
  center_ = {under.x - offset_x, std::clamp(under.y - offset_y, 0.0, 1.0)};
  emit viewChanged();
  refreshTiles();
}

MapView::Viewport MapView::viewport() const {
  Viewport view;
  const auto provider_min = player_ != nullptr ? player_->tileFetcher()->provider().min_zoom : 0;
  const auto provider_max = player_ != nullptr ? player_->tileFetcher()->provider().max_zoom : 19;
  view.tile_zoom = std::clamp(static_cast<int>(std::lround(zoom_)), provider_min, provider_max);
  // Logical pixels: one tile spans 256 pixels at its own zoom regardless of the provider's size.
  view.world_pixels = 256.0 * std::pow(2.0, zoom_);
  view.tile_pixels = view.world_pixels / std::ldexp(1.0, view.tile_zoom);
  view.left = center_.x * view.world_pixels - width() / 2.0;
  view.top = center_.y * view.world_pixels - height() / 2.0;
  return view;
}

std::vector<gb::geo::TileKey> MapView::visibleTiles(const Viewport &view) const {
  std::vector<gb::geo::TileKey> keys;
  if (width() <= 0.0 || height() <= 0.0 || route_.empty())
    return keys;
  const auto count = 1 << view.tile_zoom;
  const auto first_x = static_cast<int>(std::floor(view.left / view.tile_pixels));
  const auto last_x = static_cast<int>(std::floor((view.left + width()) / view.tile_pixels));
  const auto first_y = std::max(0, static_cast<int>(std::floor(view.top / view.tile_pixels)));
  const auto last_y =
      std::min(count - 1, static_cast<int>(std::floor((view.top + height()) / view.tile_pixels)));
  const auto center_x = (view.left + width() / 2.0) / view.tile_pixels;
  const auto center_y = (view.top + height() / 2.0) / view.tile_pixels;
  std::vector<std::pair<double, gb::geo::TileKey>> ordered;
  for (int x = first_x; x <= last_x && x - first_x < count; ++x) {
    for (int y = first_y; y <= last_y; ++y) {
      const auto wrapped = ((x % count) + count) % count;
      const auto dx = x + 0.5 - center_x;
      const auto dy = y + 0.5 - center_y;
      ordered.emplace_back(dx * dx + dy * dy, gb::geo::TileKey{view.tile_zoom, wrapped, y});
    }
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  for (const auto &[distance, key] : ordered)
    keys.push_back(key);
  return keys;
}

const QImage *MapView::tileImage(const gb::geo::TileKey key) const {
  return images_.object(packed(key));
}

void MapView::refreshTiles() {
  update();
  if (player_ == nullptr)
    return;
  auto *fetcher = player_->tileFetcher();
  const auto wanted = visibleTiles(viewport());
  for (const auto key : fetcher->retain(wanted))
    pending_.erase(key);
  std::vector<gb::geo::TileKey> missing;
  const auto now = clock_.elapsed();
  int unavailable = 0;
  for (const auto key : wanted) {
    if (images_.contains(packed(key)) || pending_.contains(key))
      continue;
    if (const auto failed = unavailable_.find(key); failed != unavailable_.end()) {
      if (now - failed->second < kRetryUnavailableMs) {
        ++unavailable;
        continue;
      }
      unavailable_.erase(failed);
    }
    missing.push_back(key);
    pending_.insert(key);
  }
  missing_tiles_ = unavailable;
  if (!missing.empty())
    fetcher->request(missing);
  updateTileStatus();
}

void MapView::onTileReady(const int zoom, const int x, const int y, const QByteArray &data) {
  const gb::geo::TileKey key{zoom, x, y};
  pending_.erase(key);
  unavailable_.erase(key);
  auto image = std::make_unique<QImage>();
  if (!image->loadFromData(data)) {
    unavailable_[key] = clock_.elapsed();
    last_unavailable_reason_ = QStringLiteral("undecodable tile image");
  } else {
    images_.insert(packed(key), image.release());
  }
  update();
}

void MapView::onTileUnavailable(const int zoom, const int x, const int y, const QString &reason) {
  const gb::geo::TileKey key{zoom, x, y};
  pending_.erase(key);
  unavailable_[key] = clock_.elapsed();
  last_unavailable_reason_ = reason;
  ++missing_tiles_;
  updateTileStatus();
  update();
}

void MapView::updateTileStatus() {
  QString status;
  if (player_ != nullptr) {
    const auto *fetcher = player_->tileFetcher();
    const auto statistics = fetcher->statistics();
    if (statistics.blocked) {
      status = QStringLiteral("Tile server refused service (%1); showing cached tiles only")
                   .arg(statistics.last_error);
    } else if (!fetcher->networkEnabled()) {
      status = QStringLiteral("Network disabled: cached tiles only");
    } else if (statistics.in_flight + statistics.queued > 0) {
      status = QStringLiteral("Loading %1 tiles…").arg(statistics.in_flight + statistics.queued);
    }
    if (missing_tiles_ > 0 && !last_unavailable_reason_.isEmpty()) {
      status += (status.isEmpty() ? QString() : QStringLiteral(" · ")) +
                QStringLiteral("%1 tiles unavailable: %2")
                    .arg(missing_tiles_)
                    .arg(last_unavailable_reason_);
    }
  }
  if (status != tile_status_) {
    tile_status_ = status;
    emit tileStatusChanged();
  }
}

void MapView::onRouteChanged() {
  route_.clear();
  usable_.clear();
  shown_course_.reset();
  last_record_.reset();
  if (player_ != nullptr) {
    for (const auto &point : player_->mapRoute()) {
      route_.push_back(gb::geo::project(point.position));
      usable_.push_back(point.usable);
    }
  }
  // A route with no usable point cannot be shown; treat it as no route.
  if (std::none_of(usable_.begin(), usable_.end(), [](const bool usable) { return usable; })) {
    route_.clear();
    usable_.clear();
  }
  emit routeChanged();
  if (!route_.empty()) {
    if (!follow_) {
      follow_ = true;
      emit followChanged();
    }
    fit_pending_ = true;
    fitRoute();
  }
  refreshTiles();
}

void MapView::fitRoute() {
  if (player_ == nullptr || route_.empty())
    return;
  if (width() <= 0.0 || height() <= 0.0) {
    fit_pending_ = true;
    return;
  }
  fit_pending_ = false;
  const auto bounds = gb::geo::route_bounds(player_->mapRoute());
  if (!bounds.has_value())
    return;
  constexpr double kPadding = 32.0;
  const auto span_x = std::max(bounds->maximum.x - bounds->minimum.x, 1e-12);
  const auto span_y = std::max(bounds->maximum.y - bounds->minimum.y, 1e-12);
  const auto fit_x = std::log2(std::max(1.0, width() - 2.0 * kPadding) / (256.0 * span_x));
  const auto fit_y = std::log2(std::max(1.0, height() - 2.0 * kPadding) / (256.0 * span_y));
  // A stationary recording should not open at street-furniture scale.
  zoom_ = std::clamp(std::min({fit_x, fit_y, 17.0}), minimumZoom(), maximumZoom());
  center_ = {(bounds->minimum.x + bounds->maximum.x) / 2.0,
             (bounds->minimum.y + bounds->maximum.y) / 2.0};
  emit viewChanged();
  refreshTiles();
}

std::optional<gb::geo::WorldPoint> MapView::followTarget() const {
  if (player_ == nullptr || route_.empty())
    return std::nullopt;
  const auto record = player_->mapRecord();
  const auto state = player_->mapState();
  if (!record.has_value() || *record >= route_.size() || !usable_[*record] ||
      (state != QStringLiteral("valid") && state != QStringLiteral("gap"))) {
    return std::nullopt;
  }
  return route_[*record];
}

void MapView::onPositionChanged() {
  if (player_ != nullptr) {
    const auto record = player_->mapRecord();
    const auto course = player_->mapCourse();
    // The first course, or one after a seek, is shown at once instead of swinging round to it.
    const auto jumped =
        !last_record_.has_value() || !record.has_value() ||
        std::max(*record, *last_record_) - std::min(*record, *last_record_) > kSnapRecords;
    if (course.has_value() && (jumped || !shown_course_.has_value()))
      shown_course_ = *course;
    if (record.has_value())
      last_record_ = record;
  }
  if (follow_) {
    if (const auto target = followTarget(); target.has_value()) {
      // Far away (a seek, or follow just switched on): jump rather than glide across the world.
      const auto world_pixels = viewport().world_pixels;
      const auto dx = std::remainder(target->x - center_.x, 1.0) * world_pixels;
      const auto dy = (target->y - center_.y) * world_pixels;
      if (std::hypot(dx, dy) > std::max(width(), height())) {
        center_ = *target;
        emit viewChanged();
        refreshTiles();
      }
    }
  }
  startAnimation();
  update();
}

void MapView::startAnimation() {
  if (animation_.isActive())
    return;
  animation_clock_.start();
  animation_.start();
}

// One step of the display filters: the arrow turns towards the current course along the shorter
// way round, and while following the centre glides towards the marker. Stops once both settle.
void MapView::animate() {
  const auto dt = std::min(0.1, static_cast<double>(animation_clock_.restart()) / 1000.0);
  bool active = false;
  const auto course = player_ != nullptr ? player_->mapCourse() : std::nullopt;
  if (course.has_value() && shown_course_.has_value()) {
    const auto difference = std::remainder(*course - *shown_course_, 360.0);
    if (std::fabs(difference) > 0.1) {
      *shown_course_ += difference * (1.0 - std::exp(-dt / kCourseTimeConstant));
      active = true;
    } else {
      shown_course_ = *course;
    }
    update();
  }
  if (follow_ && !dragging_) {
    if (const auto target = followTarget(); target.has_value()) {
      const auto dx = std::remainder(target->x - center_.x, 1.0);
      const auto dy = target->y - center_.y;
      if (dx != 0.0 || dy != 0.0) {
        if (std::hypot(dx, dy) * viewport().world_pixels > 0.25) {
          const auto step = 1.0 - std::exp(-dt / kPanTimeConstant);
          center_.x += dx * step;
          center_.x -= std::floor(center_.x);
          center_.y = std::clamp(center_.y + dy * step, 0.0, 1.0);
          active = true;
        } else {
          center_ = *target;
        }
        emit viewChanged();
        refreshTiles();
      }
    }
  }
  if (!active)
    animation_.stop();
}

void MapView::geometryChange(const QRectF &new_geometry, const QRectF &old_geometry) {
  QQuickPaintedItem::geometryChange(new_geometry, old_geometry);
  if (new_geometry.size() == old_geometry.size())
    return;
  if (fit_pending_)
    fitRoute();
  else
    refreshTiles();
}

void MapView::paint(QPainter *painter) {
  ++paint_count_;
  painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
  if (route_.empty() || player_ == nullptr) {
    painted_record_.store(-1);
    return;
  }
  const auto view = viewport();
  const auto count = 1 << view.tile_zoom;
  const auto first_x = static_cast<int>(std::floor(view.left / view.tile_pixels));
  const auto last_x = static_cast<int>(std::floor((view.left + width()) / view.tile_pixels));
  const auto first_y = std::max(0, static_cast<int>(std::floor(view.top / view.tile_pixels)));
  const auto last_y =
      std::min(count - 1, static_cast<int>(std::floor((view.top + height()) / view.tile_pixels)));

  for (int x = first_x; x <= last_x && x - first_x < count; ++x) {
    for (int y = first_y; y <= last_y; ++y) {
      const QRectF target(x * view.tile_pixels - view.left, y * view.tile_pixels - view.top,
                          view.tile_pixels, view.tile_pixels);
      const auto wrapped = ((x % count) + count) % count;
      if (const auto *image = tileImage({view.tile_zoom, wrapped, y}); image != nullptr) {
        painter->drawImage(target, *image);
        continue;
      }
      // Placeholder: the nearest cached ancestor, scaled up, until the tile itself arrives.
      bool drawn = false;
      for (int level = 1; level <= kAncestorLevels && view.tile_zoom - level >= 0 && !drawn;
           ++level) {
        const gb::geo::TileKey parent{view.tile_zoom - level, wrapped >> level, y >> level};
        if (const auto *image = tileImage(parent); image != nullptr) {
          const auto fraction = 1.0 / static_cast<double>(1 << level);
          const auto sx = (wrapped - (parent.x << level)) * fraction * image->width();
          const auto sy = (y - (parent.y << level)) * fraction * image->height();
          painter->drawImage(target, *image,
                             QRectF(sx, sy, image->width() * fraction, image->height() * fraction));
          drawn = true;
        }
      }
      if (!drawn)
        painter->fillRect(target.adjusted(0.5, 0.5, -0.5, -0.5), kPlaceholder);
    }
  }

  painter->setRenderHint(QPainter::Antialiasing, true);
  const auto screen = [&view](const gb::geo::WorldPoint point) {
    return QPointF(point.x * view.world_pixels - view.left, point.y * view.world_pixels - view.top);
  };
  const auto record = player_->mapRecord();
  const auto state = player_->mapState();
  // Each run of usable records becomes up to two paths, travelled and remaining, that meet at the
  // current record. Points closer than 1.5 px to the previous kept point are skipped.
  QPainterPath travelled;
  QPainterPath remaining;
  const auto add = [&](QPainterPath &path, const std::size_t begin, const std::size_t end) {
    if (end < begin + 2)
      return;
    QPointF previous = screen(route_[begin]);
    path.moveTo(previous);
    for (auto index = begin + 1; index < end; ++index) {
      const auto point = screen(route_[index]);
      if (index + 1 < end && std::hypot(point.x() - previous.x(), point.y() - previous.y()) < 1.5)
        continue;
      path.lineTo(point);
      previous = point;
    }
  };
  for (const auto &[first, last] : gb::geo::usable_runs(player_->mapRoute())) {
    if (!record.has_value() || *record < first) {
      add(remaining, first, last);
    } else if (*record >= last) {
      add(travelled, first, last);
    } else {
      add(travelled, first, *record + 1);
      add(remaining, *record, last);
    }
  }
  const auto stroke = [painter](const QPainterPath &path, const QColor &color) {
    painter->strokePath(
        path, QPen(QColor(255, 255, 255, 220), 6.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->strokePath(path, QPen(color, 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  };
  stroke(remaining, kRemaining);
  stroke(travelled, kTravelled);

  // Start and end of the recorded route. A single position (a photo) has neither.
  const auto first_usable = std::find(usable_.begin(), usable_.end(), true) - usable_.begin();
  const auto last_usable = usable_.rend() - std::find(usable_.rbegin(), usable_.rend(), true) - 1;
  if (route_.size() > 1U) {
    painter->setPen(QPen(Qt::white, 2.0));
    painter->setBrush(QColor(0x2f, 0x9e, 0x44));
    painter->drawEllipse(screen(route_[static_cast<std::size_t>(first_usable)]), 5.5, 5.5);
    painter->setBrush(QColor(0xc9, 0x2a, 0x2a));
    const auto end_point = screen(route_[static_cast<std::size_t>(last_usable)]);
    painter->drawRect(QRectF(end_point.x() - 5.0, end_point.y() - 5.0, 10.0, 10.0));
  }

  long long drawn_record = -1;
  if (record.has_value() && *record < route_.size() && usable_[*record]) {
    const auto position = screen(route_[*record]);
    if (state == QStringLiteral("valid")) {
      if (const auto course = player_->mapCourse(); course.has_value()) {
        // Direction of travel from neighbouring fixes, not the camera's pointing direction,
        // smoothed for display.
        painter->save();
        painter->translate(position);
        painter->rotate(shown_course_.value_or(*course));
        QPainterPath arrow;
        arrow.moveTo(0.0, -19.0);
        arrow.lineTo(7.0, -8.0);
        arrow.lineTo(-7.0, -8.0);
        arrow.closeSubpath();
        painter->setPen(QPen(Qt::white, 1.5));
        painter->setBrush(kTravelled.darker(130));
        painter->drawPath(arrow);
        painter->restore();
      }
      painter->setPen(QPen(Qt::white, 3.0));
      painter->setBrush(kTravelled);
      painter->drawEllipse(position, 8.0, 8.0);
      drawn_record = static_cast<long long>(*record);
    } else if (state == QStringLiteral("gap")) {
      // Inside a GPS gap: a hollow ring at the nearest recorded fix, not an estimated position.
      painter->setBrush(Qt::NoBrush);
      painter->setPen(QPen(QColor(255, 255, 255, 230), 5.0));
      painter->drawEllipse(position, 9.0, 9.0);
      painter->setPen(QPen(QColor(0x49, 0x50, 0x57), 2.5, Qt::DashLine));
      painter->drawEllipse(position, 9.0, 9.0);
      drawn_record = static_cast<long long>(*record);
    }
  }
  painted_record_.store(drawn_record);
}

void MapView::mousePressEvent(QMouseEvent *event) {
  press_position_ = event->position();
  last_position_ = event->position();
  dragging_ = false;
  event->accept();
}

void MapView::mouseMoveEvent(QMouseEvent *event) {
  const auto position = event->position();
  if (!dragging_ && std::hypot(position.x() - press_position_.x(),
                               position.y() - press_position_.y()) < kDragThreshold) {
    return;
  }
  if (!dragging_) {
    dragging_ = true;
    setFollow(false);
  }
  const auto view = viewport();
  center_.x -= (position.x() - last_position_.x()) / view.world_pixels;
  center_.y =
      std::clamp(center_.y - (position.y() - last_position_.y()) / view.world_pixels, 0.0, 1.0);
  center_.x -= std::floor(center_.x);
  last_position_ = position;
  emit viewChanged();
  refreshTiles();
  event->accept();
}

void MapView::mouseReleaseEvent(QMouseEvent *event) {
  if (!dragging_ && !route_.empty()) {
    const auto view = viewport();
    const gb::geo::WorldPoint world{(view.left + event->position().x()) / view.world_pixels,
                                    (view.top + event->position().y()) / view.world_pixels};
    auto wrapped = world;
    wrapped.x -= std::floor(wrapped.x);
    const auto position = gb::geo::unproject(wrapped);
    const auto metres_per_pixel =
        gb::geo::metres_per_world_unit(position.latitude) / view.world_pixels;
    emit routeClicked(
        position.latitude, position.longitude,
        std::max(kMinimumClickToleranceMetres, metres_per_pixel * kClickTolerancePixels));
  }
  dragging_ = false;
  event->accept();
}

void MapView::mouseDoubleClickEvent(QMouseEvent *event) {
  // A double click is two clicks (each may offer seek targets); it does not zoom.
  event->accept();
}

void MapView::wheelEvent(QWheelEvent *event) {
  const auto steps = event->angleDelta().y() / 120.0;
  if (follow_)
    zoomBy(steps * 0.5);
  else
    zoomAround(zoom_ + steps * 0.5, event->position());
  event->accept();
}
