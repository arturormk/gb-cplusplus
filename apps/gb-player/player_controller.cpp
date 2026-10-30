#include "player_controller.hpp"

#include "audio_output.hpp"
#include "gb/container/iso_bmff.hpp"
#include "gb/media/audio.hpp"
#include "gb/media/chapters.hpp"
#include "gb/media/playback.hpp"
#include "gb/metadata/gpmf.hpp"
#include "gb/telemetry/telemetry.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>

#ifndef GB_PLAYER_DISPLAY_NAME
#define GB_PLAYER_DISPLAY_NAME "gb-player"
#endif

struct PlayerController::Source {
  gb::media::PlaybackLayout layout;
  std::vector<gb::media::VideoStream> streams;
};

struct PlayerController::LoadedMetadata {
  std::optional<gb::container::Inspection> inspection;
  QString inspection_error;
  std::optional<gb::telemetry::Extraction> telemetry;
  QString telemetry_error;
  std::optional<std::size_t> gpmd_track;
};

namespace {

constexpr std::size_t kMaximumGpmfRows = 600;
constexpr std::size_t kMaximumValuesShown = 6;

QVariantMap row(const QString &label, const QString &value, const QString &state = QStringLiteral("ok")) {
  return {{QStringLiteral("label"), label}, {QStringLiteral("value"), value}, {QStringLiteral("state"), state}};
}

QString unavailable() { return QStringLiteral("unavailable"); }

QString number(const long double value, const int precision) {
  return QString::number(static_cast<double>(value), 'f', precision);
}

QString rational(const std::uint64_t numerator, const std::uint64_t denominator) {
  return QStringLiteral("%1/%2").arg(numerator).arg(denominator);
}

std::optional<gb::media::Rational> mediaRational(const gb::container::Rational value) {
  constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
  if (value.numerator == 0U || value.denominator == 0U || value.numerator > limit ||
      value.denominator > limit) {
    return std::nullopt;
  }
  return gb::media::Rational{static_cast<std::int32_t>(value.numerator),
                             static_cast<std::int32_t>(value.denominator)};
}

long double seconds(const std::int64_t ticks, const gb::media::Rational base) {
  return static_cast<long double>(ticks) * base.numerator / base.denominator;
}

QString clockText(const long double total_seconds) {
  if (!std::isfinite(total_seconds) || total_seconds < 0.0L) return QStringLiteral("--:--:--.---");
  const auto milliseconds = static_cast<std::int64_t>(std::floor(total_seconds * 1000.0L));
  const auto hours = milliseconds / 3600000;
  const auto minutes = (milliseconds / 60000) % 60;
  const auto secs = (milliseconds / 1000) % 60;
  const auto millis = milliseconds % 1000;
  return QStringLiteral("%1:%2:%3.%4")
      .arg(hours, 2, 10, QLatin1Char('0'))
      .arg(minutes, 2, 10, QLatin1Char('0'))
      .arg(secs, 2, 10, QLatin1Char('0'))
      .arg(millis, 3, 10, QLatin1Char('0'));
}

QString fixName(const std::optional<std::uint32_t> fix) {
  if (!fix.has_value()) return unavailable();
  switch (*fix) {
  case 0U: return QStringLiteral("no fix");
  case 2U: return QStringLiteral("2D");
  case 3U: return QStringLiteral("3D");
  default: return QStringLiteral("recorded value %1").arg(*fix);
  }
}

QString speedText(const long double metres_per_second) {
  return QStringLiteral("%1 m/s  (%2 km/h)")
      .arg(number(metres_per_second, 3), number(metres_per_second * 3.6L, 1));
}

QString azimuthText(const long double degrees) {
  static constexpr const char *kCompassPoints[] = {"N",  "NNE", "NE", "ENE", "E",  "ESE", "SE", "SSE",
                                                   "S",  "SSW", "SW", "WSW", "W",  "WNW", "NW", "NNW"};
  // Round once so the shown angle and compass point agree, and 359.96° reads 0.0° N rather than 360.0°.
  auto shown = std::round(degrees * 10.0L) / 10.0L;
  if (shown >= 360.0L) shown -= 360.0L;
  const auto point = static_cast<std::size_t>(std::floor((shown + 11.25L) / 22.5L)) % 16U;
  return QStringLiteral("%1°  %2").arg(number(shown, 1), QLatin1String(kCompassPoints[point]));
}

QString elementDetail(const gb::metadata::GpmfElement &element) {
  if (!element.children.empty()) return QStringLiteral("%1 children").arg(element.children.size());
  const auto &values = element.normalized_values.empty() ? element.values : element.normalized_values;
  if (values.empty()) return QStringLiteral("%1 raw bytes").arg(element.raw.size());
  QStringList shown;
  for (std::size_t index = 0; index < values.size() && index < kMaximumValuesShown; ++index) {
    shown << QString::fromStdString(values[index]);
  }
  auto text = shown.join(QStringLiteral(", "));
  if (values.size() > kMaximumValuesShown) {
    text += QStringLiteral(" … (%1 values)").arg(values.size());
  }
  return text;
}

void flatten(const std::vector<gb::metadata::GpmfElement> &elements, const int depth, QVariantList &rows) {
  for (const auto &element : elements) {
    if (static_cast<std::size_t>(rows.size()) >= kMaximumGpmfRows) return;
    rows.push_back(QVariantMap{
        {QStringLiteral("depth"), depth},
        {QStringLiteral("key"), QString::fromStdString(element.key)},
        {QStringLiteral("type"), QString::fromStdString(gb::metadata::type_name(element.type))},
        {QStringLiteral("repeat"), element.repeat},
        {QStringLiteral("detail"), elementDetail(element)},
        {QStringLiteral("unit"), QString::fromStdString(element.unit)},
        {QStringLiteral("known"), gb::metadata::is_known_key(element.key)}});
    flatten(element.children, depth + 1, rows);
  }
}

double percentile(std::vector<double> values, const double fraction) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(std::llround(fraction * static_cast<double>(values.size() - 1U)));
  return values[index];
}

} // namespace

