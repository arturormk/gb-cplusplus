#include "gb/export/export_settings.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace gb::exporting {
namespace {

std::string trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
    text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
    text.remove_suffix(1);
  return std::string(text);
}

std::string lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

std::vector<std::string> split_list(const std::string &text) {
  std::vector<std::string> items;
  std::size_t start = 0;
  while (true) {
    const auto comma = text.find(',', start);
    items.push_back(trim(std::string_view(text).substr(
        start, comma == std::string::npos ? std::string::npos : comma - start)));
    if (comma == std::string::npos)
      break;
    start = comma + 1U;
  }
  return items;
}

std::optional<double> parse_number(const std::string &text) {
  const auto value = trim(text);
  if (value.empty())
    return std::nullopt;
  double number{};
  const auto *begin = value.data();
  if (*begin == '+')
    ++begin;
  const auto *end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, number);
  if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(number))
    return std::nullopt;
  return number;
}

std::optional<int> parse_integer(const std::string &text) {
  const auto value = trim(text);
  int number{};
  const auto *end = value.data() + value.size();
  const auto result = std::from_chars(value.data(), end, number);
  if (value.empty() || result.ec != std::errc{} || result.ptr != end)
    return std::nullopt;
  return number;
}

std::optional<bool> parse_bool(const std::string &text) {
  const auto value = lower(trim(text));
  if (value == "yes" || value == "true")
    return true;
  if (value == "no" || value == "false")
    return false;
  return std::nullopt;
}

// Fixed-point text, locale-independent, trailing zeros dropped but one decimal kept.
std::string number(double value, const int decimals) {
  if (!std::isfinite(value))
    value = 0.0;
  std::array<char, 64> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                    std::chars_format::fixed, decimals);
  std::string text(buffer.data(), result.ptr);
  if (text.front() == '-' && text.find_first_not_of("0.", 1U) == std::string::npos)
    text.erase(0, 1);
  const auto dot = text.find('.');
  if (dot != std::string::npos) {
    while (text.size() > dot + 2U && text.back() == '0')
      text.pop_back();
  }
  return text;
}

