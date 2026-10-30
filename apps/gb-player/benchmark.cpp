#include "benchmark.hpp"

#include "player_controller.hpp"
#include "gb/media/playback.hpp"
#include "video_view.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <thread>

namespace {

double rounded(const double value, const int digits = 3) {
  const auto scale = std::pow(10.0, digits);
  return std::round(value * scale) / scale;
}

double mebibytes(const std::uint64_t bytes) { return rounded(static_cast<double>(bytes) / 1048576.0, 1); }

double mean(const double total, const std::uint64_t count) {
  return count == 0U ? 0.0 : total / static_cast<double>(count);
}

double percentile(std::vector<double> values, const double fraction) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size())) - 1.0);
  return values[std::min(index, values.size() - 1U)];
}

long double seconds(const std::int64_t ticks, const gb::media::Rational base) {
  return static_cast<long double>(ticks) * base.numerator / base.denominator;
}

} // namespace

BenchmarkRunner::BenchmarkRunner(PlayerController &controller, QQuickWindow *window,
                                 BenchmarkOptions options, QObject *parent)
    : QObject(parent), controller_(controller), engine_(controller.playbackEngine()), window_(window),
      options_(options), gpu_(VideoView::gpuAvailable()), random_(options.seed) {
  poll_.setInterval(20);
  timeout_.setSingleShot(true);
  connect(&timeout_, &QTimer::timeout, this, [this] { fail(QStringLiteral("benchmark step timed out")); });
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
  const auto frame_ready = gpu_ ? statistics.rendered_frames > 0U : statistics.presented_frames > 0U;
  if (!frame_ready || controller_.metadataLoading() || engine_.seeking()) return;
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
  QTimer::singleShot(static_cast<int>(std::lround(options_.play_seconds * 1000.0)), this,
                     &BenchmarkRunner::finishPlayback);
}

void BenchmarkRunner::finishPlayback() {
  const auto wall = static_cast<double>(wall_.nsecsElapsed()) / 1.0e9;
  const auto statistics = engine_.statistics();
  const auto usage = sample_process_usage();
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
      {QStringLiteral("expected_frames"), frame > 0.0 ? static_cast<qint64>(std::llround(media / frame)) + 1 : 0},
      {QStringLiteral("presented_frames"), static_cast<qint64>(statistics.presented_frames)},
      {QStringLiteral("rendered_frames"), static_cast<qint64>(statistics.rendered_frames)},
      {QStringLiteral("decoded_sets"), static_cast<qint64>(statistics.decoded_sets)},
      {QStringLiteral("displayed_fps"), rounded(static_cast<double>(shown) / wall, 2)},
      {QStringLiteral("decoded_fps"), rounded(static_cast<double>(statistics.decoded_sets) / wall, 2)},
      {QStringLiteral("dropped_frames"), static_cast<qint64>(statistics.late_drops + statistics.render_skips)},
      {QStringLiteral("late_drops"), static_cast<qint64>(statistics.late_drops)},
      {QStringLiteral("render_skips"), static_cast<qint64>(statistics.render_skips)},
      {QStringLiteral("underruns"), static_cast<qint64>(statistics.underruns)},
      {QStringLiteral("unpaired_frames"), static_cast<qint64>(statistics.unpaired_frames)},
      {QStringLiteral("upload_ms_mean"), rounded(mean(statistics.upload_ms_total, statistics.rendered_frames))},
      {QStringLiteral("upload_ms_max"), rounded(statistics.upload_ms_max)},
      {QStringLiteral("gpu_ms_mean"), statistics.gpu_samples == 0U ? QJsonValue() : QJsonValue(rounded(mean(statistics.gpu_ms_total, statistics.gpu_samples)))},
      {QStringLiteral("gpu_ms_max"), statistics.gpu_samples == 0U ? QJsonValue() : QJsonValue(rounded(statistics.gpu_ms_max))},
      {QStringLiteral("gpu_samples"), static_cast<qint64>(statistics.gpu_samples)},
      {QStringLiteral("cpu_percent_one_core"), rounded(cpu, 1)},
      {QStringLiteral("cpu_percent_all_threads"), rounded(cpu / std::max(1U, std::thread::hardware_concurrency()), 1)},
      {QStringLiteral("rss_mib_end"), mebibytes(usage.resident_bytes)}};
  seek_connection_ = gpu_ ? connect(&engine_, &PlaybackEngine::seekRendered, this, &BenchmarkRunner::seekCompleted)
                          : connect(&engine_, &PlaybackEngine::seekPresented, this, &BenchmarkRunner::seekCompleted);
  // Let the pause settle before measuring seeks.
  QTimer::singleShot(500, this, &BenchmarkRunner::nextSeek);
}

