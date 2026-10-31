#include "benchmark.hpp"

#include "audio_output.hpp"
#include "gb/media/playback.hpp"
#include "map_view.hpp"
#include "player_controller.hpp"
#include "video_view.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <thread>

namespace {

double rounded(const double value, const int digits = 3) {
  const auto scale = std::pow(10.0, digits);
  return std::round(value * scale) / scale;
}

double mebibytes(const std::uint64_t bytes) {
  return rounded(static_cast<double>(bytes) / 1048576.0, 1);
}

double mean(const double total, const std::uint64_t count) {
  return count == 0U ? 0.0 : total / static_cast<double>(count);
}

double percentile(std::vector<double> values, const double fraction) {
  if (values.empty())
    return 0.0;
  std::sort(values.begin(), values.end());
  const auto index =
      static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size())) - 1.0);
  return values[std::min(index, values.size() - 1U)];
}

long double seconds(const std::int64_t ticks, const gb::media::Rational base) {
  return static_cast<long double>(ticks) * base.numerator / base.denominator;
}

} // namespace

BenchmarkRunner::BenchmarkRunner(PlayerController &controller, QQuickWindow *window,
                                 BenchmarkOptions options, QObject *parent)
    : QObject(parent), controller_(controller), engine_(controller.playbackEngine()),
      window_(window), options_(options), gpu_(VideoView::gpuAvailable()), random_(options.seed) {
  poll_.setInterval(20);
  timeout_.setSingleShot(true);
  connect(&timeout_, &QTimer::timeout, this,
          [this] { fail(QStringLiteral("benchmark step timed out")); });
  if (window_ != nullptr)
    map_ = window_->findChild<MapView *>(QStringLiteral("routeMap"));
  map_poll_.setInterval(97);
  connect(&map_poll_, &QTimer::timeout, this, &BenchmarkRunner::sampleMapDuringPlayback);
}

long long BenchmarkRunner::expectedMapRecord() const {
  const auto *telemetry = controller_.telemetry();
  if (telemetry == nullptr || telemetry->gps.empty() || telemetry->time_base.numerator == 0U)
    return -1;
  const gb::media::Rational base{static_cast<std::int32_t>(telemetry->time_base.numerator),
                                 static_cast<std::int32_t>(telemetry->time_base.denominator)};
  const auto ticks = gb::media::rescale_ticks(engine_.positionTicks(), engine_.timeBase(), base);
  const auto *point = gb::telemetry::nearest_point(*telemetry, ticks);
  if (point == nullptr || point->validity != gb::telemetry::Validity::valid)
    return -1;
  return static_cast<long long>(point - telemetry->gps.data());
}

double BenchmarkRunner::recordSeparationMs(const long long first, const long long second) const {
  const auto *telemetry = controller_.telemetry();
  if (first < 0 || second < 0 || telemetry == nullptr)
    return std::numeric_limits<double>::infinity();
  const auto &a = telemetry->gps[static_cast<std::size_t>(first)];
  const auto &b = telemetry->gps[static_cast<std::size_t>(second)];
  const auto ticks = std::abs(a.media_time_ticks - b.media_time_ticks);
  return static_cast<double>(ticks) * 1000.0 * static_cast<double>(telemetry->time_base.numerator) /
         static_cast<double>(telemetry->time_base.denominator);
}

// While playing the map may lag the frame by the time it takes to paint; more than two video
// frames counts as lagging.
void BenchmarkRunner::sampleMapDuringPlayback() {
  if (map_ == nullptr || !map_->hasRoute())
    return;
  const auto expected = expectedMapRecord();
  const auto painted = map_->paintedRecord();
  ++map_playback_samples_;
  if (painted == expected)
    return;
  const auto lag = recordSeparationMs(painted, expected);
  const auto frame_ms =
      static_cast<double>(seconds(engine_.frameDurationTicks(), engine_.timeBase())) * 1000.0;
  if (std::isfinite(lag))
    map_playback_max_lag_ms_ = std::max(map_playback_max_lag_ms_, lag);
  if (!(lag <= 2.0 * frame_ms))
    ++map_playback_lagging_;
}