int hex_digit(const char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

constexpr std::array<std::pair<std::string_view, Corner>, 6> kCorners{{
    {"top-left", Corner::top_left},
    {"top", Corner::top},
    {"top-right", Corner::top_right},
    {"bottom-left", Corner::bottom_left},
    {"bottom", Corner::bottom},
    {"bottom-right", Corner::bottom_right},
}};

constexpr std::array<std::string_view, 8> kFieldNames{"datetime",  "date",     "time",  "latitude",
                                                      "longitude", "altitude", "speed", "distance"};

const std::set<std::string> &known_sections() {
  static const std::set<std::string> sections{"",      "range", "view",      "frame", "layout",
                                              "units", "text",  "telemetry", "map",   "output"};
  return sections;
}

// Accumulates the settings while the sources are read.
class Combiner {
public:
  explicit Combiner(std::vector<std::string> *warnings) : warnings_(warnings) {}

  void read(const SettingsSource &source) {
    source_ = &source;
    const auto *schema = source.document.find("", "schema");
    if (source.requires_schema) {
      if (schema == nullptr)
        fail(0, "", "schema", "'schema = gb.export/1' is required at the top of the file");
    }
    std::set<std::string> map_keys;
    for (const auto &section : source.document.sections) {
      if (section == "map") {
        map_present_ = true;
      } else if (section == "telemetry") {
        telemetry_present_ = true;
      } else if (section.rfind("field-", 0) == 0) {
        telemetry_present_ = true;
        const auto field = parse_field(section.substr(6));
        if (!field) {
          fail(line_of_section(section), "", "",
               "unknown field section [" + section + "]; fields are " + field_list());
        }
        custom(*field);
      } else if (known_sections().count(section) == 0U) {
        fail(line_of_section(section), "", "", "unknown section [" + section + "]");
      }
    }
    for (const auto &entry : source.document.entries)
      apply(entry);
    if (source.document.find("map", "width") != nullptr &&
        source.document.find("map", "scale") != nullptr) {
      fail(source.document.find("map", "scale")->line, "map", "scale",
           "'scale' is another name for 'width'; give only one");
    }
  }

  ExportSettings finish() {
    auto &s = settings_;
    // Placement: "beside" with a separate panel key is the form gb-player 0.1 wrote.
    if (raw_placement_ == "beside") {
      s.placement = panel_.value_or(Placement::right);
    } else if (panel_ && warnings_ != nullptr) {
      warnings_->push_back("[layout] panel has no effect with placement = " +
                           placement_name(s.placement));
    }
    s.map.enabled = map_present_ && map_enabled_.value_or(true);
    s.telemetry = telemetry_present_ && telemetry_enabled_.value_or(true);
    if (s.telemetry) {
      std::vector<Field> order;
      if (list_) {
        order = *list_;
      } else if (custom_order_.empty()) {
        order = {Field::datetime, Field::latitude, Field::longitude, Field::altitude, Field::speed};
      }
      for (const auto field : custom_order_) {
        if (std::find(order.begin(), order.end(), field) == order.end())
          order.push_back(field);
      }
      for (const auto field : order)
        s.fields.push_back(field_settings(field));
    }
    if (s.placement == Placement::none && (s.map.enabled || s.telemetry) && warnings_ != nullptr)
      warnings_->push_back("placement = none: the map and telemetry are not drawn");

    // Geometry that depends on several keys.
    const auto picture = exporting::picture_aspect(s.aspect, panel_side(s.placement), s.panel_size);
    if (!picture)
      throw SettingsValidationError(
          "[layout] panel_size: the panel leaves no room for the picture");
    try {
      (void)exporting::frame_layout(s.aspect, s.width, panel_side(s.placement), s.panel_size);
    } catch (const std::invalid_argument &error) {
      throw SettingsValidationError(std::string("[frame] width: ") + error.what());
    }
    if (s.crop) {
      const auto canvas = s.canvas.value_or(*picture);
      if (!crop_fits(canvas, *s.crop, *picture)) {
        throw SettingsValidationError(
            "[frame] crop: the crop must lie inside the canvas and have the picture's aspect ratio "
            "(" +
            number(*picture, 4) + ", canvas " + number(canvas, 4) + ") within 0.1 %; it has " +
            number(crop_aspect(canvas, *s.crop), 4));
      }
    }
    return s;
  }

private:
  [[noreturn]] void fail(const int line, const std::string &section, const std::string &key,
                         const std::string &message) const {
    std::string where = source_ != nullptr ? source_->name : std::string("settings");
    if (line > 0)
      where += ":" + std::to_string(line);
    std::string what;
    if (!section.empty() || !key.empty()) {
      what = section.empty() ? key : "[" + section + "]" + (key.empty() ? "" : " " + key);
      what += ": ";
    }
    throw SettingsValidationError(where + ": " + what + message);
  }

  int line_of_section(const std::string &section) const {
    for (const auto &entry : source_->document.entries) {
      if (entry.section == section)
        return entry.line;
    }
    return 0;
  }

  static std::string field_list() {
    std::string list;
    for (const auto name : kFieldNames)
      list += (list.empty() ? "" : ", ") + std::string(name);
    return list;
  }

  FieldSettings &custom(const Field field) {
    auto found = custom_.find(field);
    if (found == custom_.end()) {
      FieldSettings settings;
      settings.field = field;
      settings.label = default_label(field);
      settings.anchor = Anchor{Corner::bottom_left};
      found = custom_.emplace(field, settings).first;
      custom_order_.push_back(field);
    }
    return found->second;
  }

  FieldSettings field_settings(const Field field) {
    if (const auto found = custom_.find(field); found != custom_.end())
      return found->second;
    FieldSettings settings;
    settings.field = field;
    settings.label = default_label(field);
    settings.anchor = Anchor{Corner::bottom_left};
    return settings;
  }

  double number_in(const IniEntry &e, const double low, const double high) const {
    const auto value = parse_number(e.value);
    if (!value)
      fail(e.line, e.section, e.key, "expected a number, got '" + e.value + "'");
    if (*value < low || *value > high) {
      fail(e.line, e.section, e.key,
           e.value + " is out of range " + number(low, 4) + " to " + number(high, 4));
    }
    return *value;
  }

  int integer_in(const IniEntry &e, const int low, const int high) const {
    const auto value = parse_integer(e.value);
    if (!value)
      fail(e.line, e.section, e.key, "expected a whole number, got '" + e.value + "'");
    if (*value < low || *value > high) {
      fail(e.line, e.section, e.key,
           e.value + " is out of range " + std::to_string(low) + " to " + std::to_string(high));
    }
    return *value;
  }

  bool boolean(const IniEntry &e) const {
    const auto value = parse_bool(e.value);
    if (!value)
      fail(e.line, e.section, e.key, "expected yes or no, got '" + e.value + "'");
    return *value;
  }

  Color color(const IniEntry &e) const {
    const auto value = parse_color(e.value);
    if (!value)
      fail(e.line, e.section, e.key,
           "expected a colour such as #ffd43b or white, got '" + e.value + "'");
    return *value;
  }

  Anchor anchor(const IniEntry &e) const {
    const auto value = parse_anchor(e.value);
    if (!value) {
      fail(e.line, e.section, e.key,
           "expected top-left, top, top-right, bottom-left, bottom, bottom-right, or x, y in 0..1, "
           "got '" +
               e.value + "'");
    }
    return *value;
  }

  double distance(const IniEntry &e, const double low, const double high) const {
    const auto value = parse_distance(e.value);
    if (!value)
      fail(e.line, e.section, e.key,
           "expected a distance with a unit (800m, 1.2km, 0.5mi, 2000ft), got '" + e.value + "'");
    if (*value < low || *value > high)
      fail(e.line, e.section, e.key,
           e.value + " is out of range " + distance_text(low) + " to " + distance_text(high));
    return *value;
  }

  [[noreturn]] void unknown(const IniEntry &e) const {
    fail(e.line, e.section, e.key, "unknown key");
  }

  template <typename T, std::size_t N>
  T choice(const IniEntry &e, const std::array<std::pair<std::string_view, T>, N> &choices) const {
    const auto value = lower(trim(e.value));
    std::string names;
    for (const auto &[name, item] : choices) {
      if (value == name)
        return item;
      names += (names.empty() ? "" : ", ") + std::string(name);
    }
    fail(e.line, e.section, e.key, "expected one of " + names + ", got '" + e.value + "'");
  }

  void apply(const IniEntry &e) {
    auto &s = settings_;
    const auto &k = e.key;
    if (e.section.empty()) {
      if (k != "schema")
        unknown(e);
      if (e.value != kSettingsSchema)
        fail(e.line, "", "schema",
             "this is " + e.value + "; gb-render reads " + std::string(kSettingsSchema));
    } else if (e.section == "range") {
      if (k == "start")
        s.start = e.value;
      else if (k == "end")
        s.end = e.value;
      else
        unknown(e);
    } else if (e.section == "view") {
      if (k == "mode") {
        const auto mode = parse_view_mode(e.value);
        if (!mode)
          fail(e.line, e.section, k,
               "unknown view '" + e.value +
                   "'; views are rectilinear, fisheye, stereographic, little-planet, panorama, "
                   "raw-1, raw-2, original");
        s.mode = *mode;
      } else if (k == "yaw") {
        s.yaw = number_in(e, -360.0, 360.0);
      } else if (k == "pitch") {
        s.pitch = number_in(e, -90.0, 90.0);
      } else if (k == "roll") {
        s.roll = number_in(e, -180.0, 180.0);
      } else if (k == "fov") {
        s.field_of_view = number_in(e, 1.0, 360.0);
      } else if (k == "zoom") {
        s.zoom = number_in(e, 0.05, 50.0);
      } else if (k == "pan") {
        const auto parts = split_list(e.value);
        const auto x = parts.size() == 2U ? parse_number(parts[0]) : std::nullopt;
        const auto y = parts.size() == 2U ? parse_number(parts[1]) : std::nullopt;
        if (!x || !y)
          fail(e.line, e.section, k, "expected 'x, y', got '" + e.value + "'");
        s.pan_x = *x;
        s.pan_y = *y;
      } else if (k == "canvas") {
        s.canvas = number_in(e, 0.1, 10.0);
      } else {
        unknown(e);
      }
    } else if (e.section == "frame") {
      if (k == "aspect") {
        const auto aspect = parse_aspect(e.value);
        if (!aspect)
          fail(e.line, e.section, k,
               "expected 16:9, 4:3, 1:1, 9:16 or W:H between 1:10 and 10:1, got '" + e.value + "'");
        s.aspect = *aspect;
      } else if (k == "crop") {
        const auto parts = split_list(e.value);
        std::array<double, 4> values{};
        bool ok = parts.size() == 4U;
        for (std::size_t index = 0; ok && index < 4U; ++index) {
          const auto value = parse_number(parts[index]);
          ok = value.has_value();
          if (ok)
            values[index] = *value;
        }
        if (!ok)
          fail(e.line, e.section, k,
               "expected 'x, y, w, h' in canvas units, got '" + e.value + "'");
        s.crop = Rect{values[0], values[1], values[2], values[3]};
      } else if (k == "width") {
        s.width = integer_in(e, 16, 16384);
      } else {
        unknown(e);
      }
    } else if (e.section == "layout") {
      if (k == "placement") {
        const auto value = lower(trim(e.value));
        if (value == "beside") {
          raw_placement_ = value;
        } else {
          const auto placement = parse_placement(value);
          if (!placement)
            fail(e.line, e.section, k,
                 "expected none, overlay, left, right, top or bottom, got '" + e.value + "'");
          raw_placement_ = value;
          s.placement = *placement;
        }
      } else if (k == "panel") {
        const auto side = parse_placement(lower(trim(e.value)));
        if (!side || panel_side(*side) == PanelSide::none)
          fail(e.line, e.section, k, "expected left, right, top or bottom, got '" + e.value + "'");
        panel_ = *side;
      } else if (k == "panel_size") {
        s.panel_size = number_in(e, kMinimumPanelSize, kMaximumPanelSize);
      } else if (k == "margin") {
        s.margin = number_in(e, 0.0, 0.25);
      } else {
        unknown(e);
      }
    } else if (e.section == "units") {
      if (k == "speed") {
        s.units.speed = choice(
            e, std::array<std::pair<std::string_view, SpeedUnit>, 4>{{{"km/h", SpeedUnit::kmh},
                                                                      {"mph", SpeedUnit::mph},
                                                                      {"m/s", SpeedUnit::ms},
                                                                      {"kn", SpeedUnit::kn}}});
      } else if (k == "altitude") {
        s.units.altitude = choice(e, std::array<std::pair<std::string_view, AltitudeUnit>, 2>{
                                         {{"m", AltitudeUnit::m}, {"ft", AltitudeUnit::ft}}});
      } else if (k == "distance") {
        s.units.distance = choice(e, std::array<std::pair<std::string_view, DistanceUnit>, 2>{
                                         {{"km", DistanceUnit::km}, {"mi", DistanceUnit::mi}}});
      } else if (k == "time_zone") {
        const auto zone = trim(e.value);
        const auto valid_character = [](const char c) {
          return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '/' || c == '_' ||
                 c == '-' || c == '+' || c == ':';
        };
        if (zone.empty() || zone.size() > 64U ||
            !std::all_of(zone.begin(), zone.end(), valid_character))
          fail(e.line, e.section, k,
               "expected local, utc, +HH:MM or an IANA name such as Europe/Madrid");
        s.units.time_zone = zone;
      } else {
        unknown(e);
      }
    } else if (e.section == "text") {
      auto &t = s.text;
      if (k == "font_size")
        t.font_size = number_in(e, 0.005, 0.3);
      else if (k == "label_size")
        t.label_size = number_in(e, 0.2, 2.0);
      else if (k == "labels")
        t.labels = choice(e, std::array<std::pair<std::string_view, LabelPosition>, 3>{
                                 {{"above", LabelPosition::above},
                                  {"left", LabelPosition::left},
                                  {"none", LabelPosition::none}}});
      else if (k == "label_color")
        t.label_color = color(e);
      else if (k == "value_color")
        t.value_color = color(e);
      else if (k == "outline")
        t.outline = boolean(e);
      else if (k == "outline_color")
        t.outline_color = color(e);
      else if (k == "outline_width")
        t.outline_width = number_in(e, 0.0, 0.5);
      else
        unknown(e);
    } else if (e.section == "telemetry") {
      if (k == "enabled") {
        telemetry_enabled_ = boolean(e);
      } else if (k == "fields") {
        std::vector<Field> list;
        if (!trim(e.value).empty()) {
          for (const auto &name : split_list(e.value)) {
            const auto field = parse_field(name);
            if (!field)
              fail(e.line, e.section, k,
                   "unknown field '" + name + "'; fields are " + field_list());
            if (std::find(list.begin(), list.end(), *field) != list.end())
              fail(e.line, e.section, k, "'" + name + "' is listed twice");
            list.push_back(*field);
          }
        }
        list_ = list;
      } else {
        unknown(e);
      }
    } else if (e.section.rfind("field-", 0) == 0) {
      auto &f = custom(*parse_field(e.section.substr(6)));
      if (k == "label") {
        f.label = e.value;
      } else if (k == "anchor") {
        f.anchor = anchor(e);
      } else if (k == "align") {
        const auto align = parse_align(e.value);
        if (!align)
          fail(e.line, e.section, k, "expected left, center or right, got '" + e.value + "'");
        f.align = *align;
      } else if (k == "font_size") {
        f.font_size = number_in(e, 0.005, 0.3);
      } else {
        unknown(e);
      }
    } else if (e.section == "map") {
      auto &m = s.map;
      if (k == "enabled")
        map_enabled_ = boolean(e);
      else if (k == "width" || k == "scale")
        m.width_metres = distance(e, 10.0, 2.0e6);
      else if (k == "track")
        m.track = boolean(e);
      else if (k == "shape")
        m.shape = choice(e, std::array<std::pair<std::string_view, MapShape>, 2>{
                                {{"square", MapShape::square}, {"circle", MapShape::circle}}});
      else if (k == "size")
        m.size = number_in(e, 0.05, 1.0);
      else if (k == "anchor")
        m.anchor = anchor(e);
      else if (k == "opacity")
        m.opacity = number_in(e, 0.05, 1.0);
      else
        unknown(e);
    } else if (e.section == "output") {
      auto &o = s.output;
      if (k == "codec") {
        o.codec = choice(e, std::array<std::pair<std::string_view, Codec>, 2>{
                                {{"h264", Codec::h264}, {"hevc", Codec::hevc}}});
      } else if (k == "crf") {
        o.crf = integer_in(e, 0, 51);
      } else if (k == "encoder_preset") {
        static const std::array<std::string_view, 10> presets{
            "ultrafast", "superfast", "veryfast", "faster",   "fast",
            "medium",    "slow",      "slower",   "veryslow", "placebo"};
        const auto value = lower(trim(e.value));
        if (std::find(presets.begin(), presets.end(), value) == presets.end())
          fail(e.line, e.section, k,
               "expected ultrafast, superfast, veryfast, faster, fast, medium, slow, slower, "
               "veryslow or placebo");
        o.encoder_preset = value;
      } else if (k == "audio") {
        o.audio = boolean(e);
      } else if (k == "audio_bitrate") {
        o.audio_bitrate = integer_in(e, 32000, 512000);
      } else if (k == "speed") {
        o.speed = number_in(e, 1.0, kMaxSpeed);
      } else {
        unknown(e);
      }
    } else {
      unknown(e);
    }
  }

  std::vector<std::string> *warnings_;
  const SettingsSource *source_{};
  ExportSettings settings_;
  std::string raw_placement_;
  std::optional<Placement> panel_;
  bool map_present_{};
  std::optional<bool> map_enabled_;
  bool telemetry_present_{};
  std::optional<bool> telemetry_enabled_;
  std::optional<std::vector<Field>> list_;
  std::map<Field, FieldSettings> custom_;
  std::vector<Field> custom_order_;
};

} // namespace

