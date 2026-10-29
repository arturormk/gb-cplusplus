#include "gb/media/media.hpp"

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
