#pragma once

#include "gb/export/framing.hpp"
#include "gb/export/settings.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The complete gb.export/1 settings of a video export (docs/plans/export/settings.md): the model,
// how files and command-line overrides combine into it, validation, and the canonical text that
// --print-settings writes. No Qt: fonts, colours and time zones are only checked for form here.
namespace gb::exporting {

// Where overlays go: nowhere, over the picture, or on a black panel beside it.
enum class Placement { none, overlay, left, right, top, bottom };

[[nodiscard]] std::string placement_name(Placement placement);
// Also accepts "over" for overlay.
[[nodiscard]] std::optional<Placement> parse_placement(const std::string &text);
[[nodiscard]] PanelSide panel_side(Placement placement); // none for none and overlay

struct Color {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};
  std::uint8_t alpha{255};
  friend bool operator==(const Color &, const Color &) = default;
};

// "#rgb", "#rrggbb", "#rrggbbaa" (CSS order) or one of the names black, white, gray, red, orange,
// yellow, green, cyan, blue, magenta.
[[nodiscard]] std::optional<Color> parse_color(const std::string &text);
[[nodiscard]] std::string color_text(Color color); // "#rrggbb", or "#rrggbbaa" when translucent

// A ground distance with a unit: "800m", "1.2km", "0.5mi", "2000ft". Returns metres.
[[nodiscard]] std::optional<double> parse_distance(const std::string &text);
// "800m" below 10 km in whole metres, otherwise "12.5km".
[[nodiscard]] std::string distance_text(double metres);

// Where an overlay goes over the picture. A corner (or the middle of the top or bottom edge) is
// inset by the layout margin, and overlays at the same corner are stacked away from the edge. A
// free position is a point in fractions of the frame's width and height from its top left.
enum class Corner { top_left, top, top_right, bottom_left, bottom, bottom_right, free };

struct Anchor {
  Corner corner{Corner::top_left};
  double x{}; // free only: 0..1 of the frame width
  double y{}; // free only: 0..1 of the frame height
  friend bool operator==(const Anchor &, const Anchor &) = default;
};

[[nodiscard]] std::optional<Anchor> parse_anchor(const std::string &text);
[[nodiscard]] std::string anchor_text(const Anchor &anchor);

enum class Align { automatic, left, center, right };
[[nodiscard]] std::optional<Align> parse_align(const std::string &text);
[[nodiscard]] std::string align_name(Align align);

// How a field's label is drawn: above the value, to its left on the same line, or not at all.
enum class LabelPosition { above, left, none };

// The telemetry values that can be shown.
enum class Field { datetime, date, time, latitude, longitude, altitude, speed, distance };

inline constexpr std::array<Field, 8> kAllFields{Field::datetime, Field::date,      Field::time,
                                                 Field::latitude, Field::longitude, Field::altitude,
                                                 Field::speed,    Field::distance};

[[nodiscard]] std::string field_name(Field field);
// Also accepts "utc" for datetime, as gb-player 0.1 wrote it.
[[nodiscard]] std::optional<Field> parse_field(const std::string &text);
[[nodiscard]] std::string default_label(Field field);

struct FieldSettings {
  Field field{Field::speed};
  std::string label; // shown as given; empty for no label
  Anchor anchor;     // overlay only
  Align align{Align::automatic};
  std::optional<double> font_size; // overlay only: replaces [text] font_size for this field
};

struct TextSettings {
  double font_size{0.04}; // value text, fraction of the frame height; panel: the largest size
  double label_size{0.6}; // label text, relative to font_size
  LabelPosition labels{LabelPosition::above};
  Color label_color{0xc8, 0xd0, 0xd8, 255};
  Color value_color{0xff, 0xff, 0xff, 255};
  bool outline{true}; // a border around every glyph, so text stays readable on bright pictures
  Color outline_color{0, 0, 0, 255};
  double outline_width{0.08}; // fraction of each text's own size
};

enum class MapShape { square, circle };

