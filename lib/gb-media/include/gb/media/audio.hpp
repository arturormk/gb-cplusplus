#pragma once

#include "gb/media/media.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gb::media {

struct AudioStream {
  int index{-1};
  std::string codec;
  std::int32_t sample_rate{};
  std::int32_t channels{};
  std::string channel_layout;
  Rational time_base;
  std::int64_t start_time{};
  std::int64_t duration{};
};

struct AudioSelection {
  std::optional<int> stream;
  std::string description;
};

// Interleaved stereo float samples. Positions count samples at the stream's own sample rate,
// relative to the stream start, the same origin SynchronizedReader uses for video.
struct AudioBlock {
  std::int64_t first_sample{};
  std::vector<float> samples;

  [[nodiscard]] std::int64_t frames() const noexcept {
    return static_cast<std::int64_t>(samples.size() / 2U);
  }
};

[[nodiscard]] std::vector<AudioStream> audio_streams(const std::filesystem::path &path);

// Chooses the first mono or stereo stream. Streams with more channels, such as the four-channel
// ambisonic PCM track of MAX recordings, are not downmixed: without a spatial renderer any
// stereo fold-down would be an invented mix.
[[nodiscard]] AudioSelection select_playback_audio(const std::vector<AudioStream> &streams);

// Decodes one mono or stereo audio stream to interleaved stereo float at its own sample rate.
// Mono is duplicated to both channels at unity gain; no other rematrixing or resampling happens.
class AudioReader {
public:
  AudioReader(const std::filesystem::path &path, int stream_index);
  ~AudioReader();
  AudioReader(const AudioReader &) = delete;
  AudioReader &operator=(const AudioReader &) = delete;

  static constexpr int kChannels = 2;

  [[nodiscard]] const AudioStream &stream() const noexcept;
  [[nodiscard]] std::int32_t sample_rate() const noexcept;

  // Accurate seek: the next block starts exactly at the requested sample, or at the first
  // decodable sample when the target precedes it.
  void seek(std::int64_t sample);
  [[nodiscard]] std::optional<AudioBlock> next();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace gb::media