std::string placement_name(const Placement placement) {
  switch (placement) {
  case Placement::none:
    return "none";
  case Placement::overlay:
    return "overlay";
  case Placement::left:
    return "left";
  case Placement::right:
    return "right";
  case Placement::top:
    return "top";
  case Placement::bottom:
    return "bottom";
  }
  return "none";
}

std::optional<Placement> parse_placement(const std::string &text) {
  if (text == "over")
    return Placement::overlay;
  for (const auto placement : {Placement::none, Placement::overlay, Placement::left,
                               Placement::right, Placement::top, Placement::bottom}) {
    if (placement_name(placement) == text)
      return placement;
  }
  return std::nullopt;
}

PanelSide panel_side(const Placement placement) {
  switch (placement) {
  case Placement::left:
    return PanelSide::left;
  case Placement::right:
    return PanelSide::right;
  case Placement::top:
    return PanelSide::top;
  case Placement::bottom:
    return PanelSide::bottom;
  case Placement::none:
  case Placement::overlay:
    break;
  }
  return PanelSide::none;
}

std::optional<Color> parse_color(const std::string &text) {
  const auto value = lower(trim(text));
  static const std::map<std::string, Color> names{
      {"black", {0, 0, 0, 255}},           {"white", {255, 255, 255, 255}},
      {"gray", {128, 128, 128, 255}},      {"grey", {128, 128, 128, 255}},
      {"red", {0xe0, 0x31, 0x31, 255}},    {"orange", {0xf7, 0x67, 0x07, 255}},
      {"yellow", {0xff, 0xd4, 0x3b, 255}}, {"green", {0x2f, 0x9e, 0x44, 255}},
      {"cyan", {0x22, 0xb8, 0xcf, 255}},   {"blue", {0x3b, 0x5b, 0xdb, 255}},
      {"magenta", {0xd6, 0x33, 0x6c, 255}}};
  if (const auto found = names.find(value); found != names.end())
    return found->second;
  if (value.empty() || value.front() != '#')
    return std::nullopt;
  const auto digits = value.substr(1);
  std::vector<int> nibbles;
  for (const auto c : digits) {
    const auto digit = hex_digit(c);
    if (digit < 0)
      return std::nullopt;
    nibbles.push_back(digit);
  }
  const auto byte = [&nibbles](const std::size_t index) {
    return static_cast<std::uint8_t>(nibbles[index] * 16 + nibbles[index + 1U]);
  };
  if (nibbles.size() == 3U) {
    return Color{static_cast<std::uint8_t>(nibbles[0] * 17),
                 static_cast<std::uint8_t>(nibbles[1] * 17),
                 static_cast<std::uint8_t>(nibbles[2] * 17), 255};
  }
  if (nibbles.size() == 6U)
    return Color{byte(0), byte(2), byte(4), 255};
  if (nibbles.size() == 8U)
    return Color{byte(0), byte(2), byte(4), byte(6)};
  return std::nullopt;
}

