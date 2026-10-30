#include "gb/media/media.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gb::media {
namespace {

using Color = std::array<std::uint8_t, 3>;

constexpr Color kWhite{245U, 248U, 250U};
constexpr Color kMuted{170U, 182U, 192U};
constexpr Color kAccent{61U, 220U, 151U};
constexpr Color kVector{255U, 190U, 61U};

// Five columns, seven rows, least-significant bit at the top. Unknown bytes use '?'.
std::array<std::uint8_t, 5> glyph(const char raw) {
  const auto c = raw >= 'a' && raw <= 'z' ? static_cast<char>(raw - 'a' + 'A') : raw;
  switch (c) {
  case 'A': return {0x7e, 0x09, 0x09, 0x09, 0x7e};
  case 'B': return {0x7f, 0x49, 0x49, 0x49, 0x36};
  case 'C': return {0x3e, 0x41, 0x41, 0x41, 0x22};
  case 'D': return {0x7f, 0x41, 0x41, 0x22, 0x1c};
  case 'E': return {0x7f, 0x49, 0x49, 0x49, 0x41};
  case 'F': return {0x7f, 0x09, 0x09, 0x09, 0x01};
  case 'G': return {0x3e, 0x41, 0x49, 0x49, 0x7a};
  case 'H': return {0x7f, 0x08, 0x08, 0x08, 0x7f};
  case 'I': return {0x41, 0x41, 0x7f, 0x41, 0x41};
  case 'J': return {0x20, 0x40, 0x41, 0x3f, 0x01};
  case 'K': return {0x7f, 0x08, 0x14, 0x22, 0x41};
  case 'L': return {0x7f, 0x40, 0x40, 0x40, 0x40};
  case 'M': return {0x7f, 0x02, 0x0c, 0x02, 0x7f};
  case 'N': return {0x7f, 0x04, 0x08, 0x10, 0x7f};
  case 'O': return {0x3e, 0x41, 0x41, 0x41, 0x3e};
  case 'P': return {0x7f, 0x09, 0x09, 0x09, 0x06};
  case 'Q': return {0x3e, 0x41, 0x51, 0x21, 0x5e};
  case 'R': return {0x7f, 0x09, 0x19, 0x29, 0x46};
  case 'S': return {0x46, 0x49, 0x49, 0x49, 0x31};
  case 'T': return {0x01, 0x01, 0x7f, 0x01, 0x01};
  case 'U': return {0x3f, 0x40, 0x40, 0x40, 0x3f};
  case 'V': return {0x1f, 0x20, 0x40, 0x20, 0x1f};
  case 'W': return {0x7f, 0x20, 0x18, 0x20, 0x7f};
  case 'X': return {0x63, 0x14, 0x08, 0x14, 0x63};
  case 'Y': return {0x03, 0x04, 0x78, 0x04, 0x03};
  case 'Z': return {0x61, 0x51, 0x49, 0x45, 0x43};
  case '0': return {0x3e, 0x51, 0x49, 0x45, 0x3e};
  case '1': return {0x00, 0x42, 0x7f, 0x40, 0x00};
  case '2': return {0x62, 0x51, 0x49, 0x49, 0x46};
  case '3': return {0x22, 0x41, 0x49, 0x49, 0x36};
  case '4': return {0x18, 0x14, 0x12, 0x7f, 0x10};
  case '5': return {0x2f, 0x49, 0x49, 0x49, 0x31};
  case '6': return {0x3e, 0x49, 0x49, 0x49, 0x32};
  case '7': return {0x01, 0x71, 0x09, 0x05, 0x03};
  case '8': return {0x36, 0x49, 0x49, 0x49, 0x36};
  case '9': return {0x26, 0x49, 0x49, 0x49, 0x3e};
  case ':': return {0x00, 0x36, 0x36, 0x00, 0x00};
  case '.': return {0x00, 0x60, 0x60, 0x00, 0x00};
  case ',': return {0x00, 0x40, 0x20, 0x00, 0x00};
  case '-': return {0x08, 0x08, 0x08, 0x08, 0x08};
  case '+': return {0x08, 0x08, 0x3e, 0x08, 0x08};
  case '/': return {0x20, 0x10, 0x08, 0x04, 0x02};
  case '=': return {0x14, 0x14, 0x14, 0x14, 0x14};
  case '(': return {0x00, 0x1c, 0x22, 0x41, 0x00};
  case ')': return {0x00, 0x41, 0x22, 0x1c, 0x00};
  case '[': return {0x00, 0x7f, 0x41, 0x41, 0x00};
  case ']': return {0x00, 0x41, 0x41, 0x7f, 0x00};
  case '_': return {0x40, 0x40, 0x40, 0x40, 0x40};
  case ' ': return {};
  default: return {0x02, 0x01, 0x59, 0x09, 0x06};
  }
}

void blend(RgbFrame &frame, const int x, const int y, const Color color,
           const unsigned alpha = 255U) {
  if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) return;
  const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                       static_cast<std::size_t>(x)) * 3U;
  for (std::size_t channel = 0; channel < color.size(); ++channel) {
    const auto background = static_cast<unsigned>(frame.pixels[offset + channel]);
    frame.pixels[offset + channel] = static_cast<std::uint8_t>(
        (background * (255U - alpha) + static_cast<unsigned>(color[channel]) * alpha) / 255U);
  }
}

