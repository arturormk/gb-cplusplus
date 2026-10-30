#pragma once

#include "gb/media/audio.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct AudioStatistics {
  std::uint64_t starts{};
  std::uint64_t audible_frames{};
  std::uint64_t underruns{};
  std::uint64_t aligned_skip_frames{};
  std::uint64_t aligned_silence_frames{};
  std::int32_t sample_rate{};
  std::int32_t buffer_frames{};
  double latency_ms{};
  std::string driver;
  std::string error;
};

// Plays one decoded audio stream through an SDL2 audio device. Output stays silent until start()
// aligns it to the video timeline: the first sample written is the one the anchored clock says
// will be due when it is heard. Once audible, the device position becomes the master clock.
// stop() fades out within a few milliseconds and drops all buffered audio, so nothing stale is
// ever heard after a seek.
class AudioOutput {
public:
  using Clock = std::chrono::steady_clock;
  enum class State { idle, aligning, playing, fading, finished };

  // Throws std::runtime_error when the stream cannot be decoded or no device can be opened.
  AudioOutput(const std::string &path, int stream_index, const std::string &application_name);
  ~AudioOutput();
  AudioOutput(const AudioOutput &) = delete;
  AudioOutput &operator=(const AudioOutput &) = delete;

  [[nodiscard]] std::int32_t sampleRate() const noexcept { return sample_rate_; }

  // Sample `anchor_sample` is due at `anchor_time`, and the timeline advances in real time.
  void start(std::int64_t anchor_sample, Clock::time_point anchor_time);
  void stop();
  // The sample being heard now, once aligned audio is playing; otherwise empty.
  [[nodiscard]] std::optional<std::int64_t> position(Clock::time_point now);
  [[nodiscard]] State state() const;
  void setMuted(bool muted);
  // Releases the device while nothing is playing. Call from the thread that calls start().
  void pauseDeviceIfIdle();
  [[nodiscard]] AudioStatistics statistics() const;

private:
  static void callback(void *user, std::uint8_t *stream, int bytes);
  void fill(float *out, std::int64_t frames);
  std::int64_t copyQueued(float *out, std::int64_t frames);
  void discardQueued(std::int64_t frames);
  [[nodiscard]] std::int64_t queuedPosition() const;
  void clearQueue();
  void worker();

  std::unique_ptr<gb::media::AudioReader> reader_;
  std::uint32_t device_{};
  bool device_running_{};
  std::int32_t sample_rate_{};
  Clock::duration latency_{};

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::thread thread_;
  bool stop_thread_{};
  bool decoding_{};
  bool end_of_stream_{};
  std::uint64_t generation_{};
  std::optional<std::int64_t> seek_request_;
  std::deque<gb::media::AudioBlock> queue_;
  std::int64_t front_offset_{};
  std::int64_t queued_frames_{};
  std::int64_t capacity_frames_{};

  State state_{State::idle};
  std::int64_t anchor_sample_{};
  Clock::time_point anchor_time_;
  std::int64_t buffer_first_sample_{};
  std::int64_t buffer_frames_{};
  Clock::time_point buffer_time_;
  std::int64_t next_sample_{};
  std::optional<std::int64_t> last_position_;
  float gain_{};
  float gain_step_{};
  bool muted_{};
  AudioStatistics statistics_;
};