std::string color_text(const Color color) {
  constexpr std::string_view hex = "0123456789abcdef";
  std::string text = "#";
  for (const auto channel : {color.red, color.green, color.blue}) {
    text += hex[channel >> 4U];
    text += hex[channel & 15U];
  }
  if (color.alpha != 255) {
    text += hex[color.alpha >> 4U];
    text += hex[color.alpha & 15U];
  }
  return text;
}

std::optional<double> parse_distance(const std::string &text) {
  const auto value = lower(trim(text));
  static const std::array<std::pair<std::string_view, double>, 4> units{
      {{"km", 1000.0}, {"mi", 1609.344}, {"ft", 0.3048}, {"m", 1.0}}};
  for (const auto &[unit, metres] : units) {
    if (value.size() > unit.size() && value.ends_with(unit)) {
      const auto amount = parse_number(value.substr(0, value.size() - unit.size()));
      if (!amount || *amount <= 0.0)
        return std::nullopt;
      return *amount * metres;
    }
  }
  return std::nullopt;
}

std::string distance_text(const double metres) {
  if (metres < 10000.0)
    return std::to_string(std::max(1L, std::lround(metres))) + "m";
  return number(metres / 1000.0, 3) + "km";
}

std::optional<Anchor> parse_anchor(const std::string &text) {
  const auto value = lower(trim(text));
  for (const auto &[name, corner] : kCorners) {
    if (value == name)
      return Anchor{corner};
  }
  const auto parts = split_list(value);
  if (parts.size() != 2U)
    return std::nullopt;
  const auto x = parse_number(parts[0]);
  const auto y = parse_number(parts[1]);
  if (!x || !y || *x < 0.0 || *x > 1.0 || *y < 0.0 || *y > 1.0)
    return std::nullopt;
  return Anchor{Corner::free, *x, *y};
}