// Paused: the painted marker must be exactly the expected record.
void BenchmarkRunner::checkMapStill(int &checks, int &mismatches) {
  if (map_ == nullptr || !map_->hasRoute())
    return;
  ++checks;
  if (map_->paintedRecord() != expectedMapRecord())
    ++mismatches;
}

void BenchmarkRunner::start() {
  connect(&poll_, &QTimer::timeout, this, &BenchmarkRunner::waitUntilReady);
  timeout_.start(60000);
  poll_.start();
}

void BenchmarkRunner::fail(const QString &message) {
  poll_.stop();
  timeout_.stop();
  std::cerr << "gb-player: benchmark failed: " << message.toStdString() << '\n';
  emit finished(1);
}

void BenchmarkRunner::waitUntilReady() {
  if (!controller_.status().isEmpty() && controller_.statusIsError()) {
    fail(controller_.status());
    return;
  }
  const auto statistics = engine_.statistics();
  const auto frame_ready =
      gpu_ ? statistics.rendered_frames > 0U : statistics.presented_frames > 0U;
  if (!frame_ready || controller_.metadataLoading() || engine_.seeking())
    return;
  poll_.stop();
  disconnect(&poll_, nullptr, this, nullptr);
  startPlayback();
}

void BenchmarkRunner::startPlayback() {
  engine_.resetStatistics();
  start_statistics_ = engine_.statistics();
  start_usage_ = sample_process_usage();
  start_ticks_ = engine_.positionTicks();
  wall_.start();
  controller_.play();
  timeout_.stop();
  map_poll_.start();
  QTimer::singleShot(static_cast<int>(std::lround(options_.play_seconds * 1000.0)), this,
                     &BenchmarkRunner::finishPlayback);
}

void BenchmarkRunner::finishPlayback() {
  const auto wall = static_cast<double>(wall_.nsecsElapsed()) / 1.0e9;
  const auto statistics = engine_.statistics();
  const auto usage = sample_process_usage();
  const auto audio = engine_.audioStatistics();
  map_poll_.stop();
  controller_.pause();
  const auto base = engine_.timeBase();
  const auto media = static_cast<double>(seconds(engine_.positionTicks() - start_ticks_, base));
  const auto frame = static_cast<double>(seconds(engine_.frameDurationTicks(), base));
  const auto cpu = (usage.cpu_seconds - start_usage_.cpu_seconds) / wall * 100.0;
  const auto shown = gpu_ ? statistics.rendered_frames : statistics.presented_frames;
  playback_ = QJsonObject{
      {QStringLiteral("start_ticks"), static_cast<qint64>(start_ticks_)},
      {QStringLiteral("requested_seconds"), options_.play_seconds},
      {QStringLiteral("wall_seconds"), rounded(wall)},
      {QStringLiteral("media_seconds"), rounded(media)},
      {QStringLiteral("expected_frames"),
       frame > 0.0 ? static_cast<qint64>(std::llround(media / frame)) + 1 : 0},
      {QStringLiteral("presented_frames"), static_cast<qint64>(statistics.presented_frames)},
      {QStringLiteral("rendered_frames"), static_cast<qint64>(statistics.rendered_frames)},
      {QStringLiteral("decoded_sets"), static_cast<qint64>(statistics.decoded_sets)},
      {QStringLiteral("displayed_fps"), rounded(static_cast<double>(shown) / wall, 2)},
      {QStringLiteral("decoded_fps"),
       rounded(static_cast<double>(statistics.decoded_sets) / wall, 2)},
      {QStringLiteral("dropped_frames"),
       static_cast<qint64>(statistics.late_drops + statistics.render_skips)},
      {QStringLiteral("late_drops"), static_cast<qint64>(statistics.late_drops)},
      {QStringLiteral("render_skips"), static_cast<qint64>(statistics.render_skips)},
      {QStringLiteral("underruns"), static_cast<qint64>(statistics.underruns)},
      {QStringLiteral("unpaired_frames"), static_cast<qint64>(statistics.unpaired_frames)},
      {QStringLiteral("upload_ms_mean"),
       rounded(mean(statistics.upload_ms_total, statistics.rendered_frames))},
      {QStringLiteral("upload_ms_max"), rounded(statistics.upload_ms_max)},
      {QStringLiteral("gpu_ms_mean"),
       statistics.gpu_samples == 0U
           ? QJsonValue()
           : QJsonValue(rounded(mean(statistics.gpu_ms_total, statistics.gpu_samples)))},
      {QStringLiteral("gpu_ms_max"),
       statistics.gpu_samples == 0U ? QJsonValue() : QJsonValue(rounded(statistics.gpu_ms_max))},
      {QStringLiteral("gpu_samples"), static_cast<qint64>(statistics.gpu_samples)},
      {QStringLiteral("cpu_percent_one_core"), rounded(cpu, 1)},
      {QStringLiteral("cpu_percent_all_threads"),
       rounded(cpu / std::max(1U, std::thread::hardware_concurrency()), 1)},
      {QStringLiteral("rss_mib_end"), mebibytes(usage.resident_bytes)}};
  if (engine_.hasAudio()) {
    playback_.insert(QStringLiteral("audio_seconds"),
                     audio.sample_rate > 0
                         ? rounded(static_cast<double>(audio.audible_frames) / audio.sample_rate)
                         : 0.0);
    playback_.insert(QStringLiteral("audio_underruns"), static_cast<qint64>(audio.underruns));
  }
  // Let the pause settle before the audio gating check.
  QTimer::singleShot(500, this, &BenchmarkRunner::checkAudioGating);
}