PlayerController::PlayerController(PlayerOptions options, QObject *parent)
    : QObject(parent), options_(options), source_(std::make_unique<Source>()) {
  status_ = QStringLiteral("Open a GoPro video to begin");
  connect(&engine_, &PlaybackEngine::frameChanged, this, &PlayerController::onFrameChanged);
  connect(&engine_, &PlaybackEngine::playingChanged, this, &PlayerController::playingChanged);
  connect(&engine_, &PlaybackEngine::audioChanged, this, &PlayerController::audioChanged);
  connect(&engine_, &PlaybackEngine::errorOccurred, this,
          [this](const QString &message) { setStatus(QStringLiteral("Decode error: ") + message, true); });
  // Queued: opening the next chapter closes the engine, which is still emitting.
  connect(&engine_, &PlaybackEngine::endReached, this, &PlayerController::onEndReached, Qt::QueuedConnection);
  diagnostics_timer_.setInterval(1000);
  connect(&diagnostics_timer_, &QTimer::timeout, this, &PlayerController::updateDiagnostics);
  if (options_.map_tiles.user_agent.isEmpty()) {
    options_.map_tiles.user_agent = QStringLiteral("gb-player/%1").arg(QStringLiteral(GB_PLAYER_VERSION));
  }
  tiles_ = new gb::tiles::TileFetcher(options_.map_provider, options_.map_tiles, this);
}

PlayerController::~PlayerController() {
  engine_.close();
  if (loader_.joinable()) loader_.join();
}

QString PlayerController::displayName() const { return QStringLiteral(GB_PLAYER_DISPLAY_NAME); }
const gb::telemetry::Extraction *PlayerController::telemetry() const {
  return metadata_ && metadata_->telemetry ? &*metadata_->telemetry : nullptr;
}
QString PlayerController::mapAttribution() const { return QString::fromStdString(tiles_->provider().attribution); }
QString PlayerController::mapAttributionUrl() const { return QString::fromStdString(tiles_->provider().attribution_url); }
QString PlayerController::mapProviderName() const { return QString::fromStdString(tiles_->provider().name); }
QObject *PlayerController::engine() { return &engine_; }
PlaybackEngine &PlayerController::playbackEngine() { return engine_; }
QString PlayerController::inputName() const { return QFileInfo(input_path_).fileName(); }

QStringList PlayerController::sequenceNames() const {
  QStringList names;
  for (const auto &path : sequence_paths_) names.push_back(QFileInfo(path).fileName());
  return names;
}

int PlayerController::overlap() const { return source_->layout.overlap; }

int PlayerController::sourceLayout() const {
  return engine_.isOpen() && source_->layout.layout == gb::media::SourceLayout::max_dual_eac ? 1 : 0;
}

QString PlayerController::layoutDescription() const {
  return engine_.isOpen() ? QString::fromStdString(source_->layout.description) : QString();
}

double PlayerController::duration() const {
  return engine_.isOpen() ? static_cast<double>(seconds(engine_.durationTicks(), engine_.timeBase())) : 0.0;
}

QString PlayerController::durationText() const { return clockText(duration()); }

double PlayerController::position() const {
  return engine_.isOpen() ? static_cast<double>(seconds(engine_.positionTicks(), engine_.timeBase())) : 0.0;
}

QString PlayerController::positionText() const { return clockText(position()); }

QString PlayerController::ptsText() const {
  if (!engine_.isOpen()) return {};
  const auto base = engine_.timeBase();
  return QStringLiteral("PTS %1 @ %2/%3").arg(engine_.positionTicks()).arg(base.numerator).arg(base.denominator);
}

QString PlayerController::formatTicks(const std::int64_t ticks) const {
  return clockText(seconds(ticks, engine_.timeBase()));
}

void PlayerController::setStatus(const QString &message, const bool error) {
  status_ = message;
  status_is_error_ = error;
  emit statusChanged();
}

void PlayerController::openDialog() {
  const auto selected = QFileDialog::getOpenFileName(
      nullptr, QStringLiteral("Open GoPro media"), QFileInfo(input_path_).absolutePath(),
      QStringLiteral("GoPro video (*.360 *.MP4 *.mp4 *.MOV *.mov);;All files (*)"));
  if (!selected.isEmpty()) open(selected);
}