void BenchmarkRunner::nextSeek() {
  if (seek_index_ >= options_.seeks) {
    disconnect(seek_connection_);
    report();
    return;
  }
  const auto duration = engine_.durationTicks();
  std::uniform_int_distribution<std::int64_t> distribution(0, std::max<std::int64_t>(0, duration - 1));
  seek_target_ = distribution(random_);
  awaiting_seek_ = true;
  timeout_.start(15000);
  engine_.seek(seek_target_);
}

void BenchmarkRunner::seekCompleted(const double latency_ms) {
  if (!awaiting_seek_) return;
  awaiting_seek_ = false;
  timeout_.stop();
  const auto pts = engine_.positionTicks();
  const auto frame = engine_.frameDurationTicks();
  // Accurate seek: the displayed frame covers the target, or is the last frame.
  const auto exact = pts <= seek_target_ && (seek_target_ < pts + frame || pts + frame >= engine_.durationTicks());
  latencies_.push_back(latency_ms);
  seeks_.push_back(QJsonObject{{QStringLiteral("target_ticks"), static_cast<qint64>(seek_target_)},
                               {QStringLiteral("displayed_pts"), static_cast<qint64>(pts)},
                               {QStringLiteral("latency_ms"), rounded(latency_ms, 1)},
                               {QStringLiteral("exact"), exact}});
  ++seek_index_;
  QTimer::singleShot(250, this, &BenchmarkRunner::nextSeek);
}

void BenchmarkRunner::report() {
  const auto statistics = engine_.statistics();
  const auto usage = sample_process_usage();
  const auto streams = engine_.current().frames;
  QJsonArray decoded;
  if (streams) {
    for (const auto &picture : streams->pictures) decoded.push_back(picture.stream_index);
  }
  const auto base = engine_.timeBase();
  QJsonObject input{{QStringLiteral("path"), controller_.inputPath()},
                    {QStringLiteral("layout"), controller_.sourceLayout() == 1 ? QStringLiteral("max_dual_eac") : QStringLiteral("flat")},
                    {QStringLiteral("description"), controller_.layoutDescription()},
                    {QStringLiteral("decoded_streams"), decoded},
                    {QStringLiteral("width"), streams && !streams->pictures.empty() ? streams->pictures.front().width : 0},
                    {QStringLiteral("height"), streams && !streams->pictures.empty() ? streams->pictures.front().height : 0},
                    {QStringLiteral("time_base"), QStringLiteral("%1/%2").arg(base.numerator).arg(base.denominator)},
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
                    {QStringLiteral("measured_to"), gpu_ ? QStringLiteral("first frame drawn") : QStringLiteral("first frame presented")},
                    {QStringLiteral("exact"), static_cast<qint64>(exact)},
                    {QStringLiteral("samples"), seeks_}};
  if (!latencies_.empty()) {
    seeks.insert(QStringLiteral("latency_ms"),
                 QJsonObject{{QStringLiteral("min"), rounded(*std::min_element(latencies_.begin(), latencies_.end()), 1)},
                             {QStringLiteral("median"), rounded(percentile(latencies_, 0.5), 1)},
                             {QStringLiteral("p95"), rounded(percentile(latencies_, 0.95), 1)},
                             {QStringLiteral("max"), rounded(*std::max_element(latencies_.begin(), latencies_.end()), 1)},
                             {QStringLiteral("mean"), rounded(std::accumulate(latencies_.begin(), latencies_.end(), 0.0) / static_cast<double>(latencies_.size()), 1)}});
  }
  const QJsonObject root{
      {QStringLiteral("schema"), QStringLiteral("gb.player.benchmark/v1")},
      {QStringLiteral("version"), QCoreApplication::applicationVersion()},
      {QStringLiteral("input"), input},
      {QStringLiteral("environment"),
       QJsonObject{{QStringLiteral("renderer"), QString::fromStdString(statistics.renderer)},
                   {QStringLiteral("gpu_view"), gpu_},
                   {QStringLiteral("decoder_threads_per_stream"), controller_.options().decoder_threads},
                   {QStringLiteral("queue_capacity"), static_cast<int>(controller_.options().queue_capacity)},
                   {QStringLiteral("hardware_threads"), static_cast<int>(std::thread::hardware_concurrency())},
                   {QStringLiteral("view_mode"), options_.view_mode},
                   {QStringLiteral("window"), window}}},
      {QStringLiteral("playback"), playback_},
      {QStringLiteral("seeks"), seeks},
      {QStringLiteral("resources"), QJsonObject{{QStringLiteral("peak_rss_mib"), mebibytes(usage.peak_resident_bytes)},
                                                {QStringLiteral("rss_mib_end"), mebibytes(usage.resident_bytes)}}}};
  std::cout << QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString();
  emit finished(exact == static_cast<qint64>(latencies_.size()) ? 0 : 1);
}
