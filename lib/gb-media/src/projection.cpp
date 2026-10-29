#include "gb/media/media.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gb::media {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct Vec2 { double x{}; double y{}; };
struct Vec3 { double x{}; double y{}; double z{}; };

Vec2 rotate(const Vec2 value, const int quarter_turns) {
  switch (quarter_turns) {
  case 1: return {-value.y, value.x};
  case 2: return {-value.x, -value.y};
  case 3: return {value.y, -value.x};
  default: return value;
  }
}

Vec2 xyz_to_cube(const Vec3 value, int &face) {
  const auto phi = std::atan2(value.x, value.z);
  const auto theta = std::asin(value.y);
  double normalized_phi{};
  int direction{};
  if (phi >= -kPi / 4.0 && phi < kPi / 4.0) {
    direction = 4; normalized_phi = phi;
  } else if (phi >= -3.0 * kPi / 4.0 && phi < -kPi / 4.0) {
    direction = 1; normalized_phi = phi + kPi / 2.0;
  } else if (phi >= kPi / 4.0 && phi < 3.0 * kPi / 4.0) {
    direction = 0; normalized_phi = phi - kPi / 2.0;
  } else {
    direction = 5; normalized_phi = phi + (phi > 0.0 ? -kPi : kPi);
  }
  const auto threshold = std::atan(std::cos(normalized_phi));
  if (theta > threshold) direction = 3;
  else if (theta < -threshold) direction = 2;

  Vec2 uv;
  switch (direction) {
  case 0: uv = {-value.z / value.x, value.y / value.x}; face = 2; break;
  case 1: uv = {-value.z / value.x, -value.y / value.x}; face = 0; break;
  case 2: uv = rotate({-value.x / value.y, -value.z / value.y}, 3); face = 5; break;
  case 3: uv = rotate({value.x / value.y, -value.z / value.y}, 3); face = 3; break;
  case 4: uv = {value.x / value.z, value.y / value.z}; face = 1; break;
  default: uv = rotate({value.x / value.z, -value.y / value.z}, 1); face = 4; break;
  }
  return uv;
}

Vec2 xyz_to_eac(const Vec3 xyz, const int cube_size) {
  const auto eac_width = cube_size * 3;
  const auto eac_height = cube_size * 2;
  const auto u_pad = 2.0 / static_cast<double>(eac_width);
  const auto v_pad = 2.0 / static_cast<double>(eac_height);
  int face{};
  auto uv = xyz_to_cube(xyz, face);
  uv.x = 2.0 / kPi * std::atan(uv.x) + 0.5;
  uv.y = 2.0 / kPi * std::atan(uv.y) + 0.5;
  uv.x = (uv.x + face % 3) * (1.0 - 2.0 * u_pad) / 3.0 + u_pad;
  uv.y = uv.y * (0.5 - 2.0 * v_pad) + v_pad + 0.5 * (face / 3);
  return {uv.x * eac_width - 0.5, uv.y * eac_height - 0.5};
}

std::array<double, 3> sample(const RgbFrame &image, const double x, const double y) {
  const auto x0 = std::clamp(static_cast<int>(std::floor(x)), 0, image.width - 1);
  const auto y0 = std::clamp(static_cast<int>(std::floor(y)), 0, image.height - 1);
  const auto x1 = std::min(x0 + 1, image.width - 1);
  const auto y1 = std::min(y0 + 1, image.height - 1);
  const auto fx = std::clamp(x - std::floor(x), 0.0, 1.0);
  const auto fy = std::clamp(y - std::floor(y), 0.0, 1.0);
  std::array<double, 3> result{};
  for (std::size_t channel = 0; channel < result.size(); ++channel) {
    const auto at = [&](const int px, const int py) {
      return image.pixels[(static_cast<std::size_t>(py) *
                           static_cast<std::size_t>(image.width) +
                           static_cast<std::size_t>(px)) * 3U + channel];
    };
    result[channel] = (1.0 - fy) * ((1.0 - fx) * at(x0, y0) + fx * at(x1, y0)) +
                      fy * ((1.0 - fx) * at(x0, y1) + fx * at(x1, y1));
  }
  return result;
}

