#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace gb::media {

struct Rational {
  std::int32_t numerator{};
  std::int32_t denominator{1};
};

struct ColorMetadata {
  std::string pixel_format;
  std::string range;
  std::string space;
  std::string transfer;
  std::string primaries;
  std::string chroma_location;
};

struct VideoStream {
  int index{};
  std::string codec;
  std::int32_t width{};
  std::int32_t height{};
  Rational time_base;
  std::int64_t start_time{};
  std::int64_t duration{};
  Rational average_frame_rate;
  ColorMetadata color;
};

struct RgbFrame {
  std::int32_t width{};
  std::int32_t height{};
  std::vector<std::uint8_t> pixels;
  int stream_index{-1};
  std::int64_t requested_pts{};
  std::int64_t pts{};
  std::int64_t duration{};
  Rational time_base;
  ColorMetadata source_color;
};

struct HudVector {
  std::string label;
  long double x{};
  long double y{};
  long double z{};
  std::string unit;
};

struct HudOverlay {
  std::vector<std::string> lines;
  std::vector<HudVector> vectors;
  bool compact{};
  int font_scale_multiplier{1};
};

enum class ProjectionMode { equirectangular, rectilinear, fisheye, stereographic, little_planet };

enum class ErrorKind { missing, io, unsupported, corrupt, invalid_argument };

class MediaError final : public std::runtime_error {
public:
  MediaError(ErrorKind kind, const std::string &message);
  [[nodiscard]] ErrorKind kind() const noexcept;

private:
  ErrorKind kind_;
};

[[nodiscard]] std::vector<VideoStream> video_streams(const std::filesystem::path &path);
[[nodiscard]] RgbFrame decode_frame(const std::filesystem::path &path, int stream_index,
                                    std::int64_t presentation_ticks);
[[nodiscard]] RgbFrame reconstruct_equirectangular(const RgbFrame &front,
                                                   const RgbFrame &rear,
                                                   std::int32_t width,
                                                   std::int32_t height,
                                                   std::int32_t overlap = 64);
[[nodiscard]] RgbFrame project_equirectangular(const RgbFrame &source,
                                               ProjectionMode mode,
                                               std::int32_t width,
                                               std::int32_t height,
                                               long double yaw_degrees = 0.0L,
                                               long double pitch_degrees = 0.0L,
                                               long double roll_degrees = 0.0L,
                                               long double field_of_view_degrees = 90.0L);
[[nodiscard]] std::string projection_mode_name(ProjectionMode mode);
void draw_hud(RgbFrame &frame, const HudOverlay &overlay);
void write_png(const std::filesystem::path &path, const RgbFrame &frame);
void write_ppm(const std::filesystem::path &path, const RgbFrame &frame);
void write_rgb24(const std::filesystem::path &path, const RgbFrame &frame);
[[nodiscard]] std::string error_kind_name(ErrorKind kind);

} // namespace gb::media