bool PlayerController::open(const QString &path, const double start_seconds) {
  const auto source = path.toStdString();
  try {
    const auto streams = gb::media::video_streams(source);
    const auto layout = gb::media::select_playback_layout(streams);
    ++load_generation_;
    if (loader_.joinable()) loader_.join();
    metadata_.reset();
    metadata_loaded_ = false;
    source_->layout = layout;
    source_->streams = streams;
    input_path_ = QFileInfo(path).absoluteFilePath();
    sequence_paths_.clear();
    sequence_index_ = 0;
    for (const auto &chapter : gb::media::chapter_sequence(input_path_.toStdString())) {
      const auto chapter_path = QString::fromStdString(chapter.string());
      if (chapter_path == input_path_) sequence_index_ = static_cast<int>(sequence_paths_.size());
      sequence_paths_.push_back(chapter_path);
    }
    const auto base = streams.front().time_base;
    const auto start = std::isfinite(start_seconds) && start_seconds > 0.0
                           ? gb::media::rescale_ticks(std::llround(start_seconds * 1000.0), {1, 1000}, base)
                           : 0;
    gb::media::AudioSelection audio;
    try {
      audio = gb::media::select_playback_audio(gb::media::audio_streams(source));
    } catch (const std::exception &error) {
      audio.description = std::string("audio streams cannot be read: ") + error.what();
    }
    if (!options_.audio && audio.stream.has_value()) {
      audio.description = "disabled by --no-audio (" + audio.description + ")";
      audio.stream.reset();
    }
    audio_description_ = QString::fromStdString(audio.description);
    engine_.open(source, layout, options_.decoder_threads, options_.queue_capacity, start, audio.stream,
                 displayName().toStdString());
  } catch (const std::exception &error) {
    setStatus(QStringLiteral("Cannot open %1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(error.what())), true);
    return false;
  }
  telemetry_state_ = QStringLiteral("loading");
  telemetry_rows_ = {};
  timeline_samples_ = {};
  timeline_gaps_ = {};
  gps_coverage_ = {};
  track_rows_ = {};
  gpmf_rows_ = {};
  gpmf_title_ = QStringLiteral("Loading metadata…");
  gpmf_sample_.reset();
  map_route_.clear();
  setMapPosition(QStringLiteral("loading"), QStringLiteral("Loading GPS telemetry…"), std::nullopt, std::nullopt);
  buildFileRows();
  emit fileChanged();
  emit audioChanged();
  emit telemetryChanged();
  emit timelineChanged();
  emit metadataChanged();
  emit gpmfChanged();
  emit positionChanged();
  emit mapRouteChanged();
  setStatus(QString::fromStdString(source_->layout.description));
  previous_statistics_ = engine_.statistics();
  previous_usage_ = sample_process_usage();
  diagnostics_clock_.start();
  diagnostics_timer_.start();
  startMetadataLoad(source);
  return true;
}

void PlayerController::buildFileRows() {
  file_rows_ = {};
  const QFileInfo info(input_path_);
  file_rows_.push_back(row(QStringLiteral("File"), info.fileName()));
  file_rows_.push_back(row(QStringLiteral("Size"), QStringLiteral("%L1 bytes").arg(info.size())));
  file_rows_.push_back(row(QStringLiteral("Access"), QStringLiteral("read-only")));
  file_rows_.push_back(row(QStringLiteral("Playback layout"), QString::fromStdString(source_->layout.description)));
  if (engine_.hasAudio()) {
    file_rows_.push_back(row(QStringLiteral("Audio"), audio_description_));
  } else {
    auto reason = audio_description_;
    if (!engine_.audioError().isEmpty()) reason += QStringLiteral(" — ") + engine_.audioError();
    file_rows_.push_back(row(QStringLiteral("Audio"), reason, QStringLiteral("unavailable")));
  }
  for (const auto &stream : source_->streams) {
    const auto selected = std::find(source_->layout.streams.begin(), source_->layout.streams.end(), stream.index) !=
                          source_->layout.streams.end();
    const auto rate = stream.average_frame_rate.denominator > 0
                          ? static_cast<double>(stream.average_frame_rate.numerator) /
                                stream.average_frame_rate.denominator
                          : 0.0;
    file_rows_.push_back(row(
        QStringLiteral("Video stream %1%2").arg(stream.index).arg(selected ? QStringLiteral(" (decoded)") : QString()),
        QStringLiteral("%1 %2×%3, %4 fps, time base %5/%6, %7 %8 range %9")
            .arg(QString::fromStdString(stream.codec))
            .arg(stream.width)
            .arg(stream.height)
            .arg(rate, 0, 'f', 3)
            .arg(stream.time_base.numerator)
            .arg(stream.time_base.denominator)
            .arg(QString::fromStdString(stream.color.pixel_format), QString::fromStdString(stream.color.space),
                 QString::fromStdString(stream.color.range))));
  }
}

void PlayerController::startMetadataLoad(const std::string &path) {
  const auto generation = load_generation_;
  loader_ = std::thread([this, path, generation] {
    QThread::currentThread()->setPriority(QThread::LowPriority);
    auto loaded = std::make_shared<LoadedMetadata>();
    try {
      loaded->inspection = gb::container::inspect_iso_bmff(path);
      for (std::size_t index = 0; index < loaded->inspection->tracks.size(); ++index) {
        if (loaded->inspection->tracks[index].codec == "gpmd") {
          loaded->gpmd_track = index;
          break;
        }
      }
    } catch (const std::exception &error) {
      loaded->inspection_error = QString::fromUtf8(error.what());
    }
    try {
      loaded->telemetry = gb::telemetry::extract(path);
    } catch (const std::exception &error) {
      loaded->telemetry_error = QString::fromUtf8(error.what());
    }
    QMetaObject::invokeMethod(this, [this, generation, loaded] { applyMetadata(generation, loaded); },
                              Qt::QueuedConnection);
  });
}

void PlayerController::applyMetadata(const std::uint64_t generation, std::shared_ptr<LoadedMetadata> loaded) {
  if (generation != load_generation_) return;
  metadata_ = std::move(loaded);
  metadata_loaded_ = true;

  track_rows_ = {};
  if (metadata_->inspection.has_value()) {
    const auto &inspection = *metadata_->inspection;
    if (inspection.identity.major_brand.has_value()) {
      file_rows_.push_back(row(QStringLiteral("Major brand"), QString::fromStdString(*inspection.identity.major_brand)));
    }
    for (const auto &track : inspection.tracks) {
      QString geometry;
      if (track.width > 0U) geometry = QStringLiteral("%1×%2").arg(track.width).arg(track.height);
      else if (track.sample_rate > 0U) {
        geometry = QStringLiteral("%1 Hz, %2 ch, %3 bit").arg(track.sample_rate).arg(track.channel_count).arg(track.bits_per_sample);
      }
      track_rows_.push_back(QVariantMap{
          {QStringLiteral("id"), track.id},
          {QStringLiteral("handler"), QString::fromStdString(track.handler_type)},
          {QStringLiteral("name"), QString::fromStdString(track.handler_name)},
          {QStringLiteral("codec"), QString::fromStdString(track.codec)},
          {QStringLiteral("timeBase"), rational(track.time_base.numerator, track.time_base.denominator)},
          {QStringLiteral("duration"), QString::number(track.duration_ticks)},
          {QStringLiteral("samples"), track.sample_count},
          {QStringLiteral("geometry"), geometry}});
    }
  } else {
    file_rows_.push_back(row(QStringLiteral("Container"), metadata_->inspection_error, QStringLiteral("unavailable")));
  }

  timeline_samples_ = {};
  timeline_gaps_ = {};
  gps_coverage_ = {};
  const auto total = static_cast<long double>(duration());
  if (metadata_->telemetry.has_value() && total > 0.0L) {
    const auto &telemetry = *metadata_->telemetry;
    const auto fraction = [&](const std::int64_t ticks) {
      return static_cast<double>(std::clamp(static_cast<long double>(ticks) * telemetry.time_base.numerator /
                                                telemetry.time_base.denominator / total,
                                            0.0L, 1.0L));
    };
    if (metadata_->inspection.has_value() && metadata_->gpmd_track.has_value()) {
      for (const auto &sample : metadata_->inspection->tracks[*metadata_->gpmd_track].samples) {
        timeline_samples_.push_back(fraction(sample.presentation_time));
      }
    }
    for (const auto &gap : telemetry.gaps) {
      timeline_gaps_.push_back(QVariantMap{{QStringLiteral("start"), fraction(gap.start_ticks)},
                                           {QStringLiteral("end"), fraction(gap.end_ticks)},
                                           {QStringLiteral("reason"), QString::fromStdString(gap.reason)}});
    }
    if (!telemetry.gps.empty()) {
      gps_coverage_ = {{QStringLiteral("start"), fraction(telemetry.gps.front().media_time_ticks)},
                       {QStringLiteral("end"), fraction(telemetry.gps.back().media_time_ticks)}};
    }
    telemetry_state_ = telemetry.gps.empty() ? QStringLiteral("none") : QStringLiteral("ready");
  } else {
    telemetry_state_ = QStringLiteral("none");
  }
  map_route_.clear();
  if (metadata_->telemetry.has_value()) {
    for (const auto &point : metadata_->telemetry->gps) {
      map_route_.push_back({{static_cast<double>(point.latitude), static_cast<double>(point.longitude)},
                            point.media_time_ticks,
                            point.validity == gb::telemetry::Validity::valid});
    }
  }
  emit metadataChanged();
  emit timelineChanged();
  emit mapRouteChanged();
  updateTelemetry();
  updateGpmfSample();
  emit metadataReady();
}

void PlayerController::onFrameChanged() {
  emit positionChanged();
  updateTelemetry();
  updateGpmfSample();
}

void PlayerController::updateTelemetry() {
  QVariantList rows;
  const auto video_base = engine_.timeBase();
  const auto position_ticks = engine_.positionTicks();
  rows.push_back(row(QStringLiteral("Media time"), formatTicks(position_ticks)));
  rows.push_back(row(QStringLiteral("Frame PTS"),
                     QStringLiteral("%1 @ %2/%3").arg(position_ticks).arg(video_base.numerator).arg(video_base.denominator)));
  if (!metadata_loaded_) {
    rows.push_back(row(QStringLiteral("GPS"), QStringLiteral("loading…"), QStringLiteral("unavailable")));
    telemetry_rows_ = rows;
    emit telemetryChanged();
    if (engine_.isOpen()) {
      setMapPosition(QStringLiteral("loading"), QStringLiteral("Loading GPS telemetry…"), std::nullopt, std::nullopt);
    }
    return;
  }
  const auto *telemetry = metadata_ && metadata_->telemetry ? &*metadata_->telemetry : nullptr;
  const auto telemetry_base = telemetry != nullptr ? mediaRational(telemetry->time_base) : std::nullopt;
  if (telemetry == nullptr || telemetry->gps.empty() || !telemetry_base.has_value()) {
    QString reason = QStringLiteral("no GPS records");
    if (telemetry == nullptr && metadata_) reason = metadata_->telemetry_error;
    rows.push_back(row(QStringLiteral("GPS"), unavailable() + QStringLiteral(" — ") + reason, QStringLiteral("unavailable")));
    telemetry_rows_ = rows;
    emit telemetryChanged();
    setMapPosition(QStringLiteral("none"), QStringLiteral("No GPS in this recording (%1)").arg(reason), std::nullopt,
                   std::nullopt);
    return;
  }
  const auto metadata_ticks = gb::media::rescale_ticks(position_ticks, video_base, *telemetry_base);
  const auto *point = gb::telemetry::nearest_point(*telemetry, metadata_ticks);
  const gb::telemetry::Gap *gap = nullptr;
  for (const auto &candidate : telemetry->gaps) {
    if (metadata_ticks >= candidate.start_ticks && metadata_ticks <= candidate.end_ticks) {
      gap = &candidate;
      break;
    }
  }
  const auto offset_ms = static_cast<double>(seconds(point->media_time_ticks - metadata_ticks, *telemetry_base) * 1000.0L);
  if (gap != nullptr) {
    rows.push_back(row(QStringLiteral("GPS gap"), QString::fromStdString(gap->reason), QStringLiteral("unavailable")));
  }
  const auto state = point->validity == gb::telemetry::Validity::valid ? QStringLiteral("ok")
                                                                        : QStringLiteral("invalid");
  rows.push_back(point->utc.has_value()
                     ? row(QStringLiteral("GPS UTC"), QString::fromStdString(*point->utc))
                     : row(QStringLiteral("GPS UTC"), unavailable(), QStringLiteral("unavailable")));
  rows.push_back(row(QStringLiteral("Fix"), fixName(point->fix), point->fix.has_value() ? state : QStringLiteral("unavailable")));
  rows.push_back(point->dop.has_value() ? row(QStringLiteral("DOP"), number(*point->dop, 2), state)
                                        : row(QStringLiteral("DOP"), unavailable(), QStringLiteral("unavailable")));
  rows.push_back(row(QStringLiteral("Latitude"), number(point->latitude, 7) + QStringLiteral("°"), state));
  rows.push_back(row(QStringLiteral("Longitude"), number(point->longitude, 7) + QStringLiteral("°"), state));
  rows.push_back(row(QStringLiteral("Altitude (WGS 84)"), number(point->altitude, 3) + QStringLiteral(" m"), state));
  rows.push_back(row(QStringLiteral("Speed 2D"), speedText(point->speed_2d), state));
  const auto azimuth = gb::telemetry::motion_azimuth_degrees(
      telemetry->gps, static_cast<std::size_t>(point - telemetry->gps.data()));
  rows.push_back(azimuth.has_value()
                     ? row(QStringLiteral("Speed 2D azimuth"), azimuthText(*azimuth), state)
                     : row(QStringLiteral("Speed 2D azimuth"),
                           unavailable() + QStringLiteral(" — no motion vector (stationary or no distinct neighbouring fix)"),
                           QStringLiteral("unavailable")));
  rows.push_back(row(QStringLiteral("Speed 3D"), speedText(point->speed_3d), state));
  telemetry_rows_ = rows;
  emit telemetryChanged();

  const auto record = static_cast<std::size_t>(point - telemetry->gps.data());
  const auto any_valid = std::any_of(map_route_.begin(), map_route_.end(), [](const auto &item) { return item.usable; });
  if (!any_valid) {
    setMapPosition(QStringLiteral("none"), QStringLiteral("The recording has GPS records but no valid fix"), std::nullopt,
                   std::nullopt);
  } else if (point->validity != gb::telemetry::Validity::valid) {
    setMapPosition(QStringLiteral("invalid"),
                   QStringLiteral("No valid GPS fix at this time (nearest record: %1)")
                       .arg(QString::fromStdString(gb::telemetry::validity_name(point->validity))),
                   record, std::nullopt);
  } else if (gap != nullptr) {
    setMapPosition(QStringLiteral("gap"),
                   QStringLiteral("GPS gap (%1). The ring marks the nearest recorded fix, %2 ms away.")
                       .arg(QString::fromStdString(gap->reason))
                       .arg(offset_ms, 0, 'f', 0),
                   record, std::nullopt);
  } else {
    setMapPosition(QStringLiteral("valid"), QString(), record,
                   azimuth.has_value() ? std::optional<double>(static_cast<double>(*azimuth)) : std::nullopt);
  }
}

void PlayerController::setMapPosition(const QString &state, const QString &message,
                                      const std::optional<std::size_t> record, const std::optional<double> course) {
  if (state == map_state_ && message == map_message_ && record == map_record_ && course == map_course_) return;
  map_state_ = state;
  map_message_ = message;
  map_record_ = record;
  map_course_ = course;
  emit mapPositionChanged();
}

QVariantList PlayerController::routeCandidates(const double latitude, const double longitude,
                                               const double tolerance_metres) const {
  QVariantList result;
  const auto *telemetry = metadata_ && metadata_->telemetry ? &*metadata_->telemetry : nullptr;
  const auto telemetry_base = telemetry != nullptr ? mediaRational(telemetry->time_base) : std::nullopt;
  if (!engine_.isOpen() || !telemetry_base.has_value()) return result;
  const auto candidates = gb::geo::route_candidates(map_route_, {latitude, longitude}, tolerance_metres);
  const auto now = engine_.positionTicks();
  const auto last = std::max<std::int64_t>(0, engine_.durationTicks() - 1);
  std::optional<std::size_t> nearest;
  std::int64_t nearest_distance = std::numeric_limits<std::int64_t>::max();
  std::vector<std::int64_t> ticks;
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    const auto video = std::clamp<std::int64_t>(
        gb::media::rescale_ticks(candidates[index].ticks, *telemetry_base, engine_.timeBase()), 0, last);
    ticks.push_back(video);
    const auto distance = video >= now ? video - now : now - video;
    if (distance < nearest_distance) {
      nearest_distance = distance;
      nearest = index;
    }
  }
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    result.push_back(QVariantMap{{QStringLiteral("ticks"), static_cast<double>(ticks[index])},
                                 {QStringLiteral("timeText"), formatTicks(ticks[index])},
                                 {QStringLiteral("distanceMetres"), candidates[index].distance_metres},
                                 {QStringLiteral("record"), static_cast<qulonglong>(candidates[index].index)},
                                 {QStringLiteral("nearestToNow"), nearest == index}});
  }
  return result;
}

