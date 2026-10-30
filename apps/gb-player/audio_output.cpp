#include "audio_output.hpp"

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>

namespace {

// About 21 ms at 48 kHz: short enough that a stop is heard promptly, long enough to survive a
// fully loaded decoder.
constexpr int kBufferFrames = 1024;
constexpr double kFadeSeconds = 0.005;
constexpr double kQueueSeconds = 0.5;
// Seek this far before the expected first sample so alignment trims rather than pads.
constexpr double kSeekLeadSeconds = 0.02;

std::int64_t framesIn(const AudioOutput::Clock::duration duration, const std::int32_t rate) {
  return static_cast<std::int64_t>(std::floor(std::chrono::duration<double>(duration).count() * rate));
}

} // namespace

AudioOutput::AudioOutput(const std::string &path, const int stream_index, const std::string &application_name) {
  try {
    reader_ = std::make_unique<gb::media::AudioReader>(path, stream_index);
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("cannot decode audio: ") + error.what());
  }
  sample_rate_ = reader_->sample_rate();

  SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
  SDL_SetHint("SDL_AUDIO_DEVICE_APP_NAME", application_name.c_str());
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    throw std::runtime_error(std::string("cannot initialize audio: ") + SDL_GetError());
  }
  SDL_AudioSpec desired{};
  desired.freq = sample_rate_;
  desired.format = AUDIO_F32SYS;
  desired.channels = gb::media::AudioReader::kChannels;
  desired.samples = kBufferFrames;
  desired.callback = &AudioOutput::callback;
  desired.userdata = this;
  SDL_AudioSpec obtained{};
  // No allowed changes: SDL converts to the device format, so the callback always works in the
  // stream's own rate and the sample clock stays exact.
  device_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
  if (device_ == 0U) {
    const std::string message = SDL_GetError();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    throw std::runtime_error("cannot open audio device: " + message);
  }
  const auto *driver = SDL_GetCurrentAudioDriver();
  statistics_.driver = driver == nullptr ? "unknown" : driver;
  statistics_.sample_rate = sample_rate_;
  statistics_.buffer_frames = obtained.samples;
  // SDL2 cannot report device latency. Assume one buffer is queued ahead of the one being
  // filled; the residual error is bounded by about one buffer (see ADR-0008).
  latency_ = std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(static_cast<double>(obtained.samples) / sample_rate_));
  statistics_.latency_ms = std::chrono::duration<double, std::milli>(latency_).count();
  capacity_frames_ = static_cast<std::int64_t>(kQueueSeconds * sample_rate_);
  gain_step_ = static_cast<float>(1.0 / (kFadeSeconds * sample_rate_));
  thread_ = std::thread(&AudioOutput::worker, this);
}