std::uint64_t BenchmarkRunner::audibleFrames() const {
  return engine_.audioStatistics().audible_frames;
}

void BenchmarkRunner::runSteps() {
  if (step_ >= steps_.size())
    return;
  const auto [delay, action] = steps_[step_++];
  QTimer::singleShot(delay, this, [this, action = action] {
    action();
    runSteps();
  });
}

void BenchmarkRunner::checkAudioGating() {
  checkMapStill(map_pause_checks_, map_pause_mismatches_);
  if (!engine_.hasAudio()) {
    audio_ = {{QStringLiteral("available"), false},
              {QStringLiteral("reason"), engine_.audioError().isEmpty()
                                             ? controller_.audioDescription()
                                             : engine_.audioError()}};
    startStall();
    return;
  }
  const auto statistics = engine_.audioStatistics();
  audio_ = {{QStringLiteral("available"), true},
            {QStringLiteral("stream"), controller_.audioDescription()},
            {QStringLiteral("driver"), QString::fromStdString(statistics.driver)},
            {QStringLiteral("sample_rate"), statistics.sample_rate},
            {QStringLiteral("buffer_frames"), statistics.buffer_frames},
            {QStringLiteral("latency_estimate_ms"), rounded(statistics.latency_ms, 1)}};
  const auto duration = engine_.durationTicks();
  const auto random_fraction = [this] {
    return std::uniform_real_distribution<double>(0.0, 0.9)(random_);
  };
  auto mark = std::make_shared<std::uint64_t>();
  const auto check = [this](const QString &name, const bool ok) {
    audio_.insert(name, ok);
    audio_ok_ = audio_ok_ && ok;
  };
  const auto silent = [this, mark](const QString &name) {
    const auto heard = audibleFrames() - *mark;
    audio_.insert(name, static_cast<qint64>(heard));
    audio_ok_ = audio_ok_ && heard == 0U;
  };
  steps_.clear();
  step_ = 0;
  // Stable playback: audio must start.
  steps_.emplace_back(0, [this, mark] {
    *mark = audibleFrames();
    controller_.play();
  });
  steps_.emplace_back(1500, [mark, check, this] {
    check(QStringLiteral("heard_during_playback"), audibleFrames() > *mark);
  });
  // Timeline drag while playing: 40 seeks 50 ms apart. The 100 ms margin covers the 5 ms fade.
  steps_.emplace_back(0, [this] { controller_.beginScrub(); });
  steps_.emplace_back(100, [this, mark] { *mark = audibleFrames(); });
  for (int index = 0; index < 40; ++index) {
    steps_.emplace_back(50,
                        [this, random_fraction] { controller_.seekFraction(random_fraction()); });
  }
  steps_.emplace_back(400, [silent] { silent(QStringLiteral("scrub_audible_frames")); });
  steps_.emplace_back(0, [this] { controller_.endScrub(); });
  steps_.emplace_back(1500, [this, mark, check] {
    check(QStringLiteral("resumed_after_scrub"), audibleFrames() > *mark);
    *mark = audibleFrames();
  });
  // Held arrow key while playing: seeks 150 ms apart, inside the settle hold.
  steps_.emplace_back(0, [this, duration, random_fraction] {
    engine_.seek(static_cast<std::int64_t>(random_fraction() * static_cast<double>(duration)));
  });
  steps_.emplace_back(100, [this, mark] { *mark = audibleFrames(); });
  for (int index = 0; index < 15; ++index) {
    steps_.emplace_back(150, [this, duration, random_fraction] {
      engine_.seek(static_cast<std::int64_t>(random_fraction() * static_cast<double>(duration)));
    });
  }
  steps_.emplace_back(0, [silent] { silent(QStringLiteral("seek_burst_audible_frames")); });
  steps_.emplace_back(1500, [this, mark, check] {
    check(QStringLiteral("resumed_after_seeks"), audibleFrames() > *mark);
    controller_.pause();
    *mark = audibleFrames();
  });
  steps_.emplace_back(500, [this, silent] {
    silent(QStringLiteral("paused_audible_frames"));
    const auto statistics = engine_.audioStatistics();
    audio_.insert(QStringLiteral("starts"), static_cast<qint64>(statistics.starts));
    audio_.insert(QStringLiteral("underruns"), static_cast<qint64>(statistics.underruns));
    audio_.insert(QStringLiteral("gating_ok"), audio_ok_);
    startStall();
  });
  runSteps();
}

