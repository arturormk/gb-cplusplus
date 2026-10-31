#include "gb/media/media.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace gb::media {
namespace {

constexpr long double kPi = 3.141592653589793238462643383279502884L;

enum class CubeFace { left, right, top, front, back, down };

struct Vec2 {
  long double x{};
  long double y{};
};

struct Vec3 {
  long double x{};
  long double y{};
  long double z{};
};

struct FaceCoordinate {
  CubeFace face{};
  Vec2 uv;
};

using Pixel = std::array<long double, 3>;

void validate_output_dimensions(const std::int32_t width, const std::int32_t height) {
  constexpr std::int64_t maximum_pixels = 128LL * 1024LL * 1024LL;
  if (width <= 0 || height <= 0 || width > 16384 || height > 8192 ||
      static_cast<std::int64_t>(width) * height > maximum_pixels) {
    throw MediaError(ErrorKind::invalid_argument, "output dimensions exceed safety limits");
  }
}

Pixel sample(const RgbFrame &image, const long double x, const long double y,
             const bool wrap_x = false) {
  auto mapped_x = x;
  if (wrap_x) {
    mapped_x = std::fmod(mapped_x, static_cast<long double>(image.width));
    if (mapped_x < 0.0L)
      mapped_x += image.width;
  }
  const auto floor_x = std::floor(mapped_x);
  const auto floor_y = std::floor(y);
  const auto x0 = std::clamp(static_cast<int>(floor_x), 0, image.width - 1);
  const auto y0 = std::clamp(static_cast<int>(floor_y), 0, image.height - 1);
  const auto x1 = wrap_x ? (x0 + 1) % image.width : std::min(x0 + 1, image.width - 1);
  const auto y1 = std::min(y0 + 1, image.height - 1);
  const auto fx = std::clamp(mapped_x - floor_x, 0.0L, 1.0L);
  const auto fy = std::clamp(y - floor_y, 0.0L, 1.0L);
  Pixel result{};
  for (std::size_t channel = 0; channel < result.size(); ++channel) {
    const auto at = [&](const int px, const int py) {
      return image.pixels[(static_cast<std::size_t>(py) * static_cast<std::size_t>(image.width) +
                           static_cast<std::size_t>(px)) *
                              3U +
                          channel];
    };
    result[channel] = (1.0L - fy) * ((1.0L - fx) * at(x0, y0) + fx * at(x1, y0)) +
                      fy * ((1.0L - fx) * at(x0, y1) + fx * at(x1, y1));
  }
  return result;
}

void store(RgbFrame &image, const int x, const int y, const Pixel color) {
  const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                       static_cast<std::size_t>(x)) *
                      3U;
  for (std::size_t channel = 0; channel < color.size(); ++channel) {
    image.pixels[offset + channel] =
        static_cast<std::uint8_t>(std::clamp(std::lround(color[channel]), 0L, 255L));
  }
}

FaceCoordinate face_coordinate(const Vec3 direction) {
  const auto absolute_x = std::fabs(direction.x);
  const auto absolute_y = std::fabs(direction.y);
  const auto absolute_z = std::fabs(direction.z);
  CubeFace face{};
  Vec3 intersection;
  if (absolute_x >= absolute_y && absolute_x >= absolute_z) {
    const auto scale = 1.0L / absolute_x;
    intersection = {direction.x * scale, direction.y * scale, direction.z * scale};
    face = direction.x < 0.0L ? CubeFace::left : CubeFace::right;
  } else if (absolute_y >= absolute_z) {
    const auto scale = 1.0L / absolute_y;
    intersection = {direction.x * scale, direction.y * scale, direction.z * scale};
    face = direction.y < 0.0L ? CubeFace::back : CubeFace::front;
  } else {
    const auto scale = 1.0L / absolute_z;
    intersection = {direction.x * scale, direction.y * scale, direction.z * scale};
    face = direction.z < 0.0L ? CubeFace::down : CubeFace::top;
  }

  const auto equal_area = [](const long double value) { return std::atan(value) * 4.0L / kPi; };
  Vec2 uv;
  switch (face) {
  case CubeFace::left:
    uv = {(equal_area(intersection.y) + 1.0L) * 0.5L, (equal_area(intersection.z) + 1.0L) * 0.5L};
    break;
  case CubeFace::right:
    uv = {(1.0L - equal_area(intersection.y)) * 0.5L, (equal_area(intersection.z) + 1.0L) * 0.5L};
    break;
  case CubeFace::front:
    uv = {(equal_area(intersection.x) + 1.0L) * 0.5L, (equal_area(intersection.z) + 1.0L) * 0.5L};
    break;
  case CubeFace::back:
    uv = {(1.0L - equal_area(intersection.x)) * 0.5L, (equal_area(intersection.z) + 1.0L) * 0.5L};
    break;
  case CubeFace::down:
    uv = {(1.0L - equal_area(intersection.x)) * 0.5L, (1.0L - equal_area(intersection.y)) * 0.5L};
    break;
  case CubeFace::top:
    uv = {(1.0L - equal_area(intersection.x)) * 0.5L, (equal_area(intersection.y) + 1.0L) * 0.5L};
    break;
  }
  if (face == CubeFace::down || face == CubeFace::back || face == CubeFace::top) {
    uv = {uv.y, 1.0L - uv.x};
  }
  uv.x = std::clamp(uv.x, 0.0L, std::nextafter(1.0L, 0.0L));
  uv.y = std::clamp(uv.y, 0.0L, std::nextafter(1.0L, 0.0L));
  return {face, uv};
}