void PlayerController::seekTicks(const double ticks) {
  if (!engine_.isOpen() || !std::isfinite(ticks)) return;
  const auto last = std::max<std::int64_t>(0, engine_.durationTicks() - 1);
  engine_.seek(std::clamp<std::int64_t>(std::llround(ticks), 0, last));
}

void PlayerController::updateGpmfSample() {
  if (!metadata_loaded_ || !metadata_ || !metadata_->inspection || !metadata_->gpmd_track) {
    if (metadata_loaded_ && gpmf_title_ != QStringLiteral("No gpmd metadata track")) {
      gpmf_title_ = QStringLiteral("No gpmd metadata track");
      gpmf_rows_ = {};
      emit gpmfChanged();
    }
    return;
  }
  const auto &track = metadata_->inspection->tracks[*metadata_->gpmd_track];
  const auto base = mediaRational(track.time_base);
  if (!base.has_value() || track.samples.empty()) return;
  const auto ticks = gb::media::rescale_ticks(engine_.positionTicks(), engine_.timeBase(), *base);
  const auto found = std::upper_bound(track.samples.begin(), track.samples.end(), ticks,
                                      [](const std::int64_t value, const auto &sample) {
                                        return value < sample.presentation_time;
                                      });
  const auto index = found == track.samples.begin()
                         ? std::size_t{0}
                         : static_cast<std::size_t>(std::distance(track.samples.begin(), found) - 1);
  if (gpmf_sample_ == index) return;
  gpmf_sample_ = index;
  const auto &sample = track.samples[index];
  gpmf_rows_ = {};
  try {
    const auto payload = gb::container::read_sample(metadata_->inspection->path, sample);
    flatten(gb::metadata::parse_gpmf(payload), 0, gpmf_rows_);
    gpmf_title_ = QStringLiteral("Track %1 sample %2 of %3 — ticks %4..%5 @ %6, %7 bytes at offset %8%9")
                      .arg(track.id)
                      .arg(index)
                      .arg(track.samples.size())
                      .arg(sample.presentation_time)
                      .arg(sample.presentation_time + sample.duration)
                      .arg(rational(track.time_base.numerator, track.time_base.denominator))
                      .arg(sample.size)
                      .arg(sample.offset)
                      .arg(static_cast<std::size_t>(gpmf_rows_.size()) >= kMaximumGpmfRows
                               ? QStringLiteral(" (first %1 entries)").arg(kMaximumGpmfRows)
                               : QString());
  } catch (const std::exception &error) {
    gpmf_title_ = QStringLiteral("Sample %1 cannot be parsed: %2").arg(index).arg(QString::fromUtf8(error.what()));
  }
  emit gpmfChanged();
}

