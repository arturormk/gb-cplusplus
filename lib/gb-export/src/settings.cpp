#include "gb/export/settings.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <sstream>

namespace gb::exporting {
namespace {

bool name_character(const char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
}

bool valid_name(const std::string_view name) {
  return !name.empty() && name.size() <= 64U &&
         std::all_of(name.begin(), name.end(), name_character);
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
    text.remove_suffix(1);
  return text;
}

// Fixed-point text with `decimals` places, independent of the locale. Negative zero is written as
// zero.
std::string fixed(double value, const int decimals) {
  if (!std::isfinite(value))
    value = 0.0;
  std::array<char, 64> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                    std::chars_format::fixed, decimals);
  std::string text(buffer.data(), result.ptr);
  if (text.front() == '-' && text.find_first_not_of("0.", 1U) == std::string::npos) {
    text.erase(0, 1);
  }
  return text;
}

// Up to `decimals` places, trailing zeros dropped but one decimal kept: 4.0, -12.5, 95.125.
std::string number(const double value, const int decimals) {
  auto text = fixed(value, decimals);
  const auto dot = text.find('.');
  if (dot == std::string::npos)
    return text;
  while (text.size() > dot + 2U && text.back() == '0')
    text.pop_back();
  return text;
}

double read_back(const std::string &text) {
  double value{};
  std::from_chars(text.data(), text.data() + text.size(), value);
  return value;
}

std::string one_line(std::string text) {
  std::replace_if(
      text.begin(), text.end(), [](const char c) { return c == '\n' || c == '\r'; }, ' ');
  return text;
}

} // namespace

SettingsError::SettingsError(const int line, const std::string &message)
    : std::runtime_error(line > 0 ? "line " + std::to_string(line) + ": " + message : message),
      line_(line) {}

const IniEntry *IniDocument::find(const std::string_view section,
                                  const std::string_view key) const {
  for (const auto &entry : entries) {
    if (entry.section == section && entry.key == key)
      return &entry;
  }
  return nullptr;
}

IniDocument parse_ini(const std::string_view text) {
  if (text.size() > kMaximumSettingsFile)
    throw SettingsError(0, "settings file is larger than 1 MiB");
  IniDocument document;
  std::string section;
  int line_number = 0;
  std::size_t start = 0;
  while (start < text.size()) {
    ++line_number;
    auto end = text.find('\n', start);
    if (end == std::string_view::npos)
      end = text.size();
    auto line = text.substr(start, end - start);
    start = end + 1U;
    if (line.size() > kMaximumSettingsLine)
      throw SettingsError(line_number, "line is longer than 4 KiB");
    if (line.find('\0') != std::string_view::npos)
      throw SettingsError(line_number, "NUL byte");
    if (const auto comment = line.find_first_of(";#"); comment != std::string_view::npos)
      line = line.substr(0, comment);
    line = trim(line);
    if (line.empty())
      continue;
    if (line.front() == '[') {
      if (line.back() != ']')
        throw SettingsError(line_number, "section header without ']'");
      const auto name = trim(line.substr(1, line.size() - 2U));
      if (!valid_name(name))
        throw SettingsError(line_number, "invalid section name");
      section = std::string(name);
      if (std::find(document.sections.begin(), document.sections.end(), section) ==
          document.sections.end()) {
        document.sections.push_back(section);
      }
      continue;
    }
    const auto equals = line.find('=');
    if (equals == std::string_view::npos)
      throw SettingsError(line_number, "expected 'key = value'");
    const auto key = trim(line.substr(0, equals));
    if (!valid_name(key))
      throw SettingsError(line_number, "invalid key name");
    if (document.find(section, key) != nullptr) {
      throw SettingsError(line_number,
                          "'" + std::string(key) + "' is given twice in " +
                              (section.empty() ? "the file header" : "[" + section + "]"));
    }
    document.entries.push_back(
        {section, std::string(key), std::string(trim(line.substr(equals + 1U))), line_number});
  }
  return document;
}

std::string write_capture_settings(const CaptureSettings &settings) {
  std::ostringstream out;
  out << "; from " << one_line(settings.generator) << ", " << one_line(settings.source) << '\n';
  out << "schema = " << kSettingsSchema << '\n';

  if (settings.has_range) {
    out << "\n[range]\n";
    if (settings.start)
      out << "start = " << *settings.start << '\n';
    else
      out << "start = " << settings.current << "   ; the current position (Set start to fix it)\n";
    if (settings.end)
      out << "end   = " << *settings.end << '\n';
    else
      out << "; end = (Set end in gb-player)\n";
  }

  out << "\n[view]\n";
  out << "mode   = " << view_mode_name(settings.mode) << '\n';
  if (is_spherical_mode(settings.mode)) {
    out << "yaw    = " << number(settings.yaw, 3) << '\n';
    out << "pitch  = " << number(settings.pitch, 3) << '\n';
    out << "roll   = " << number(settings.roll, 3) << '\n';
    out << "fov    = " << number(settings.field_of_view, 3) << '\n';
  } else {
    out << "zoom   = " << number(settings.zoom, 4) << '\n';
    out << "pan    = " << number(settings.pan_x, 4) << ", " << number(settings.pan_y, 4) << '\n';
  }
  const auto canvas = fixed(settings.canvas, 4);
  out << "canvas = " << canvas << '\n';

  out << "\n[frame]\n";
  out << "aspect = " << settings.aspect.text() << '\n';
  const auto picture = picture_aspect(settings.aspect, settings.panel, settings.panel_size)
                           .value_or(settings.aspect.value());
  std::string crop;
  for (const auto decimals : {4, 6, 8}) {
    const std::array<std::string, 4> parts{
        fixed(settings.crop.x, decimals), fixed(settings.crop.y, decimals),
        fixed(settings.crop.width, decimals), fixed(settings.crop.height, decimals)};
    crop = parts[0] + ", " + parts[1] + ", " + parts[2] + ", " + parts[3];
    const Rect written{read_back(parts[0]), read_back(parts[1]), read_back(parts[2]),
                       read_back(parts[3])};
    if (crop_fits(read_back(canvas), written, picture))
      break;
  }
  out << "crop   = " << crop << '\n';
  out << "width  = " << settings.width << '\n';

  if (settings.panel != PanelSide::none) {
    out << "\n[layout]\n";
    out << "placement  = beside\n";
    out << "panel      = " << panel_side_name(settings.panel) << '\n';
    out << "panel_size = " << number(settings.panel_size, 3) << '\n';
    if (settings.telemetry) {
      out << "\n[units]\n";
      out << "speed     = km/h\n";
      out << "altitude  = m\n";
      out << "time_zone = local\n";
    }
    if (settings.map_width_metres) {
      out << "\n[map]\n";
      out << "scale = " << std::max(1L, std::lround(*settings.map_width_metres))
          << "m   ; ground distance across the map\n";
    }
    if (settings.telemetry) {
      out << "\n[telemetry]\n";
      out << "fields = utc, latitude, longitude, altitude, speed\n";
    }
  }
  return out.str();
}

} // namespace gb::exporting