Pixel packed_face_color(const FaceCoordinate coordinate, const RgbFrame &first,
                        const RgbFrame &second, const int overlap) {
  const auto center_width = first.height;
  const auto side_width = (first.width - center_width) / 2;
  const auto blend_width = overlap / 2;
  const auto center_offset = side_width;
  const auto right_offset = side_width + center_width;
  // The published MAX mapping uses a bottom-up bitmap buffer. Decoded RGB frames are top-down.
  const auto y = (1.0L - coordinate.uv.y) * (first.height - 1);
  if (coordinate.face == CubeFace::front || coordinate.face == CubeFace::back) {
    const auto x = center_offset + coordinate.uv.x * (center_width - 1);
    return sample(coordinate.face == CubeFace::front ? first : second, x, y);
  }

  const auto *source =
      coordinate.face == CubeFace::left || coordinate.face == CubeFace::right ? &first : &second;
  const auto offset =
      coordinate.face == CubeFace::right || coordinate.face == CubeFace::top ? right_offset : 0;
  const auto duplicate_ratio = static_cast<long double>(blend_width) / side_width;
  const auto left_u = 2.0L * (0.5L - duplicate_ratio) * coordinate.uv.x;
  const auto right_u =
      2.0L * (0.5L - duplicate_ratio) * (coordinate.uv.x - 0.5L) + 0.5L + duplicate_ratio;
  if (left_u <= 0.5L - 2.0L * duplicate_ratio) {
    return sample(*source, offset + left_u * side_width, y);
  }
  if (right_u >= 0.5L + 2.0L * duplicate_ratio) {
    return sample(*source, offset + right_u * side_width, y);
  }
  const auto first_color = sample(*source, offset + left_u * side_width, y);
  const auto second_color = sample(*source, offset + right_u * side_width, y);
  const auto alpha =
      std::clamp((left_u - 0.5L + 2.0L * duplicate_ratio) / (2.0L * duplicate_ratio), 0.0L, 1.0L);
  Pixel result{};
  for (std::size_t channel = 0; channel < result.size(); ++channel) {
    result[channel] = first_color[channel] * (1.0L - alpha) + second_color[channel] * alpha;
  }
  return result;
}

Vec3 normalize(const Vec3 value) {
  const auto magnitude = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  return magnitude == 0.0L ? Vec3{}
                           : Vec3{value.x / magnitude, value.y / magnitude, value.z / magnitude};
}

Vec3 rotate_camera(Vec3 value, const long double yaw, const long double pitch,
                   const long double roll) {
  const auto roll_cosine = std::cos(roll);
  const auto roll_sine = std::sin(roll);
  value = {value.x * roll_cosine - value.y * roll_sine, value.x * roll_sine + value.y * roll_cosine,
           value.z};
  const auto pitch_cosine = std::cos(pitch);
  const auto pitch_sine = std::sin(pitch);
  value = {value.x, value.y * pitch_cosine + value.z * pitch_sine,
           -value.y * pitch_sine + value.z * pitch_cosine};
  const auto yaw_cosine = std::cos(yaw);
  const auto yaw_sine = std::sin(yaw);
  return {value.x * yaw_cosine + value.z * yaw_sine, value.y,
          -value.x * yaw_sine + value.z * yaw_cosine};
}

std::optional<Vec3> camera_ray(const ProjectionMode mode, const long double x, const long double y,
                               const long double aspect, const long double field_of_view) {
  if (mode == ProjectionMode::rectilinear) {
    const auto tangent = std::tan(field_of_view * 0.5L);
    return normalize({x * aspect * tangent, y * tangent, 1.0L});
  }
  const auto radius = std::sqrt(x * x + y * y);
  if (radius > 1.0L)
    return std::nullopt;
  if (radius == 0.0L)
    return Vec3{0.0L, 0.0L, 1.0L};
  long double theta{};
  if (mode == ProjectionMode::fisheye)
    theta = radius * field_of_view * 0.5L;
  else
    theta = 2.0L * std::atan(radius * std::tan(field_of_view * 0.25L));
  const auto sine = std::sin(theta);
  return Vec3{x / radius * sine, y / radius * sine, std::cos(theta)};
}

} // namespace