// GUI-thread stall, the mechanism of docs/incidents/2026-10-31-video-lag-after-screen-lock.md: on
// GNOME/X11 a locked screen throttles buffer swaps to about one a second, and each swap blocks the
// GUI thread, so neither advanceFrame() nor the engine timer runs. Blocking the GUI thread here
// stops both presentation paths in the same way, whether or not the engine is frame-driven. While
// it is blocked, only the decode worker can keep the queue current: it must go on decoding and
// dropping stale sets in step with the clock. Without that, it waits behind a full queue and
// decodes at most a queue's worth of sets, and the video falls behind the audio.
void BenchmarkRunner::startStall() {
  if (options_.stall_seconds <= 0.0) {
    startSeeks();
    return;
  }
  engine_.seek(0);
  timeout_.start(15000);
  connect(&poll_, &QTimer::timeout, this, [this] {
    if (engine_.seeking())
      return;
    poll_.stop();
    disconnect(&poll_, nullptr, this, nullptr);
    timeout_.stop();
    controller_.play();
    // Past the seek settle hold, so the clock is in steady playback when the stall begins.
    QTimer::singleShot(1000, this, &BenchmarkRunner::stallGuiThread);
  });
  poll_.start();
}

void BenchmarkRunner::stallGuiThread() {
  const auto before = engine_.statistics();
  std::this_thread::sleep_for(std::chrono::duration<double>(options_.stall_seconds));
  const auto after = engine_.statistics();
  const auto frame = static_cast<double>(seconds(engine_.frameDurationTicks(), engine_.timeBase()));
  const auto due =
      frame > 0.0 ? static_cast<std::uint64_t>(std::floor(options_.stall_seconds / frame)) : 0U;
  const auto decoded = after.decoded_sets - before.decoded_sets;
  const auto dropped = after.late_drops - before.late_drops;
  // 80% leaves room for a slow machine. Without the worker keeping pace, decoded is at most the
  // queue capacity.
  const auto kept_pace = due > 0U && decoded * 5U >= due * 4U;
  stall_ok_ = engine_.playing() && kept_pace;
  stall_ =
      QJsonObject{{QStringLiteral("seconds"), options_.stall_seconds},
                  {QStringLiteral("presentation"),
                   gpu_ ? QStringLiteral("frame-driven") : QStringLiteral("timer")},
                  {QStringLiteral("sets_due"), static_cast<qint64>(due)},
                  {QStringLiteral("decoded_sets"), static_cast<qint64>(decoded)},
                  {QStringLiteral("worker_late_drops"), static_cast<qint64>(dropped)},
                  {QStringLiteral("queue_capacity"), static_cast<qint64>(after.queue_capacity)},
                  {QStringLiteral("stall_ok"), stall_ok_}};
  controller_.pause();
  startSeeks();
}