std::string anchor_text(const Anchor &anchor) {
  for (const auto &[name, corner] : kCorners) {
    if (anchor.corner == corner)
      return std::string(name);
  }
  return number(anchor.x, 4) + ", " + number(anchor.y, 4);
}

std::optional<Align> parse_align(const std::string &text) {
  const auto value = lower(trim(text));
  for (const auto align : {Align::automatic, Align::left, Align::center, Align::right}) {
    if (align_name(align) == value)
      return align;
  }
  return std::nullopt;
}

std::string align_name(const Align align) {
  switch (align) {
  case Align::automatic:
    return "auto";
  case Align::left:
    return "left";
  case Align::center:
    return "center";
  case Align::right:
    return "right";
  }
  return "auto";
}

std::string field_name(const Field field) {
  return std::string(kFieldNames[static_cast<std::size_t>(field)]);
}

std::optional<Field> parse_field(const std::string &text) {
  const auto value = lower(trim(text));
  if (value == "utc")
    return Field::datetime;
  for (const auto field : kAllFields) {
    if (field_name(field) == value)
      return field;
  }
  return std::nullopt;
}

std::string default_label(const Field field) {
  switch (field) {
  case Field::datetime:
    return "Date and time";
  case Field::date:
    return "Date";
  case Field::time:
    return "Time";
  case Field::latitude:
    return "Latitude";
  case Field::longitude:
    return "Longitude";
  case Field::altitude:
    return "Altitude";
  case Field::speed:
    return "Speed";
  case Field::distance:
    return "Distance";
  }
  return {};
}