void rectangle(RgbFrame &frame, const int left, const int top, const int width,
               const int height, const Color color, const unsigned alpha) {
  for (int y = std::max(top, 0); y < std::min(top + height, frame.height); ++y) {
    for (int x = std::max(left, 0); x < std::min(left + width, frame.width); ++x) {
      blend(frame, x, y, color, alpha);
    }
  }
}

void line(RgbFrame &frame, int x0, int y0, const int x1, const int y1,
          const Color color, const int thickness = 1) {
  const auto dx = std::abs(x1 - x0);
  const auto sx = x0 < x1 ? 1 : -1;
  const auto dy = -std::abs(y1 - y0);
  const auto sy = y0 < y1 ? 1 : -1;
  auto error = dx + dy;
  while (true) {
    for (int oy = -thickness / 2; oy <= thickness / 2; ++oy) {
      for (int ox = -thickness / 2; ox <= thickness / 2; ++ox) blend(frame, x0 + ox, y0 + oy, color);
    }
    if (x0 == x1 && y0 == y1) break;
    const auto twice = 2 * error;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}

void text(RgbFrame &frame, const int x, const int y, const std::string_view value,
          const int scale, const Color color, const std::size_t maximum_characters) {
  const auto count = std::min(value.size(), maximum_characters);
  for (std::size_t index = 0; index < count; ++index) {
    const auto columns = glyph(value[index]);
    for (int column = 0; column < 5; ++column) {
      for (int row = 0; row < 7; ++row) {
        if ((columns[static_cast<std::size_t>(column)] & (1U << row)) == 0U) continue;
        rectangle(frame, x + static_cast<int>(index) * 6 * scale + column * scale,
                  y + row * scale, scale, scale, color, 255U);
      }
    }
  }
}

void vector_gauge(RgbFrame &frame, const HudVector &value, const int left, const int top,
                  const int size, const int scale) {
  rectangle(frame, left, top, size, size, {7U, 13U, 18U}, 190U);
  const auto center_x = left + size / 2;
  const auto center_y = top + size / 2;
  const auto radius = size / 2 - 5 * scale;
  line(frame, center_x - radius, center_y, center_x + radius, center_y, kMuted);
  line(frame, center_x, center_y - radius, center_x, center_y + radius, kMuted);
  const auto magnitude = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  if (std::isfinite(magnitude) && magnitude > 0.0L) {
    const auto end_x = center_x + static_cast<int>(std::lround(value.x / magnitude * radius));
    const auto end_y = center_y - static_cast<int>(std::lround(value.y / magnitude * radius));
    line(frame, center_x, center_y, end_x, end_y, kVector, std::max(1, scale));
    rectangle(frame, end_x - scale, end_y - scale, scale * 3, scale * 3, kVector, 255U);
  }
  const auto max_chars = static_cast<std::size_t>(std::max(1, size / (6 * scale)));
  text(frame, left + 4 * scale, top + 3 * scale, value.label, scale, kAccent, max_chars);
  std::string z_label = "Z " + std::to_string(static_cast<double>(value.z));
  if (!value.unit.empty()) z_label += " " + value.unit;
  text(frame, left + 4 * scale, top + size - 11 * scale, z_label, scale, kWhite, max_chars);
}

} // namespace

