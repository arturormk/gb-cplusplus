#include "gb/media/media.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {

gb::media::RgbFrame solid(const std::int32_t width, const std::int32_t height,
                          const std::uint8_t red, const std::uint8_t green,
                          const std::uint8_t blue) {
  gb::media::RgbFrame frame;
  frame.width = width;
  frame.height = height;
  frame.pixels.resize(static_cast<std::size_t>(width) *
                      static_cast<std::size_t>(height) * 3U);
  for (std::size_t offset = 0; offset < frame.pixels.size(); offset += 3U) {
    frame.pixels[offset] = red;
    frame.pixels[offset + 1U] = green;
    frame.pixels[offset + 2U] = blue;
  }
  frame.pts = 100;
  frame.requested_pts = 90;
  frame.duration = 40;
  frame.time_base = {1, 1000};
  return frame;
}

gb::media::RgbFrame coordinate_map(const std::int32_t width, const std::int32_t height) {
  auto frame = solid(width, height, 0U, 0U, 0U);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(x)) *
                          3U;
      frame.pixels[offset] = static_cast<std::uint8_t>(x * 255 / (width - 1));
      frame.pixels[offset + 1U] = static_cast<std::uint8_t>(y * 255 / (height - 1));
    }
  }
  return frame;
}

gb::media::RgbFrame packed_faces() {
  auto frame = solid(200, 64, 0U, 0U, 0U);
  const std::array<std::array<std::uint8_t, 3>, 3> colors{{
      {255U, 0U, 0U}, {0U, 255U, 0U}, {0U, 0U, 255U}}};
  const std::array<int, 4> boundaries{0, 68, 132, 200};
  for (std::size_t region = 0; region < colors.size(); ++region) {
    for (int y = 0; y < frame.height; ++y) {
      for (int x = boundaries[region]; x < boundaries[region + 1U]; ++x) {
        const auto offset = (static_cast<std::size_t>(y) *
                                 static_cast<std::size_t>(frame.width) +
                             static_cast<std::size_t>(x)) *
                            3U;
        for (std::size_t channel = 0; channel < 3U; ++channel) {
          frame.pixels[offset + channel] = colors[region][channel];
        }
      }
    }
  }
  return frame;
}

std::uint8_t channel_at(const gb::media::RgbFrame &frame, const int x, const int y,
                        const std::size_t channel) {
  return frame.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                       static_cast<std::size_t>(x)) *
                          3U +
                      channel];
}

void require(const bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

} // namespace