void PlayerController::updateDiagnostics() {
  const auto statistics = engine_.statistics();
  const auto usage = sample_process_usage();
  const auto elapsed = std::max(1e-3, static_cast<double>(diagnostics_clock_.restart()) / 1000.0);
  const auto rate = [elapsed](const std::uint64_t now, const std::uint64_t before) {
    return static_cast<double>(now >= before ? now - before : 0U) / elapsed;
  };
  const auto cpu = (usage.cpu_seconds - previous_usage_.cpu_seconds) / elapsed * 100.0;
  const auto cores = std::max(1U, std::thread::hardware_concurrency());
  const auto mean = [](const double total, const std::uint64_t count) {
    return count == 0U ? 0.0 : total / static_cast<double>(count);
  };
  const auto &seeks = statistics.seek_render_ms.empty() ? statistics.seek_present_ms : statistics.seek_render_ms;

  QVariantList rows;
  rows.push_back(row(QStringLiteral("Displayed fps"),
                     QString::number(rate(statistics.rendered_frames, previous_statistics_.rendered_frames), 'f', 1)));
  rows.push_back(row(QStringLiteral("Presented fps"),
                     QString::number(rate(statistics.presented_frames, previous_statistics_.presented_frames), 'f', 1)));
  rows.push_back(row(QStringLiteral("Decoded sets/s"),
                     QString::number(rate(statistics.decoded_sets, previous_statistics_.decoded_sets), 'f', 1)));
  const auto dropped = statistics.late_drops + statistics.render_skips;
  rows.push_back(row(QStringLiteral("Dropped frames"),
                     QStringLiteral("%1  (late %2, not drawn %3)").arg(dropped).arg(statistics.late_drops).arg(statistics.render_skips),
                     dropped > 0U ? QStringLiteral("invalid") : QStringLiteral("ok")));
  rows.push_back(row(QStringLiteral("Decoder underruns"), QString::number(statistics.underruns),
                     statistics.underruns > 0U ? QStringLiteral("invalid") : QStringLiteral("ok")));
  rows.push_back(row(QStringLiteral("Queue"), QStringLiteral("%1 / %2 frame sets").arg(statistics.queue_depth).arg(statistics.queue_capacity)));
  rows.push_back(row(QStringLiteral("Unpaired frames"), QString::number(statistics.unpaired_frames),
                     statistics.unpaired_frames > 0U ? QStringLiteral("invalid") : QStringLiteral("ok")));
  rows.push_back(seeks.empty()
                     ? row(QStringLiteral("Seek latency"), QStringLiteral("no seeks yet"))
                     : row(QStringLiteral("Seek latency"),
                           QStringLiteral("last %1 ms, median %2 ms, max %3 ms (n=%4)")
                               .arg(seeks.back(), 0, 'f', 0)
                               .arg(percentile(seeks, 0.5), 0, 'f', 0)
                               .arg(*std::max_element(seeks.begin(), seeks.end()), 0, 'f', 0)
                               .arg(seeks.size())));
  rows.push_back(row(QStringLiteral("Texture upload"),
                     QStringLiteral("mean %1 ms, max %2 ms")
                         .arg(mean(statistics.upload_ms_total, statistics.rendered_frames), 0, 'f', 2)
                         .arg(statistics.upload_ms_max, 0, 'f', 2)));
  rows.push_back(statistics.gpu_samples == 0U
                     ? row(QStringLiteral("GPU time"), unavailable(), QStringLiteral("unavailable"))
                     : row(QStringLiteral("GPU time"),
                           QStringLiteral("mean %1 ms, max %2 ms (upload + draw)")
                               .arg(mean(statistics.gpu_ms_total, statistics.gpu_samples), 0, 'f', 2)
                               .arg(statistics.gpu_ms_max, 0, 'f', 2)));
  rows.push_back(row(QStringLiteral("Process CPU"),
                     QStringLiteral("%1% of one core (%2% of %3 threads)")
                         .arg(cpu, 0, 'f', 0)
                         .arg(cpu / cores, 0, 'f', 1)
                         .arg(cores)));
  rows.push_back(row(QStringLiteral("Memory"),
                     QStringLiteral("RSS %1 MiB, peak %2 MiB")
                         .arg(static_cast<double>(usage.resident_bytes) / 1048576.0, 0, 'f', 0)
                         .arg(static_cast<double>(usage.peak_resident_bytes) / 1048576.0, 0, 'f', 0)));
  if (engine_.hasAudio()) {
    const auto audio = engine_.audioStatistics();
    rows.push_back(row(QStringLiteral("Audio"), engine_.audioState()));
    rows.push_back(row(QStringLiteral("Audio output"),
                       QStringLiteral("%1, %2 Hz stereo float, %3-frame buffer, latency estimate %4 ms")
                           .arg(QString::fromStdString(audio.driver))
                           .arg(audio.sample_rate)
                           .arg(audio.buffer_frames)
                           .arg(audio.latency_ms, 0, 'f', 1)));
    rows.push_back(row(QStringLiteral("Audio underruns"), QString::number(audio.underruns),
                       audio.underruns > 0U ? QStringLiteral("invalid") : QStringLiteral("ok")));
    rows.push_back(row(QStringLiteral("Audio alignment"),
                       QStringLiteral("%1 starts, %2 frames trimmed, %3 frames of lead-in silence")
                           .arg(audio.starts)
                           .arg(audio.aligned_skip_frames)
                           .arg(audio.aligned_silence_frames)));
    if (!audio.error.empty()) {
      rows.push_back(row(QStringLiteral("Audio decode"), QString::fromStdString(audio.error), QStringLiteral("invalid")));
    }
  } else {
    rows.push_back(row(QStringLiteral("Audio"),
                       engine_.audioError().isEmpty() ? audio_description_ : engine_.audioError(),
                       QStringLiteral("unavailable")));
  }
  const auto tiles = tiles_->statistics();
  rows.push_back(row(QStringLiteral("Map tiles"),
                     QStringLiteral("%1 from cache (%2 revalidated), %3 requests: %4 downloaded (%5 KiB), "
                                    "%6 not modified, %7 failed; %8 in flight, %9 queued")
                         .arg(tiles.cache_fresh + tiles.cache_stale)
                         .arg(tiles.cache_stale)
                         .arg(tiles.network_requests)
                         .arg(tiles.downloaded)
                         .arg(tiles.downloaded_bytes / 1024U)
                         .arg(tiles.not_modified)
                         .arg(tiles.failures)
                         .arg(tiles.in_flight)
                         .arg(tiles.queued),
                     tiles.blocked || tiles.failures > 0U ? QStringLiteral("invalid") : QStringLiteral("ok")));
  rows.push_back(row(QStringLiteral("Map source"),
                     QStringLiteral("%1; %2; cache %3")
                         .arg(mapProviderName())
                         .arg(tiles_->networkEnabled() ? QStringLiteral("network on") : QStringLiteral("network off"))
                         .arg(tiles_->diskCacheEnabled() ? tiles_->cacheDirectory() : QStringLiteral("memory only"))));
  rows.push_back(row(QStringLiteral("Decoder threads"),
                     options_.decoder_threads == 0 ? QStringLiteral("automatic, per stream")
                                                   : QStringLiteral("%1 per stream").arg(options_.decoder_threads)));
  rows.push_back(row(QStringLiteral("Renderer"),
                     statistics.renderer.empty() ? QStringLiteral("not initialized") : QString::fromStdString(statistics.renderer)));
  diagnostics_rows_ = rows;
  previous_statistics_ = statistics;
  previous_usage_ = usage;
  emit diagnosticsChanged();
}

