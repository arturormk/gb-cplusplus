#include "playback_engine.hpp"

#include "audio_output.hpp"
#include "gb/media/playback.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace {

using SteadyClock = std::chrono::steady_clock;

// Audio stays silent for this long after a seek is shown or a scrub ends, so held arrow keys and
// repeated clicks never produce fragments of sound between seeks.
constexpr auto kAudioSettle = std::chrono::milliseconds(250);

// When frame-driven and no scene-graph frame has arrived for this long, the window is not being
// drawn (hidden, minimized, screen locked) and the timer presents instead. Otherwise the decoder
// stalls on a full queue while audio runs on, and video has to race to catch up when drawing resumes.
constexpr auto kFrameStall = std::chrono::milliseconds(100);

double millisecondsSince(const std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

PlaybackEngine::PlaybackEngine(QObject *parent) : QObject(parent) {
  timer_.setTimerType(Qt::PreciseTimer);
  timer_.setInterval(4);
  connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
}

PlaybackEngine::~PlaybackEngine() { stopWorker(); }

void PlaybackEngine::open(const std::string &path, const gb::media::PlaybackLayout &layout,
                          const int decoder_threads, const std::size_t queue_capacity,
                          const std::int64_t start_ticks, const std::optional<int> audio_stream,
                          const std::string &application_name) {
  close();
  reader_ = std::make_unique<gb::media::SynchronizedReader>(path, layout.streams, decoder_threads);
  time_base_numerator_ = reader_->time_base().numerator;
  time_base_denominator_ = reader_->time_base().denominator;
  duration_ticks_ = reader_->duration_ticks();
  audio_error_.clear();
  if (audio_stream.has_value()) {
    try {
      audio_ = std::make_unique<AudioOutput>(path, *audio_stream, application_name);
      audio_->setMuted(audio_muted_);
    } catch (const std::exception &error) {
      audio_error_ = QString::fromUtf8(error.what());
    }
  }
  {
    const std::lock_guard lock(mutex_);
    capacity_ = std::max<std::size_t>(1U, queue_capacity);
    stop_ = false;
    end_of_stream_ = false;
    worker_error_.reset();
    generation_ = 0;
  }
  resetStatistics();
  thread_ = std::thread(&PlaybackEngine::worker, this);
  timer_.start();
  seek(start_ticks);
}

void PlaybackEngine::openStill(std::shared_ptr<const gb::media::FrameSet> frames) {
  close();
  still_ = true;
  time_base_numerator_ = frames->time_base.numerator;
  time_base_denominator_ = frames->time_base.denominator;
  duration_ticks_ = 0;
  resetStatistics();
  {
    const std::lock_guard lock(statistics_mutex_);
    statistics_.presented_frames = 1;
  }
  {
    const std::lock_guard lock(current_mutex_);
    current_ = {std::move(frames), ++serial_, generation_};
  }
  emit frameChanged();
}

void PlaybackEngine::stopWorker() {
  {
    const std::lock_guard lock(mutex_);
    stop_ = true;
  }
  condition_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void PlaybackEngine::close() {
  timer_.stop();
  disengageAudio();
  audio_.reset();
  audio_error_.clear();
  scrubbing_ = false;
  stopWorker();
  reader_.reset();
  still_ = false;
  {
    const std::lock_guard lock(mutex_);
    queue_.clear();
    seek_request_.reset();
    awaiting_seek_ = false;
  }
  {
    const std::lock_guard lock(current_mutex_);
    current_ = {};
  }
  const auto was_playing = std::exchange(playing_, false);
  duration_ticks_ = 0;
  if (was_playing) emit playingChanged();
  emit frameChanged();
  emit audioChanged();
}

bool PlaybackEngine::isOpen() const noexcept { return reader_ != nullptr || still_; }
bool PlaybackEngine::playing() const noexcept { return playing_; }
std::int64_t PlaybackEngine::durationTicks() const noexcept { return duration_ticks_; }
gb::media::Rational PlaybackEngine::timeBase() const noexcept {
  return {time_base_numerator_, time_base_denominator_};
}

bool PlaybackEngine::seeking() const {
  const std::lock_guard lock(mutex_);
  return awaiting_seek_;
}

PresentedFrame PlaybackEngine::current() const {
  const std::lock_guard lock(current_mutex_);
  return current_;
}

std::int64_t PlaybackEngine::positionTicks() const {
  const std::lock_guard lock(current_mutex_);
  return current_.frames ? current_.frames->pts : 0;
}

std::int64_t PlaybackEngine::frameDurationTicks() const {
  const std::lock_guard lock(current_mutex_);
  return current_.frames && current_.frames->duration > 0 ? current_.frames->duration : 1;
}

void PlaybackEngine::play() {
  if (reader_ == nullptr || playing_) return;
  bool restart{};
  {
    const std::lock_guard lock(mutex_);
    const auto position = positionTicks();
    restart = end_of_stream_ && queue_.empty() && !awaiting_seek_ &&
              position + frameDurationTicks() >= duration_ticks_;
  }
  if (restart) seek(0);
  playing_ = true;
  rebaseClock(clock_base_ticks_, SteadyClock::now());
  emit playingChanged();
}

void PlaybackEngine::pause() {
  if (!playing_) return;
  disengageAudio();
  playing_ = false;
  clock_base_ticks_ = positionTicks();
  emit playingChanged();
}

void PlaybackEngine::setScrubbing(const bool scrubbing) {
  if (scrubbing == scrubbing_ || reader_ == nullptr) return;
  const auto now = SteadyClock::now();
  if (scrubbing) {
    disengageAudio();
    clock_base_ticks_ = clockTicks();
    scrubbing_ = true;
  } else {
    scrubbing_ = false;
    rebaseClock(clock_base_ticks_, now);
    audio_hold_until_ = now + kAudioSettle;
  }
  emit audioChanged();
}

void PlaybackEngine::setAudioMuted(const bool muted) {
  audio_muted_ = muted;
  if (audio_) audio_->setMuted(muted);
  emit audioChanged();
}

void PlaybackEngine::seek(std::int64_t ticks) {
  if (reader_ == nullptr) return;
  ticks = std::max<std::int64_t>(0, ticks);
  if (duration_ticks_ > 0) ticks = std::min(ticks, duration_ticks_ - 1);
  disengageAudio();
  {
    const std::lock_guard lock(mutex_);
    seek_request_ = ticks;
    ++generation_;
    queue_.clear();
    end_of_stream_ = false;
    worker_error_.reset();
    awaiting_seek_ = true;
  }
  {
    const std::lock_guard lock(statistics_mutex_);
    seek_started_ = std::chrono::steady_clock::now();
    seek_render_pending_ = false;
  }
  condition_.notify_all();
}

std::int64_t PlaybackEngine::clockTicks() const {
  if (!playing_ || scrubbing_) return clock_base_ticks_;
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - clock_origin_);
  return clock_base_ticks_ + gb::media::rescale_ticks(elapsed.count(), {1, 1000000000}, timeBase());
}

void PlaybackEngine::rebaseClock(const std::int64_t ticks, const SteadyClock::time_point at) {
  clock_base_ticks_ = ticks;
  clock_origin_ = at;
}

// While audio is heard, its position drives the clock. Rebasing the wall clock on every tick
// means it continues seamlessly from the last audio position whenever audio stops.
void PlaybackEngine::syncClockToAudio() {
  if (!audio_engaged_ || !playing_ || scrubbing_) return;
  const auto now = SteadyClock::now();
  if (const auto sample = audio_->position(now)) {
    rebaseClock(gb::media::rescale_ticks(*sample, {1, audio_->sampleRate()}, timeBase()), now);
  }
}

void PlaybackEngine::updateAudio(const bool seek_pending) {
  if (!audio_) return;
  const auto now = SteadyClock::now();
  const auto stable = playing_ && !scrubbing_ && !seek_pending && now >= audio_hold_until_;
  if (stable && !audio_engaged_) {
    // Anchor on the current video clock so audio joins without moving the picture.
    const auto sample = gb::media::rescale_ticks(clock_base_ticks_, timeBase(), {1, audio_->sampleRate()});
    audio_->start(sample, clock_origin_);
    audio_engaged_ = true;
    emit audioChanged();
  } else if (!stable && audio_engaged_) {
    disengageAudio();
  }
  if (!audio_engaged_) audio_->pauseDeviceIfIdle();
}

void PlaybackEngine::disengageAudio() {
  if (!audio_engaged_) return;
  syncClockToAudio();
  audio_->stop();
  audio_engaged_ = false;
  emit audioChanged();
}

QString PlaybackEngine::audioState() const {
  if (!audio_) return audio_error_.isEmpty() ? QStringLiteral("none") : QStringLiteral("unavailable");
  if (audio_engaged_) {
    switch (audio_->state()) {
    case AudioOutput::State::playing:
      return audio_muted_ ? QStringLiteral("muted (audio clock master)") : QStringLiteral("playing (audio clock master)");
    case AudioOutput::State::finished: return QStringLiteral("audio stream ended");
    default: return QStringLiteral("aligning to the video clock");
    }
  }
  if (!playing_) return QStringLiteral("silent: paused");
  if (scrubbing_) return QStringLiteral("silent: scrubbing");
  if (seeking()) return QStringLiteral("silent: seeking");
  return QStringLiteral("silent: settling after seek");
}

AudioStatistics PlaybackEngine::audioStatistics() const { return audio_ ? audio_->statistics() : AudioStatistics{}; }

void PlaybackEngine::worker() {
  std::unique_lock lock(mutex_);
  while (true) {
    condition_.wait(lock, [this] {
      return stop_ || seek_request_.has_value() ||
             (!end_of_stream_ && !worker_error_.has_value() && queue_.size() < capacity_);
    });
    if (stop_) return;
    const auto target = std::exchange(seek_request_, std::nullopt);
    const auto generation = generation_;
    lock.unlock();

    const auto started = std::chrono::steady_clock::now();
    std::optional<gb::media::FrameSet> set;
    std::optional<QString> error;
    try {
      if (target.has_value()) reader_->seek(*target);
      set = reader_->next();
    } catch (const std::exception &failure) {
      error = QString::fromUtf8(failure.what());
    }
    const auto busy = millisecondsSince(started);
    const auto unpaired = reader_->unpaired_frames();

    lock.lock();
    {
      const std::lock_guard statistics_lock(statistics_mutex_);
      statistics_.decode_busy_ms += busy;
      statistics_.unpaired_frames = unpaired;
      if (set.has_value()) ++statistics_.decoded_sets;
    }
    if (error.has_value()) {
      worker_error_ = error;
      QMetaObject::invokeMethod(this, [this, message = *error] { emit errorOccurred(message); },
                                Qt::QueuedConnection);
      continue;
    }
    if (seek_request_.has_value() || generation != generation_) continue;
    if (!set.has_value()) {
      end_of_stream_ = true;
      continue;
    }
    queue_.push_back({generation, std::make_shared<const gb::media::FrameSet>(std::move(*set))});
  }
}

void PlaybackEngine::present(QueuedSet item) {
  {
    const std::lock_guard lock(current_mutex_);
    current_ = {std::move(item.frames), ++serial_, item.generation};
  }
  const std::lock_guard lock(statistics_mutex_);
  ++statistics_.presented_frames;
}

void PlaybackEngine::setFrameDriven(const bool enabled) { frame_driven_ = enabled; }

void PlaybackEngine::advanceFrame() {
  last_frame_tick_ = SteadyClock::now();
  tick(true);
}

void PlaybackEngine::tick(const bool from_frame) {
  bool changed{};
  bool ended{};
  bool seek_pending{};
  std::optional<double> seek_latency;
  syncClockToAudio();
  {
    std::unique_lock lock(mutex_);
    if (awaiting_seek_ && queue_.empty()) {
      if (end_of_stream_ || worker_error_.has_value()) awaiting_seek_ = false;
      seek_pending = awaiting_seek_;
    } else if (awaiting_seek_) {
      auto item = std::move(queue_.front());
      queue_.pop_front();
      awaiting_seek_ = false;
      const auto now = SteadyClock::now();
      rebaseClock(item.frames->pts, now);
      audio_hold_until_ = now + kAudioSettle;
      present(std::move(item));
      changed = true;
      in_underrun_ = false;
      const std::lock_guard statistics_lock(statistics_mutex_);
      seek_latency = millisecondsSince(seek_started_);
      statistics_.seek_present_ms.push_back(*seek_latency);
      seek_serial_ = serial_;
      seek_render_pending_ = true;
    } else if (playing_ && !scrubbing_ &&
               (from_frame || !frame_driven_ || SteadyClock::now() - last_frame_tick_ > kFrameStall)) {
      const auto now = clockTicks();
      std::optional<QueuedSet> due;
      std::uint64_t late{};
      while (!queue_.empty() && queue_.front().frames->pts <= now) {
        if (due.has_value()) ++late;
        due = std::move(queue_.front());
        queue_.pop_front();
      }
      if (due.has_value()) {
        present(std::move(*due));
        changed = true;
        in_underrun_ = false;
      } else if (queue_.empty()) {
        const auto current = this->current();
        const auto frame_end = current.frames ? current.frames->pts + std::max<std::int64_t>(1, current.frames->duration) : 0;
        if (end_of_stream_) {
          if (now >= frame_end) {
            playing_ = false;
            clock_base_ticks_ = current.frames ? current.frames->pts : 0;
            ended = true;
          }
        } else if (now >= frame_end && !in_underrun_) {
          in_underrun_ = true;
          const std::lock_guard statistics_lock(statistics_mutex_);
          ++statistics_.underruns;
        }
      }
      if (late > 0U) {
        const std::lock_guard statistics_lock(statistics_mutex_);
        statistics_.late_drops += late;
      }
    }
  }
  if (changed) condition_.notify_all();
  updateAudio(seek_pending);
  if (seek_latency.has_value()) emit seekPresented(*seek_latency);
  if (changed) emit frameChanged();
  if (ended) {
    emit playingChanged();
    emit endReached();
  }
}

void PlaybackEngine::reportRendered(const std::uint64_t serial, const std::uint64_t generation,
                                    const double upload_ms, const std::optional<double> gpu_ms) {
  std::optional<double> seek_latency;
  {
    const std::lock_guard lock(statistics_mutex_);
    if (generation == last_rendered_generation_ && serial > last_rendered_serial_ + 1U) {
      statistics_.render_skips += serial - last_rendered_serial_ - 1U;
    }
    last_rendered_serial_ = serial;
    last_rendered_generation_ = generation;
    ++statistics_.rendered_frames;
    statistics_.upload_ms_total += upload_ms;
    statistics_.upload_ms_max = std::max(statistics_.upload_ms_max, upload_ms);
    if (gpu_ms.has_value()) {
      ++statistics_.gpu_samples;
      statistics_.gpu_ms_total += *gpu_ms;
      statistics_.gpu_ms_max = std::max(statistics_.gpu_ms_max, *gpu_ms);
    }
    if (seek_render_pending_ && serial >= seek_serial_) {
      seek_render_pending_ = false;
      seek_latency = millisecondsSince(seek_started_);
      statistics_.seek_render_ms.push_back(*seek_latency);
    }
  }
  if (seek_latency.has_value()) {
    QMetaObject::invokeMethod(this, [this, latency = *seek_latency] { emit seekRendered(latency); },
                              Qt::QueuedConnection);
  }
}

void PlaybackEngine::setRendererDescription(std::string description) {
  const std::lock_guard lock(statistics_mutex_);
  statistics_.renderer = std::move(description);
}

PlaybackStatistics PlaybackEngine::statistics() const {
  std::size_t depth{};
  std::size_t capacity{};
  {
    const std::lock_guard lock(mutex_);
    depth = queue_.size();
    capacity = capacity_;
  }
  const std::lock_guard lock(statistics_mutex_);
  auto result = statistics_;
  result.queue_depth = depth;
  result.queue_capacity = capacity;
  return result;
}

void PlaybackEngine::resetStatistics() {
  const std::lock_guard lock(statistics_mutex_);
  auto renderer = std::move(statistics_.renderer);
  const auto unpaired = statistics_.unpaired_frames;
  statistics_ = {};
  statistics_.unpaired_frames = unpaired;
  statistics_.renderer = std::move(renderer);
}