std::array<double, 3> sample_gopro(const RgbFrame &image, const Vec2 eac,
                                   const int overlap) {
  const auto cube = image.height;
  const auto gap = (cube * 3 + overlap * 2 - image.width) / 2;
  auto mapped_x = eac.x;
  auto blend = 0.0;
  if (eac.x < cube || eac.x > cube * 2) {
    auto offset = 0;
    const auto compressed = cube - gap;
    const auto local = std::fmod(eac.x, static_cast<double>(cube)) * compressed / cube;
    if (eac.x >= cube * 2) offset = cube * 2 + overlap - gap;
    if (local >= (compressed + overlap) / 2.0) offset += overlap;
    mapped_x = local + offset;
    if (local > (compressed - overlap) / 2.0 && local < (compressed + overlap) / 2.0) {
      blend = (local - (compressed - overlap) / 2.0) / overlap;
    }
  } else {
    mapped_x += overlap - gap;
  }
  auto first = sample(image, mapped_x, eac.y);
  if (blend > 0.0) {
    const auto second = sample(image, mapped_x + overlap, eac.y);
    for (std::size_t channel = 0; channel < first.size(); ++channel) {
      first[channel] = first[channel] * (1.0 - blend) + second[channel] * blend;
    }
  }
  return first;
}

} // namespace

RgbFrame reconstruct_equirectangular(const RgbFrame &front, const RgbFrame &rear,
                                     const std::int32_t width, const std::int32_t height,
                                     const std::int32_t overlap) {
  if (front.width != rear.width || front.height != rear.height || front.height <= 0 ||
      front.width <= 0) {
    throw MediaError(ErrorKind::invalid_argument, "source frames must have identical dimensions");
  }
  if (width <= 0 || height <= 0 || width > 16384 || height > 8192 ||
      static_cast<std::int64_t>(width) * height > 128LL * 1024LL * 1024LL) {
    throw MediaError(ErrorKind::invalid_argument, "output dimensions exceed safety limits");
  }
  if (overlap <= 0 || overlap > 128 || front.width > front.height * 3 + overlap * 2) {
    throw MediaError(ErrorKind::invalid_argument, "invalid overlap or source MAX EAC geometry");
  }
  RgbFrame output;
  output.width = width;
  output.height = height;
  output.pixels.resize(static_cast<std::size_t>(width) *
                       static_cast<std::size_t>(height) * 3U);
  output.stream_index = -1;
  output.requested_pts = front.requested_pts;
  output.pts = front.pts;
  output.duration = front.duration;
  output.time_base = front.time_base;
  output.source_color = {"rgb24", "pc", "bt709", "bt709", "bt709", "unspecified"};

  for (int y = 0; y < height; ++y) {
    const auto theta = (2.0 * (y + 0.5) / height - 1.0) * kPi / 2.0;
    for (int x = 0; x < width; ++x) {
      const auto phi = (2.0 * (x + 0.5) / width - 1.0) * kPi;
      const Vec3 xyz{std::cos(theta) * std::sin(phi), std::sin(theta),
                     std::cos(theta) * std::cos(phi)};
      auto uv = xyz_to_eac(xyz, front.height);
      const auto *source = &front;
      if (uv.y >= front.height) {
        uv.y -= front.height;
        source = &rear;
      }
      const auto pixel = sample_gopro(*source, uv, overlap);
      const auto offset = (static_cast<std::size_t>(y) *
                           static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(x)) * 3U;
      for (std::size_t channel = 0; channel < pixel.size(); ++channel) {
        output.pixels[offset + channel] = static_cast<std::uint8_t>(
            std::clamp(std::lround(pixel[channel]), 0L, 255L));
      }
    }
  }
  return output;
}

} // namespace gb::media
