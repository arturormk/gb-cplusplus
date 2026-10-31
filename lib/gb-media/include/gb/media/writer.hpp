#pragma once

#include "gb/media/media.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace gb::media {

enum class VideoCodec { h264, hevc };

struct VideoWriterOptions {
  std::int32_t width{};        // even
  std::int32_t height{};       // even
  Rational time_base{1, 1000}; // of the PTS passed to write_video
  Rational frame_rate{30, 1};
  VideoCodec codec{VideoCodec::h264};
  int crf{20};
  std::string preset{"medium"};
  // Stereo AAC at this sample rate, or no audio track.
  std::optional<std::int32_t> audio_sample_rate;
  std::int32_t audio_bitrate{192000};
};

// Encodes and muxes an exported video (docs/plans/export/pipeline.md#encoder): H.264 through
// libx264 or HEVC through libx265 (yuv420p, BT.709, limited range, tagged so), FFmpeg's native AAC
// for audio, into MP4 (with +faststart, HEVC tagged hvc1) or MKV, chosen by the file extension.
// No metadata is copied from any source: there is no location, telemetry or camera information in
// the output. Throws MediaError when an encoder is missing or a write fails.
class VideoWriter {
public:
  VideoWriter(const std::filesystem::path &path, const VideoWriterOptions &options);
  ~VideoWriter();
  VideoWriter(const VideoWriter &) = delete;
  VideoWriter &operator=(const VideoWriter &) = delete;

  // A picture of width × height pixels, 4 bytes per pixel in B, G, R, X byte order (a
  // QImage::Format_RGB32 image on a little-endian machine), rows `stride` bytes apart. PTS values
  // must increase.
  void write_video(const std::uint8_t *pixels, int stride, std::int64_t pts);

  // Interleaved stereo float samples, continuing the audio track. Ignored without audio.
  void write_audio(const float *samples, std::int64_t frames);
  [[nodiscard]] std::int64_t audio_frames_written() const noexcept;

  // Flushes the encoders and writes the trailer. The file is incomplete without it.
  void finish();

  [[nodiscard]] std::string description() const; // "libx264 1920×1080, aac 48000 Hz, mp4"

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Whether an encoder is available by name, such as "libx264".
[[nodiscard]] bool encoder_available(const std::string &name);

} // namespace gb::media