int main() {
  try {
    const auto front = solid(200, 64, 255U, 0U, 0U);
    const auto rear = solid(200, 64, 0U, 0U, 255U);
    const auto output = gb::media::reconstruct_equirectangular(front, rear, 128, 64, 8);
    require(output.width == 128 && output.height == 64, "unexpected output geometry");
    require(output.pixels.size() == 128U * 64U * 3U, "unexpected buffer size");
    std::size_t red_pixels{};
    std::size_t blue_pixels{};
    for (std::size_t offset = 0; offset < output.pixels.size(); offset += 3U) {
      const auto red = output.pixels[offset];
      const auto green = output.pixels[offset + 1U];
      const auto blue = output.pixels[offset + 2U];
      require(green == 0U, "projection changed a constant channel");
      require((red == 255U && blue == 0U) || (red == 0U && blue == 255U),
              "projection mixed independent EAC halves");
      red_pixels += red == 255U ? 1U : 0U;
      blue_pixels += blue == 255U ? 1U : 0U;
    }
    require(red_pixels > 0U && blue_pixels > 0U, "both source streams must be represented");

    auto first_faces = packed_faces();
    auto second_faces = packed_faces();
    for (std::size_t offset = 0; offset < second_faces.pixels.size(); offset += 3U) {
      second_faces.pixels[offset] = static_cast<std::uint8_t>(255U - second_faces.pixels[offset]);
      second_faces.pixels[offset + 1U] =
          static_cast<std::uint8_t>(255U - second_faces.pixels[offset + 1U]);
      second_faces.pixels[offset + 2U] =
          static_cast<std::uint8_t>(255U - second_faces.pixels[offset + 2U]);
    }
    const auto six_faces =
        gb::media::reconstruct_equirectangular(first_faces, second_faces, 256, 128, 8);
    std::array<std::size_t, 6> face_color_counts{};
    const std::array<std::array<std::uint8_t, 3>, 6> face_colors{{
        {255U, 0U, 0U}, {0U, 255U, 0U}, {0U, 0U, 255U},
        {0U, 255U, 255U}, {255U, 0U, 255U}, {255U, 255U, 0U}}};
    for (std::size_t offset = 0; offset < six_faces.pixels.size(); offset += 3U) {
      for (std::size_t color = 0; color < face_colors.size(); ++color) {
        if (six_faces.pixels[offset] == face_colors[color][0] &&
            six_faces.pixels[offset + 1U] == face_colors[color][1] &&
            six_faces.pixels[offset + 2U] == face_colors[color][2]) {
          ++face_color_counts[color];
        }
      }
    }
    for (const auto count : face_color_counts) {
      require(count > 100U, "one of the six packed faces was not reconstructed");
    }

    const auto map = coordinate_map(360, 180);
    const auto forward = gb::media::project_equirectangular(
        map, gb::media::ProjectionMode::rectilinear, 101, 101, 0.0L, 0.0L, 0.0L, 90.0L);
    require(std::abs(static_cast<int>(channel_at(forward, 50, 50, 0U)) - 128) <= 2,
            "rectilinear center longitude is incorrect");
    require(std::abs(static_cast<int>(channel_at(forward, 50, 50, 1U)) - 128) <= 2,
            "rectilinear center latitude is incorrect");
    const auto east = gb::media::project_equirectangular(
        map, gb::media::ProjectionMode::rectilinear, 101, 101, 90.0L, 0.0L, 0.0L, 90.0L);
    require(std::abs(static_cast<int>(channel_at(east, 50, 50, 0U)) - 191) <= 2,
            "positive yaw does not look east");
    const auto up = gb::media::project_equirectangular(
        map, gb::media::ProjectionMode::rectilinear, 101, 101, 0.0L, 90.0L, 0.0L, 90.0L);
    require(channel_at(up, 50, 50, 1U) <= 2U, "positive pitch does not look up");
    const auto fisheye = gb::media::project_equirectangular(
        map, gb::media::ProjectionMode::fisheye, 100, 100, 0.0L, 0.0L, 0.0L, 180.0L);
    require(channel_at(fisheye, 0, 0, 0U) == 0U && channel_at(fisheye, 0, 0, 1U) == 0U,
            "fisheye outside-circle pixels are not black");
    for (const auto mode : {gb::media::ProjectionMode::stereographic,
                            gb::media::ProjectionMode::little_planet}) {
      const auto projected = gb::media::project_equirectangular(
          map, mode, 100, 100, 0.0L, 0.0L, 0.0L, 180.0L);
      require(projected.width == 100 && projected.height == 100 && !projected.pixels.empty(),
              "projection mode did not produce the requested output");
    }
    require(gb::media::projection_mode_name(gb::media::ProjectionMode::little_planet) ==
                "little_planet",
            "projection mode name changed");

    auto hud_frame = solid(320, 180, 20U, 30U, 40U);
    const auto before = hud_frame.pixels;
    gb::media::HudOverlay hud;
    hud.lines = {"GB-FRAMES HUD", "TIME 00:00:01.000", "METADATA N/A"};
    hud.vectors.push_back({"GRAV", 1.0L, -2.0L, 9.8L, "m/s2"});
    gb::media::draw_hud(hud_frame, hud);
    require(hud_frame.pixels != before, "HUD did not alter the frame");
    require(hud_frame.pixels.size() == before.size(), "HUD changed the buffer geometry");

    auto invalid_hud = hud_frame;
    invalid_hud.pixels.pop_back();
    bool invalid_hud_rejected{};
    try {
      gb::media::draw_hud(invalid_hud, hud);
    } catch (const gb::media::MediaError &error) {
      invalid_hud_rejected = error.kind() == gb::media::ErrorKind::invalid_argument;
    }
    require(invalid_hud_rejected, "invalid HUD target was not rejected");

    auto mismatched = rear;
    mismatched.width = 199;
    bool rejected{};
    try {
      static_cast<void>(gb::media::reconstruct_equirectangular(front, mismatched, 64, 32, 8));
    } catch (const gb::media::MediaError &error) {
      rejected = error.kind() == gb::media::ErrorKind::invalid_argument;
    }
    require(rejected, "mismatched source geometry was not rejected");
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "media test failure: " << error.what() << '\n';
    return 1;
  }
}