AudioOutput::~AudioOutput() {
  {
    const std::lock_guard lock(mutex_);
    stop_thread_ = true;
  }
  condition_.notify_all();
  if (thread_.joinable()) thread_.join();
  SDL_CloseAudioDevice(device_);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioOutput::start(const std::int64_t anchor_sample, const Clock::time_point anchor_time) {
  {
    const std::lock_guard lock(mutex_);
    ++generation_;
    clearQueue();
    end_of_stream_ = false;
    statistics_.error.clear();
    state_ = State::aligning;
    gain_ = 0.0F;
    anchor_sample_ = anchor_sample;
    anchor_time_ = anchor_time;
    last_position_.reset();
    decoding_ = true;
    const auto expected = anchor_sample + framesIn(Clock::now() + latency_ - anchor_time, sample_rate_);
    seek_request_ = std::max<std::int64_t>(
        0, expected - static_cast<std::int64_t>(kSeekLeadSeconds * sample_rate_));
    ++statistics_.starts;
  }
  condition_.notify_all();
  if (!device_running_) {
    SDL_PauseAudioDevice(device_, 0);
    device_running_ = true;
  }
}

void AudioOutput::stop() {
  {
    const std::lock_guard lock(mutex_);
    ++generation_;
    decoding_ = false;
    seek_request_.reset();
    if (state_ == State::playing && gain_ > 0.0F) {
      state_ = State::fading;
    } else {
      state_ = State::idle;
      clearQueue();
    }
  }
  condition_.notify_all();
}

std::optional<std::int64_t> AudioOutput::position(const Clock::time_point now) {
  const std::lock_guard lock(mutex_);
  if (state_ != State::playing) return std::nullopt;
  // The buffer's first sample is heard `latency_` after it was requested.
  auto result = buffer_first_sample_ + framesIn(now - buffer_time_ - latency_, sample_rate_);
  result = std::min(result, buffer_first_sample_ + buffer_frames_);
  if (last_position_.has_value()) result = std::max(result, *last_position_);
  last_position_ = result;
  return result;
}

AudioOutput::State AudioOutput::state() const {
  const std::lock_guard lock(mutex_);
  return state_;
}

void AudioOutput::setMuted(const bool muted) {
  const std::lock_guard lock(mutex_);
  muted_ = muted;
}

void AudioOutput::pauseDeviceIfIdle() {
  if (!device_running_) return;
  const auto current = state();
  if (current != State::idle && current != State::finished) return;
  SDL_PauseAudioDevice(device_, 1);
  device_running_ = false;
}

AudioStatistics AudioOutput::statistics() const {
  const std::lock_guard lock(mutex_);
  return statistics_;
}

void AudioOutput::clearQueue() {
  queue_.clear();
  front_offset_ = 0;
  queued_frames_ = 0;
}

std::int64_t AudioOutput::queuedPosition() const { return queue_.front().first_sample + front_offset_; }

void AudioOutput::discardQueued(std::int64_t frames) {
  while (frames > 0 && !queue_.empty()) {
    const auto available = queue_.front().frames() - front_offset_;
    const auto used = std::min(frames, available);
    front_offset_ += used;
    queued_frames_ -= used;
    frames -= used;
    if (front_offset_ == queue_.front().frames()) {
      queue_.pop_front();
      front_offset_ = 0;
    }
  }
}

// Copies contiguous queued audio with the fade/mute gain applied; returns frames written.
std::int64_t AudioOutput::copyQueued(float *out, const std::int64_t frames) {
  std::int64_t written{};
  while (written < frames && !queue_.empty()) {
    const auto &block = queue_.front();
    const auto available = block.frames() - front_offset_;
    const auto count = std::min(frames - written, available);
    const auto *source = block.samples.data() + static_cast<std::size_t>(front_offset_) * 2U;
    for (std::int64_t frame = 0; frame < count; ++frame) {
      const auto target = state_ == State::fading || muted_ ? 0.0F : 1.0F;
      gain_ = gain_ < target ? std::min(target, gain_ + gain_step_) : std::max(target, gain_ - gain_step_);
      out[(written + frame) * 2] = source[frame * 2] * gain_;
      out[(written + frame) * 2 + 1] = source[frame * 2 + 1] * gain_;
    }
    written += count;
    discardQueued(count);
    if (state_ == State::fading && gain_ <= 0.0F) break;
  }
  return written;
}

void AudioOutput::callback(void *user, std::uint8_t *stream, const int bytes) {
  auto *self = static_cast<AudioOutput *>(user);
  auto *out = reinterpret_cast<float *>(stream);
  std::memset(stream, 0, static_cast<std::size_t>(bytes));
  self->fill(out, bytes / static_cast<int>(sizeof(float) * gb::media::AudioReader::kChannels));
}

void AudioOutput::fill(float *out, const std::int64_t frames) {
  const auto now = Clock::now();
  bool wake{};
  {
    const std::lock_guard lock(mutex_);
    switch (state_) {
    case State::idle:
    case State::finished:
      return;
    case State::fading: {
      copyQueued(out, frames);
      if (gain_ <= 0.0F || queue_.empty()) {
        state_ = State::idle;
        clearQueue();
      }
      return;
    }
    case State::aligning: {
      const auto first = anchor_sample_ + framesIn(now + latency_ - anchor_time_, sample_rate_);
      if (!queue_.empty() && queuedPosition() < first) {
        const auto skip = std::min(first - queuedPosition(), queued_frames_);
        statistics_.aligned_skip_frames += static_cast<std::uint64_t>(skip);
        discardQueued(skip);
        wake = true;
      }
      if (queue_.empty()) {
        if (end_of_stream_) state_ = State::finished;
        break;
      }
      const auto silence = queuedPosition() - first;
      if (silence >= frames) break;
      statistics_.aligned_silence_frames += static_cast<std::uint64_t>(silence);
      state_ = State::playing;
      gain_ = 0.0F;
      buffer_first_sample_ = first;
      buffer_frames_ = frames;
      buffer_time_ = now;
      const auto written = copyQueued(out + silence * 2, frames - silence);
      statistics_.audible_frames += static_cast<std::uint64_t>(written);
      next_sample_ = first + silence + written;
      wake = true;
      if (silence + written < frames && !end_of_stream_) {
        ++statistics_.underruns;
        state_ = State::aligning;
        anchor_sample_ = first;
        anchor_time_ = now + latency_;
      }
      break;
    }
    case State::playing: {
      buffer_first_sample_ = next_sample_;
      buffer_frames_ = frames;
      buffer_time_ = now;
      const auto written = copyQueued(out, frames);
      statistics_.audible_frames += static_cast<std::uint64_t>(written);
      next_sample_ += frames;
      wake = true;
      if (written < frames) {
        if (end_of_stream_) {
          state_ = State::finished;
        } else {
          // Decoder fell behind: go silent and realign on the same timeline.
          ++statistics_.underruns;
          state_ = State::aligning;
          anchor_sample_ = buffer_first_sample_;
          anchor_time_ = now + latency_;
        }
      }
      break;
    }
    }
  }
  if (wake) condition_.notify_all();
}

void AudioOutput::worker() {
  std::unique_lock lock(mutex_);
  while (true) {
    condition_.wait(lock, [this] {
      return stop_thread_ || seek_request_.has_value() ||
             (decoding_ && !end_of_stream_ && queued_frames_ < capacity_frames_);
    });
    if (stop_thread_) return;
    const auto target = std::exchange(seek_request_, std::nullopt);
    const auto generation = generation_;
    lock.unlock();

    std::optional<gb::media::AudioBlock> block;
    std::optional<std::string> error;
    try {
      if (target.has_value()) reader_->seek(*target);
      block = reader_->next();
    } catch (const std::exception &failure) {
      error = failure.what();
    }

    lock.lock();
    if (generation != generation_ || seek_request_.has_value()) continue;
    if (error.has_value()) {
      statistics_.error = *error;
      end_of_stream_ = true;
      continue;
    }
    if (!block.has_value()) {
      end_of_stream_ = true;
      continue;
    }
    queued_frames_ += block->frames();
    queue_.push_back(std::move(*block));
  }
}