void draw_hud(RgbFrame &frame, const HudOverlay &overlay) {
  if (frame.width <= 0 || frame.height <= 0 ||
      frame.pixels.size() != static_cast<std::size_t>(frame.width) *
                                 static_cast<std::size_t>(frame.height) * 3U) {
    throw MediaError(ErrorKind::invalid_argument, "HUD requires a packed RGB24 frame");
  }
  const auto previous_scale = std::max(1, std::min(frame.width / 960, frame.height / 540) + 1);
  const auto scale = std::max(1, (previous_scale + 1) / 2) *
                     std::max(1, overlay.font_scale_multiplier);
  const auto margin = 8 * scale;
  const auto row_height = 10 * scale;
  const auto gauge_columns = std::min<std::size_t>(overlay.vectors.size(), 2U);
  const auto gauge_rows = gauge_columns == 0U
                              ? 0U
                              : (overlay.vectors.size() + gauge_columns - 1U) / gauge_columns;
  const auto gauge_height = gauge_rows == 0U
                                ? 0
                                : std::max(0, (frame.height - 2 * margin -
                                               static_cast<int>(gauge_rows - 1U) * margin) /
                                              static_cast<int>(gauge_rows));
  const auto gauge_size = std::min(90 * scale, gauge_height);
  const auto reserved = gauge_columns == 0U || gauge_size == 0
                            ? 0
                            : static_cast<int>(gauge_columns) * (gauge_size + margin);
  const auto available_panel_width = std::max(0, frame.width - 2 * margin - reserved);
  const auto longest_line = std::max_element(
      overlay.lines.begin(), overlay.lines.end(),
      [](const auto &left, const auto &right) { return left.size() < right.size(); });
  const auto compact_width = longest_line == overlay.lines.end()
                                 ? 0
                                 : static_cast<int>(longest_line->size()) * 6 * scale + 2 * margin;
  const auto panel_width = overlay.compact
                               ? std::min(available_panel_width, compact_width)
                               : available_panel_width;
  const auto max_rows = frame.height > 2 * margin
                            ? static_cast<std::size_t>((frame.height - 2 * margin) / row_height)
                            : 0U;
  const auto required_columns = max_rows == 0U
                                    ? 0U
                                    : (overlay.lines.size() + max_rows - 1U) / max_rows;
  const auto maximum_columns = panel_width > 0
                                   ? std::max(1, panel_width / (30 * 6 * scale))
                                   : 0;
  const auto text_columns = std::min<std::size_t>(required_columns,
                                                   static_cast<std::size_t>(maximum_columns));
  const auto column_width = text_columns == 0U
                                ? 0
                                : panel_width / static_cast<int>(text_columns);
  const auto max_chars = column_width > margin
                             ? static_cast<std::size_t>((column_width - margin) / (6 * scale))
                             : 0U;
  const auto visible = std::min(overlay.lines.size(), max_rows * text_columns);
  if (visible != 0U && max_chars != 0U) {
    const auto rows = std::min(max_rows, visible);
    rectangle(frame, margin, margin, panel_width, static_cast<int>(rows) * row_height + margin,
              {7U, 13U, 18U}, 190U);
    rectangle(frame, margin, margin, 3 * scale,
              static_cast<int>(rows) * row_height + margin, kAccent, 255U);
    for (std::size_t index = 0; index < visible; ++index) {
      const auto column = index / max_rows;
      const auto row = index % max_rows;
      text(frame, margin * 2 + static_cast<int>(column) * column_width,
           margin + static_cast<int>(row) * row_height,
           overlay.lines[index], scale, index == 0U ? kAccent : kWhite, max_chars);
    }
  }
  for (std::size_t index = 0; index < overlay.vectors.size() && gauge_size > 0; ++index) {
    const auto column = index % gauge_columns;
    const auto row = index / gauge_columns;
    const auto left = frame.width - margin -
                      static_cast<int>(gauge_columns - column) * (gauge_size + margin);
    const auto top = margin + static_cast<int>(row) * (gauge_size + margin);
    vector_gauge(frame, overlay.vectors[index], left, top, gauge_size, scale);
  }
}

} // namespace gb::media
