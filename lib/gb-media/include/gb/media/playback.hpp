#pragma once

#include "gb/media/media.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gb::media {

enum class SourceLayout { flat, max_dual_eac };

struct PlaybackLayout {
  SourceLayout layout{SourceLayout::flat};
  std::vector<int> streams;
  std::int32_t overlap{};
  std::string description;
};

struct PlanarPicture {
  std::int32_t width{};
  std::int32_t height{};
  std::array<const std::uint8_t *, 3> planes{};
  std::array<std::int32_t, 3> strides{};
  int stream_index{-1};
  std::int64_t pts{};
  std::int64_t duration{};
  Rational time_base;
  ColorMetadata source_color;
  long double luma_red{0.2126L};
  long double luma_blue{0.0722L};
  bool full_range{};
  std::shared_ptr<const void> storage;
};

struct FrameSet {
  std::vector<PlanarPicture> pictures;
  std::int64_t pts{};
  std::int64_t duration{};
  Rational time_base;
};

[[nodiscard]] PlaybackLayout select_playback_layout(const std::vector<VideoStream> &streams);
[[nodiscard]] std::string source_layout_name(SourceLayout layout);
[[nodiscard]] std::int64_t rescale_ticks(std::int64_t ticks, Rational from, Rational to);

// Demuxes a file once and decodes the selected video streams in lockstep. Frames from all
// selected streams are emitted together only when their presentation times are identical.
class SynchronizedReader {
public:
  SynchronizedReader(const std::filesystem::path &path, std::vector<int> stream_indices,
                     int decoder_threads = 0);
  ~SynchronizedReader();
  SynchronizedReader(const SynchronizedReader &) = delete;
  SynchronizedReader &operator=(const SynchronizedReader &) = delete;

  [[nodiscard]] const std::vector<VideoStream> &streams() const noexcept;
  [[nodiscard]] Rational time_base() const noexcept;
  [[nodiscard]] std::int64_t duration_ticks() const noexcept;
  [[nodiscard]] std::uint64_t unpaired_frames() const noexcept;

  // After a seek, next() returns the latest frame set whose PTS is not after the target, or the
  // first decodable set when the target precedes it. Targets use the first stream's time base.
  void seek(std::int64_t presentation_ticks);
  [[nodiscard]] std::optional<FrameSet> next();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace gb::media