std::int64_t PlayerController::ticksForSeconds(const double value) const {
  if (!engine_.isOpen() || !std::isfinite(value)) return 0;
  const auto milliseconds = std::llround(std::max(0.0, value) * 1000.0);
  return gb::media::rescale_ticks(milliseconds, {1, 1000}, engine_.timeBase());
}

void PlayerController::onEndReached() {
  const auto next = sequence_index_ + 1;
  // Continue only if nothing moved playback off the last frame before this queued call ran.
  if (options_.continue_sequence && next < sequence_paths_.size() && !engine_.playing() && !engine_.seeking() &&
      engine_.positionTicks() + engine_.frameDurationTicks() >= engine_.durationTicks()) {
    // A failed open keeps the previous chapter and reports why.
    if (open(sequence_paths_[next])) play();
    return;
  }
  setStatus(QStringLiteral("End of media"));
}

void PlayerController::openSequenceIndex(const int index) {
  if (index < 0 || index >= sequence_paths_.size() || index == sequence_index_) return;
  const auto was_playing = engine_.playing();
  if (open(sequence_paths_[index]) && was_playing) play();
}

void PlayerController::togglePlay() {
  if (engine_.playing()) pause();
  else play();
}

void PlayerController::play() {
  if (!engine_.isOpen()) return;
  engine_.play();
  setStatus(QStringLiteral("Playing — %1").arg(QString::fromStdString(source_->layout.description)));
}

void PlayerController::pause() {
  engine_.pause();
  if (engine_.isOpen()) setStatus(QStringLiteral("Paused at %1").arg(positionText()));
}

void PlayerController::seekSeconds(const double value) { engine_.seek(ticksForSeconds(value)); }

void PlayerController::seekFraction(const double fraction) {
  if (!engine_.isOpen() || !std::isfinite(fraction)) return;
  const auto clamped = std::clamp(fraction, 0.0, 1.0);
  engine_.seek(static_cast<std::int64_t>(std::floor(clamped * static_cast<double>(engine_.durationTicks()))));
}

void PlayerController::beginScrub() { engine_.setScrubbing(true); }

void PlayerController::endScrub() { engine_.setScrubbing(false); }

void PlayerController::stepFrames(const int frames) {
  if (!engine_.isOpen()) return;
  engine_.pause();
  engine_.seek(engine_.positionTicks() + static_cast<std::int64_t>(frames) * engine_.frameDurationTicks());
}