struct MapSettings {
  bool enabled{};
  double width_metres{1000.0}; // ground distance across the map
  bool track{true};            // the route over the whole range, travelled and remaining
  MapShape shape{MapShape::square};
  double size{0.3}; // overlay: side of the map, fraction of the frame height
  Anchor anchor{Corner::top_right};
  double opacity{1.0}; // overlay only
};

enum class SpeedUnit { kmh, mph, ms, kn };
enum class AltitudeUnit { m, ft };
enum class DistanceUnit { km, mi };

struct UnitSettings {
  SpeedUnit speed{SpeedUnit::kmh};
  AltitudeUnit altitude{AltitudeUnit::m};
  DistanceUnit distance{DistanceUnit::km};
  std::string time_zone{"local"}; // local, utc, +HH:MM / -HH:MM, or an IANA name
};

[[nodiscard]] std::string speed_unit_name(SpeedUnit unit);
[[nodiscard]] std::string altitude_unit_name(AltitudeUnit unit);
[[nodiscard]] std::string distance_unit_name(DistanceUnit unit);

enum class Codec { h264, hevc };

struct OutputSettings {
  Codec codec{Codec::h264};
  int crf{20};
  std::string encoder_preset{"medium"};
  bool audio{true};
  int audio_bitrate{192000};
  // Playback speed of the output relative to real time (>= 1). Above 1, frames are dropped to
  // keep the source frame rate, and the audio is time-stretched at its pitch up to
  // kMaxAudioSpeed; faster exports have no audio track.
  double speed{1.0};
};

inline constexpr double kMaxSpeed = 1000.0;
inline constexpr double kMaxAudioSpeed = 4.0;

struct ExportSettings {
  // [range]: positions as written (gb::media::parse_position checks them).
  std::optional<std::string> start;
  std::optional<std::string> end;

  // [view]
  int mode{7};  // original
  double yaw{}; // degrees
  double pitch{};
  double roll{};
  double field_of_view{90.0};
  double zoom{1.0};
  double pan_x{};
  double pan_y{};
  std::optional<double> canvas; // default: the picture area's aspect ratio

  // [frame]
  AspectRatio aspect;
  std::optional<Rect> crop; // default: the whole canvas
  int width{1920};

  // [layout]
  Placement placement{Placement::overlay};
  double panel_size{0.32};
  double margin{0.03};

  UnitSettings units;
  TextSettings text;
  // Shown when [telemetry] or any [field-NAME] section is present, in this order.
  bool telemetry{};
  std::vector<FieldSettings> fields;
  MapSettings map;
  OutputSettings output;

  [[nodiscard]] FrameLayout frame_layout() const; // throws std::invalid_argument
  [[nodiscard]] double picture_aspect() const;
  [[nodiscard]] double canvas_aspect() const { return canvas.value_or(picture_aspect()); }
  [[nodiscard]] Rect crop_or_whole() const;
};

// One file's (or the command line's) contribution: its parsed INI and a name for messages.
struct SettingsSource {
  std::string name; // "ride.ini", "--set"
  IniDocument document;
  bool requires_schema{true};
};

class SettingsValidationError final : public std::runtime_error {
public:
  explicit SettingsValidationError(const std::string &message) : std::runtime_error(message) {}
};

// Built-in defaults, then each source in order, later values replacing earlier ones key by key.
// Every section and key is checked: an unknown one, a value of the wrong form or out of range, a
// missing or wrong schema, or a crop that does not fit the picture is a SettingsValidationError
// naming the source, line and key. `warnings` receives settings that are given but have no effect.
[[nodiscard]] ExportSettings combine_settings(const std::vector<SettingsSource> &sources,
                                              std::vector<std::string> *warnings = nullptr);

// A source holding "section.key=value" overrides, as --set gives them.
[[nodiscard]] SettingsSource override_source(const std::string &name,
                                             const std::vector<std::string> &assignments);

// The combined settings as gb.export/1 text, every key explicit, so that reading it back gives the
// same settings. `header` lines are written as comments at the top.
[[nodiscard]] std::string write_export_settings(const ExportSettings &settings,
                                                const std::vector<std::string> &header = {});

// A commented example of every section, as `gb-render --example-settings` prints it.
[[nodiscard]] std::string example_settings();

} // namespace gb::exporting