std::string speed_unit_name(const SpeedUnit unit) {
  switch (unit) {
  case SpeedUnit::kmh:
    return "km/h";
  case SpeedUnit::mph:
    return "mph";
  case SpeedUnit::ms:
    return "m/s";
  case SpeedUnit::kn:
    return "kn";
  }
  return "km/h";
}

std::string altitude_unit_name(const AltitudeUnit unit) {
  return unit == AltitudeUnit::ft ? "ft" : "m";
}

std::string distance_unit_name(const DistanceUnit unit) {
  return unit == DistanceUnit::mi ? "mi" : "km";
}

FrameLayout ExportSettings::frame_layout() const {
  return exporting::frame_layout(aspect, width, panel_side(placement), panel_size);
}

double ExportSettings::picture_aspect() const {
  return exporting::picture_aspect(aspect, panel_side(placement), panel_size)
      .value_or(aspect.value());
}

Rect ExportSettings::crop_or_whole() const { return crop.value_or(Rect{}); }

ExportSettings combine_settings(const std::vector<SettingsSource> &sources,
                                std::vector<std::string> *warnings) {
  Combiner combiner(warnings);
  for (const auto &source : sources)
    combiner.read(source);
  return combiner.finish();
}

SettingsSource override_source(const std::string &name,
                               const std::vector<std::string> &assignments) {
  SettingsSource source;
  source.name = name;
  source.requires_schema = false;
  int index = 0;
  for (const auto &assignment : assignments) {
    ++index;
    const auto equals = assignment.find('=');
    const auto path = trim(std::string_view(assignment).substr(0, equals));
    const auto dot = path.rfind('.');
    if (equals == std::string::npos || dot == std::string::npos || dot == 0U ||
        dot + 1U == path.size()) {
      throw SettingsValidationError(name + " " + assignment +
                                    ": expected SECTION.KEY=VALUE, for example map.width=2km");
    }
    // Run the assignment through the INI reader, so names and values follow the file rules.
    const auto text = "[" + path.substr(0, dot) + "]\n" + path.substr(dot + 1U) + " = " +
                      assignment.substr(equals + 1U) + "\n";
    try {
      const auto document = parse_ini(text);
      for (const auto &section : document.sections) {
        if (std::find(source.document.sections.begin(), source.document.sections.end(), section) ==
            source.document.sections.end()) {
          source.document.sections.push_back(section);
        }
      }
      for (auto entry : document.entries) {
        entry.line = index;
        // A later --set of the same key replaces an earlier one.
        auto &entries = source.document.entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&entry](const IniEntry &existing) {
                                       return existing.section == entry.section &&
                                              existing.key == entry.key;
                                     }),
                      entries.end());
        entries.push_back(entry);
      }
    } catch (const SettingsError &error) {
      throw SettingsValidationError(name + " " + assignment + ": " + error.what());
    }
  }
  return source;
}

