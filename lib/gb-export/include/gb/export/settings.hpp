#pragma once

#include "gb/export/framing.hpp"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The gb.export/1 INI settings format (docs/plans/export/settings.md): the syntax reader, and the
// writer for the parts gb-player's Capture mode knows about ([range], [view], [frame], [layout]).
namespace gb::exporting {

inline constexpr std::string_view kSettingsSchema = "gb.export/1";
inline constexpr std::size_t kMaximumSettingsLine = 4096;
inline constexpr std::size_t kMaximumSettingsFile = 1024 * 1024;

class SettingsError final : public std::runtime_error {
public:
  SettingsError(int line, const std::string &message);
  [[nodiscard]] int line() const noexcept { return line_; }

private:
  int line_;
};

struct IniEntry {
  std::string section; // empty before the first section header
  std::string key;
  std::string value;
  int line{};
};

struct IniDocument {
  std::vector<std::string> sections; // in order of first appearance
  std::vector<IniEntry> entries;     // in file order

  [[nodiscard]] const IniEntry *find(std::string_view section, std::string_view key) const;
};

// Reads the INI syntax: `[section]` headers, `key = value` lines, comments from ';' to the end of
// the line, or from a '#' that starts the line or is followed by a space or the end of the line
// (so `#ffd43b` is a value), blank lines. Keys and section names are letters, digits, '_' and '-'.
// A key given twice in one section, a line over 4 KiB, a file over 1 MiB, a NUL byte or a line that
// is none of these is a SettingsError naming the line. What the sections and keys mean is not
// checked here.
[[nodiscard]] IniDocument parse_ini(std::string_view text);

// The framing gb-player's Capture mode shows as settings.
struct CaptureSettings {
  std::string generator;            // "gb-player 0.1.52"
  std::string source;               // file name of the recording or photo
  bool has_range{};                 // false for a photo
  std::optional<std::string> start; // positions, e.g. "c2:00:05:31.400"
  std::optional<std::string> end;
  std::string current; // the current position, written as the start when none is set

  int mode{};   // VideoView::Mode
  double yaw{}; // degrees, spherical views
  double pitch{};
  double roll{};
  double field_of_view{90.0};
  double zoom{1.0}; // flat views
  double pan_x{};
  double pan_y{};
  double canvas{16.0 / 9.0};

  AspectRatio aspect;
  Rect crop;
  int width{1920};
  PanelSide panel{PanelSide::none};
  double panel_size{0.32};
  // In the panel: the map, as the ground distance across it in metres, and the telemetry text
  // (date and time, latitude, longitude, altitude, speed). Written only with a panel.
  std::optional<double> map_width_metres;
  bool telemetry{};
};

// Writes the settings as gb.export/1 INI text. Numbers use the C locale. The crop is written with
// enough decimals for it to keep the picture's aspect ratio within 0.1 % when read back.
[[nodiscard]] std::string write_capture_settings(const CaptureSettings &settings);

} // namespace gb::exporting