void BenchmarkRunner::startSeeks() {
  seek_connection_ =
      gpu_ ? connect(&engine_, &PlaybackEngine::seekRendered, this, &BenchmarkRunner::seekCompleted)
           : connect(&engine_, &PlaybackEngine::seekPresented, this,
                     &BenchmarkRunner::seekCompleted);
  nextSeek();
}

void BenchmarkRunner::nextSeek() {
  if (seek_index_ >= options_.seeks) {
    disconnect(seek_connection_);
    report();
    return;
  }
  const auto duration = engine_.durationTicks();
  std::uniform_int_distribution<std::int64_t> distribution(0,
                                                           std::max<std::int64_t>(0, duration - 1));
  seek_target_ = distribution(random_);
  awaiting_seek_ = true;
  timeout_.start(15000);
  engine_.seek(seek_target_);
}

void BenchmarkRunner::seekCompleted(const double latency_ms) {
  if (!awaiting_seek_)
    return;
  awaiting_seek_ = false;
  timeout_.stop();
  const auto pts = engine_.positionTicks();
  const auto frame = engine_.frameDurationTicks();
  // Accurate seek: the displayed frame covers the target, or is the last frame.
  const auto exact =
      pts <= seek_target_ && (seek_target_ < pts + frame || pts + frame >= engine_.durationTicks());
  latencies_.push_back(latency_ms);
  seeks_.push_back(QJsonObject{{QStringLiteral("target_ticks"), static_cast<qint64>(seek_target_)},
                               {QStringLiteral("displayed_pts"), static_cast<qint64>(pts)},
                               {QStringLiteral("latency_ms"), rounded(latency_ms, 1)},
                               {QStringLiteral("exact"), exact}});
  ++seek_index_;
  QTimer::singleShot(150, this, [this] { checkMapStill(map_seek_checks_, map_seek_mismatches_); });
  QTimer::singleShot(250, this, &BenchmarkRunner::nextSeek);
}