std::string write_export_settings(const ExportSettings &s, const std::vector<std::string> &header) {
  std::ostringstream out;
  for (const auto &line : header)
    out << "; " << line << '\n';
  out << "schema = " << kSettingsSchema << '\n';

  out << "\n[range]\n";
  if (s.start)
    out << "start = " << *s.start << '\n';
  if (s.end)
    out << "end   = " << *s.end << '\n';

  out << "\n[view]\n";
  out << "mode   = " << view_mode_name(s.mode) << '\n';
  if (is_spherical_mode(s.mode)) {
    out << "yaw    = " << number(s.yaw, 3) << '\n';
    out << "pitch  = " << number(s.pitch, 3) << '\n';
    out << "roll   = " << number(s.roll, 3) << '\n';
    out << "fov    = " << number(s.field_of_view, 3) << '\n';
  } else {
    out << "zoom   = " << number(s.zoom, 4) << '\n';
    out << "pan    = " << number(s.pan_x, 4) << ", " << number(s.pan_y, 4) << '\n';
  }
  out << "canvas = " << number(s.canvas_aspect(), 6) << '\n';

  out << "\n[frame]\n";
  out << "aspect = " << s.aspect.text() << '\n';
  const auto crop = s.crop_or_whole();
  out << "crop   = " << number(crop.x, 8) << ", " << number(crop.y, 8) << ", "
      << number(crop.width, 8) << ", " << number(crop.height, 8) << '\n';
  out << "width  = " << s.width << '\n';

  out << "\n[layout]\n";
  out << "placement  = " << placement_name(s.placement) << '\n';
  out << "panel_size = " << number(s.panel_size, 4) << '\n';
  out << "margin     = " << number(s.margin, 4) << '\n';

  out << "\n[units]\n";
  out << "speed     = " << speed_unit_name(s.units.speed) << '\n';
  out << "altitude  = " << altitude_unit_name(s.units.altitude) << '\n';
  out << "distance  = " << distance_unit_name(s.units.distance) << '\n';
  out << "time_zone = " << s.units.time_zone << '\n';

  const auto &t = s.text;
  out << "\n[text]\n";
  out << "font_size     = " << number(t.font_size, 4) << '\n';
  out << "label_size    = " << number(t.label_size, 3) << '\n';
  out << "labels        = "
      << (t.labels == LabelPosition::above  ? "above"
          : t.labels == LabelPosition::left ? "left"
                                            : "none")
      << '\n';
  out << "label_color   = " << color_text(t.label_color) << '\n';
  out << "value_color   = " << color_text(t.value_color) << '\n';
  out << "outline       = " << (t.outline ? "yes" : "no") << '\n';
  out << "outline_color = " << color_text(t.outline_color) << '\n';
  out << "outline_width = " << number(t.outline_width, 3) << '\n';

  if (s.telemetry) {
    out << "\n[telemetry]\n";
    out << "fields = ";
    for (std::size_t index = 0; index < s.fields.size(); ++index)
      out << (index > 0U ? ", " : "") << field_name(s.fields[index].field);
    out << '\n';
    for (const auto &f : s.fields) {
      out << "\n[field-" << field_name(f.field) << "]\n";
      out << "label  = " << f.label << '\n';
      out << "anchor = " << anchor_text(f.anchor) << '\n';
      out << "align  = " << align_name(f.align) << '\n';
      if (f.font_size)
        out << "font_size = " << number(*f.font_size, 4) << '\n';
    }
  }

  if (s.map.enabled) {
    const auto &m = s.map;
    out << "\n[map]\n";
    out << "width   = " << distance_text(m.width_metres) << '\n';
    out << "track   = " << (m.track ? "yes" : "no") << '\n';
    out << "shape   = " << (m.shape == MapShape::circle ? "circle" : "square") << '\n';
    out << "size    = " << number(m.size, 4) << '\n';
    out << "anchor  = " << anchor_text(m.anchor) << '\n';
    out << "opacity = " << number(m.opacity, 3) << '\n';
  }

  const auto &o = s.output;
  out << "\n[output]\n";
  out << "codec          = " << (o.codec == Codec::hevc ? "hevc" : "h264") << '\n';
  out << "crf            = " << o.crf << '\n';
  out << "encoder_preset = " << o.encoder_preset << '\n';
  out << "audio          = " << (o.audio ? "yes" : "no") << '\n';
  out << "audio_bitrate  = " << o.audio_bitrate << '\n';
  out << "speed          = " << number(o.speed, 3) << '\n';
  return out.str();
}