RgbFrame reconstruct_equirectangular(const RgbFrame &front, const RgbFrame &rear,
                                     const std::int32_t width, const std::int32_t height,
                                     const std::int32_t overlap) {
  if (front.width != rear.width || front.height != rear.height || front.height <= 0 ||
      front.width <= 0) {
    throw MediaError(ErrorKind::invalid_argument, "source frames must have identical dimensions");
  }
  validate_output_dimensions(width, height);
  if (overlap <= 0 || overlap > 256 || overlap % 2 != 0 ||
      front.width != front.height * 3 + overlap) {
    throw MediaError(ErrorKind::invalid_argument,
                     "source frames do not match the supported MAX EAC strip layout");
  }
  RgbFrame output;
  output.width = width;
  output.height = height;
  output.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U);
  output.stream_index = -1;
  output.requested_pts = front.requested_pts;
  output.pts = front.pts;
  output.duration = front.duration;
  output.time_base = front.time_base;
  output.source_color = {"rgb24", "pc", "bt709", "bt709", "bt709", "unspecified"};

  for (int y = 0; y < height; ++y) {
    const auto latitude = kPi * 0.5L - (static_cast<long double>(y) + 0.5L) / height * kPi;
    const auto latitude_cosine = std::cos(latitude);
    for (int x = 0; x < width; ++x) {
      const auto longitude = (static_cast<long double>(x) + 0.5L) / width * 2.0L * kPi - kPi;
      const Vec3 direction{latitude_cosine * std::sin(longitude),
                           latitude_cosine * std::cos(longitude), std::sin(latitude)};
      store(output, x, y, packed_face_color(face_coordinate(direction), front, rear, overlap));
    }
  }
  return output;
}

RgbFrame project_equirectangular(const RgbFrame &source, const ProjectionMode mode,
                                 const std::int32_t width, const std::int32_t height,
                                 const long double yaw_degrees, const long double pitch_degrees,
                                 const long double roll_degrees,
                                 const long double field_of_view_degrees) {
  validate_output_dimensions(width, height);
  if (source.width <= 0 || source.height <= 0 ||
      source.pixels.size() !=
          static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height) * 3U) {
    throw MediaError(ErrorKind::invalid_argument, "projection source must be packed RGB24");
  }
  if (!std::isfinite(yaw_degrees) || !std::isfinite(pitch_degrees) ||
      !std::isfinite(roll_degrees) || !std::isfinite(field_of_view_degrees) ||
      field_of_view_degrees <= 0.0L || field_of_view_degrees >= 360.0L) {
    throw MediaError(ErrorKind::invalid_argument,
                     "invalid projection orientation or field of view");
  }
  RgbFrame output = source;
  output.width = width;
  output.height = height;
  output.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3U, 0U);
  const auto yaw = yaw_degrees * kPi / 180.0L;
  auto pitch = pitch_degrees * kPi / 180.0L;
  const auto roll = roll_degrees * kPi / 180.0L;
  const auto field_of_view = field_of_view_degrees * kPi / 180.0L;
  if (mode == ProjectionMode::little_planet)
    pitch -= kPi * 0.5L;
  const auto aspect = static_cast<long double>(width) / height;
  for (int y = 0; y < height; ++y) {
    const auto normalized_y = 1.0L - 2.0L * (static_cast<long double>(y) + 0.5L) / height;
    for (int x = 0; x < width; ++x) {
      if (mode == ProjectionMode::equirectangular) {
        const auto source_x = (static_cast<long double>(x) + 0.5L) / width * source.width - 0.5L;
        const auto source_y = (static_cast<long double>(y) + 0.5L) / height * source.height - 0.5L;
        store(output, x, y, sample(source, source_x, source_y, true));
        continue;
      }
      const auto normalized_x = 2.0L * (static_cast<long double>(x) + 0.5L) / width - 1.0L;
      const auto ray = camera_ray(mode, normalized_x, normalized_y, aspect, field_of_view);
      if (!ray.has_value())
        continue;
      const auto direction = normalize(rotate_camera(*ray, yaw, pitch, roll));
      const auto longitude = std::atan2(direction.x, direction.z);
      const auto latitude = std::asin(std::clamp(direction.y, -1.0L, 1.0L));
      const auto source_x = (longitude / (2.0L * kPi) + 0.5L) * source.width - 0.5L;
      const auto source_y = (0.5L - latitude / kPi) * source.height - 0.5L;
      store(output, x, y, sample(source, source_x, source_y, true));
    }
  }
  return output;
}

std::string projection_mode_name(const ProjectionMode mode) {
  switch (mode) {
  case ProjectionMode::equirectangular:
    return "equirectangular";
  case ProjectionMode::rectilinear:
    return "rectilinear";
  case ProjectionMode::fisheye:
    return "fisheye";
  case ProjectionMode::stereographic:
    return "stereographic";
  case ProjectionMode::little_planet:
    return "little_planet";
  }
  return "unknown";
}

} // namespace gb::media