void BenchmarkRunner::report() {
  const auto statistics = engine_.statistics();
  const auto usage = sample_process_usage();
  const auto streams = engine_.current().frames;
  QJsonArray decoded;
  if (streams) {
    for (const auto &picture : streams->pictures)
      decoded.push_back(picture.stream_index);
  }
  const auto base = engine_.timeBase();
  QJsonObject input{
      {QStringLiteral("path"), controller_.inputPath()},
      {QStringLiteral("layout"),
       controller_.sourceLayout() == 1 ? QStringLiteral("max_dual_eac") : QStringLiteral("flat")},
      {QStringLiteral("description"), controller_.layoutDescription()},
      {QStringLiteral("decoded_streams"), decoded},
      {QStringLiteral("width"),
       streams && !streams->pictures.empty() ? streams->pictures.front().width : 0},
      {QStringLiteral("height"),
       streams && !streams->pictures.empty() ? streams->pictures.front().height : 0},
      {QStringLiteral("time_base"),
       QStringLiteral("%1/%2").arg(base.numerator).arg(base.denominator)},
      {QStringLiteral("duration_ticks"), static_cast<qint64>(engine_.durationTicks())}};
  QJsonObject window;
  if (window_ != nullptr) {
    window = {{QStringLiteral("width"), window_->width()},
              {QStringLiteral("height"), window_->height()},
              {QStringLiteral("device_pixel_ratio"), window_->devicePixelRatio()}};
  }
  const auto exact = std::count_if(seeks_.begin(), seeks_.end(), [](const QJsonValue &value) {
    return value.toObject().value(QStringLiteral("exact")).toBool();
  });
  QJsonObject seeks{{QStringLiteral("count"), static_cast<int>(latencies_.size())},
                    {QStringLiteral("seed"), static_cast<qint64>(options_.seed)},
                    {QStringLiteral("mode"), QStringLiteral("paused, accurate")},
                    {QStringLiteral("measured_to"), gpu_ ? QStringLiteral("first frame drawn")
                                                         : QStringLiteral("first frame presented")},
                    {QStringLiteral("exact"), static_cast<qint64>(exact)},
                    {QStringLiteral("samples"), seeks_}};
  if (!latencies_.empty()) {
    seeks.insert(QStringLiteral("latency_ms"),
                 QJsonObject{{QStringLiteral("min"),
                              rounded(*std::min_element(latencies_.begin(), latencies_.end()), 1)},
                             {QStringLiteral("median"), rounded(percentile(latencies_, 0.5), 1)},
                             {QStringLiteral("p95"), rounded(percentile(latencies_, 0.95), 1)},
                             {QStringLiteral("max"),
                              rounded(*std::max_element(latencies_.begin(), latencies_.end()), 1)},
                             {QStringLiteral("mean"),
                              rounded(std::accumulate(latencies_.begin(), latencies_.end(), 0.0) /
                                          static_cast<double>(latencies_.size()),
                                      1)}});
  }
  QJsonObject map{{QStringLiteral("route_shown"), map_ != nullptr && map_->hasRoute()},
                  {QStringLiteral("state_at_end"), controller_.mapState()}};
  bool map_ok = true;
  if (map_ != nullptr && map_->hasRoute()) {
    map_ok = map_seek_mismatches_ == 0 && map_pause_mismatches_ == 0;
    map.insert(QStringLiteral("checked_against"),
               QStringLiteral("nearest GPS record to the displayed PTS"));
    map.insert(QStringLiteral("playback_samples"), map_playback_samples_);
    map.insert(QStringLiteral("playback_lagging_samples"), map_playback_lagging_);
    map.insert(QStringLiteral("playback_max_lag_ms"), rounded(map_playback_max_lag_ms_, 1));
    map.insert(QStringLiteral("pause_checks"), map_pause_checks_);
    map.insert(QStringLiteral("pause_mismatches"), map_pause_mismatches_);
    map.insert(QStringLiteral("seek_checks"), map_seek_checks_);
    map.insert(QStringLiteral("seek_mismatches"), map_seek_mismatches_);
    map.insert(QStringLiteral("paints"), static_cast<qint64>(map_->paintCount()));
    map.insert(QStringLiteral("aligned"), map_ok);
  }
  const auto tiles = controller_.tileFetcher()->statistics();
  map.insert(QStringLiteral("tiles"),
             QJsonObject{{QStringLiteral("network"), controller_.tileFetcher()->networkEnabled()},
                         {QStringLiteral("cache_fresh"), static_cast<qint64>(tiles.cache_fresh)},
                         {QStringLiteral("cache_stale"), static_cast<qint64>(tiles.cache_stale)},
                         {QStringLiteral("requests"), static_cast<qint64>(tiles.network_requests)},
                         {QStringLiteral("downloaded"), static_cast<qint64>(tiles.downloaded)},
                         {QStringLiteral("not_modified"), static_cast<qint64>(tiles.not_modified)},
                         {QStringLiteral("failures"), static_cast<qint64>(tiles.failures)}});
  const QJsonObject root{
      {QStringLiteral("schema"), QStringLiteral("gb.player.benchmark/v1")},
      {QStringLiteral("version"), QCoreApplication::applicationVersion()},
      {QStringLiteral("input"), input},
      {QStringLiteral("environment"),
       QJsonObject{
           {QStringLiteral("renderer"), QString::fromStdString(statistics.renderer)},
           {QStringLiteral("gpu_view"), gpu_},
           {QStringLiteral("decoder_threads_per_stream"), controller_.options().decoder_threads},
           {QStringLiteral("queue_capacity"),
            static_cast<int>(controller_.options().queue_capacity)},
           {QStringLiteral("hardware_threads"),
            static_cast<int>(std::thread::hardware_concurrency())},
           {QStringLiteral("view_mode"), options_.view_mode},
           {QStringLiteral("window"), window}}},
      {QStringLiteral("playback"), playback_},
      {QStringLiteral("audio"), audio_},
      {QStringLiteral("stall"), stall_.isEmpty() ? QJsonValue() : QJsonValue(stall_)},
      {QStringLiteral("seeks"), seeks},
      {QStringLiteral("map"), map},
      {QStringLiteral("resources"),
       QJsonObject{{QStringLiteral("peak_rss_mib"), mebibytes(usage.peak_resident_bytes)},
                   {QStringLiteral("rss_mib_end"), mebibytes(usage.resident_bytes)}}}};
  std::cout << QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString();
  emit finished(
      exact == static_cast<qint64>(latencies_.size()) && audio_ok_ && stall_ok_ && map_ok ? 0 : 1);
}