std::string example_settings() {
  return R"(; gb-render export settings (gb.export/1). Every key is optional except schema.
; Combine files: gb-render REC --settings framing.ini --settings this.ini -o out.mp4
; Sizes are fractions of the output frame height, so a draft and a 4K export look the same.
schema = gb.export/1

[range]
; chapter:time positions as gb-player shows them, or recording time without a chapter
start = c1:00:11:30.000
end   = c2:00:02:05.000

[view]
; Copy this section and [frame] from gb-player's Capture mode.
mode   = original       ; rectilinear, fisheye, stereographic, little-planet, panorama, raw-1, raw-2, original
; yaw = 0, pitch = 0, roll = 0, fov = 90 for spherical views; zoom = 1.0, pan = 0, 0 for flat ones

[frame]
aspect = 16:9           ; the whole output frame, panel included: 16:9, 4:3, 1:1, 9:16 or W:H
width  = 1920           ; the height follows from the aspect ratio
; crop = 0.1, 0.1, 0.8, 0.8   ; x, y, w, h of the canvas, 0..1 (default: all of it)

[layout]
placement  = overlay    ; none, overlay (over the picture), or a black panel: left, right, top, bottom
panel_size = 0.32       ; panel only: its width (left, right) or height (top, bottom)
margin     = 0.03       ; overlay only: space between overlays and the frame edge

[units]
speed     = km/h        ; km/h, mph, m/s, kn
altitude  = m           ; m, ft
distance  = km          ; km, mi
time_zone = local       ; local, utc, +02:00, or an IANA name such as Europe/Madrid

[text]
font_size     = 0.04    ; value text; in a panel, the largest size (text shrinks to fit)
label_size    = 0.6     ; label text, relative to font_size
labels        = above   ; above, left (same line) or none
label_color   = #c8d0d8
value_color   = #ffffff
outline       = yes     ; a border around the text, readable on bright pictures
outline_color = #000000
outline_width = 0.08    ; fraction of the text size

[telemetry]
; datetime, date, time, latitude, longitude, altitude, speed, distance (from the start of the range)
fields = time, speed, altitude, distance

; One optional section per field, to change its label and, over the picture, its place.
; Fields at the same anchor are stacked in the order of the list.
[field-speed]
label  = Speed
anchor = bottom-left    ; top-left, top, top-right, bottom-left, bottom, bottom-right, or x, y (0..1)
; align = auto          ; auto, left, center, right
; font_size = 0.07      ; this field only

[field-time]
label  =                ; empty: no label
anchor = top-left

[map]
; enabled = no removes a map that an earlier file added.
width   = 1.2km         ; ground distance across the map: 800m, 1.2km, 0.5mi
track   = yes           ; the route over the whole range, even across chapters
shape   = circle        ; square, or circle (inscribed in the square)
size    = 0.3           ; overlay only: side of the map
anchor  = top-right     ; overlay only
opacity = 0.9           ; overlay only

[output]
codec          = h264   ; h264 or hevc
crf            = 20     ; 0..51, lower is better
encoder_preset = medium ; ultrafast .. veryslow
audio          = yes
audio_bitrate  = 192000
speed          = 1      ; times real time, 1..1000: frames are dropped; audio keeps
                        ; its pitch up to 4, above that there is no audio
)";
}

} // namespace gb::exporting
