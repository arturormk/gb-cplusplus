#include "gb/export/export_settings.hpp"
#include "gb/export/framing.hpp"
#include "gb/export/settings.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace ex = gb::exporting;

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool near(const double value, const double expected, const double tolerance = 1e-9) {
  return std::abs(value - expected) <= tolerance;
}

template <typename Function> void throws(Function function, const std::string &message) {
  bool thrown = false;
  try {
    function();
  } catch (const std::exception &) {
    thrown = true;
  }
  require(thrown, message);
}

void test_aspect() {
  require(ex::parse_aspect("16:9") == ex::AspectRatio{16, 9}, "16:9");
  require(ex::parse_aspect("9:16") == ex::AspectRatio{9, 16}, "9:16");
  require(ex::parse_aspect("21:9").has_value(), "free ratio");
  require(!ex::parse_aspect("16x9") && !ex::parse_aspect("0:9") && !ex::parse_aspect("16:") &&
              !ex::parse_aspect("-16:9") && !ex::parse_aspect("1.5:1") &&
              !ex::parse_aspect("11:1") && !ex::parse_aspect("100000:1"),
          "invalid ratios rejected");
  require(ex::AspectRatio{4, 3}.text() == "4:3", "aspect text");
  require(ex::parse_panel_side("right") == ex::PanelSide::right &&
              ex::parse_panel_side("top") == ex::PanelSide::top && !ex::parse_panel_side("middle"),
          "panel side names");
}

void test_frame_layout() {
  auto layout = ex::frame_layout({16, 9}, 1920);
  require(layout.width == 1920 && layout.height == 1080 && !layout.panel &&
              layout.picture == ex::PixelRect{0, 0, 1920, 1080},
          "1080p");
  layout = ex::frame_layout({9, 16}, 1080);
  require(layout.height == 1920, "vertical");
  layout = ex::frame_layout({4, 3}, 1001);
  require(layout.width % 2 == 0 && layout.height % 2 == 0, "even sizes");
  layout = ex::frame_layout({16, 9}, 1920, ex::PanelSide::right, 0.32);
  require(layout.panel && layout.picture.x == 0 && layout.picture.width % 2 == 0 &&
              layout.picture.width + layout.panel->width == 1920 &&
              layout.panel->x == layout.picture.width && std::abs(layout.panel->width - 346) <= 1,
          "right panel");
  layout = ex::frame_layout({16, 9}, 1920, ex::PanelSide::left, 0.32);
  require(layout.panel->x == 0 && layout.picture.x == layout.panel->width, "left panel");
  layout = ex::frame_layout({16, 9}, 1920, ex::PanelSide::bottom, 0.25);
  require(layout.picture.height == 810 && layout.panel->y == 810 && layout.panel->height == 270,
          "bottom panel");
  layout = ex::frame_layout({16, 9}, 1920, ex::PanelSide::top, 0.25);
  require(layout.picture.height == 810 && layout.picture.y == 270 && layout.panel->y == 0 &&
              layout.panel->height == 270,
          "top panel");
  {
    const auto top = ex::panel_regions(layout, ex::PanelSide::top, true, true);
    require(top.map && top.text && top.map->y == 24 && top.map->height == 270 - 48 &&
                top.text->x > top.map->x,
            "top panel: map left, text right");
  }
  throws([] { (void)ex::frame_layout({16, 9}, 8); }, "too narrow");
  throws(
      [] {
        (void)ex::frame_layout({1, 1}, 1080, ex::PanelSide::right, 0.95);
      },
      "panel larger than the frame");

  require(near(*ex::picture_aspect({16, 9}, ex::PanelSide::none, 0.0), 16.0 / 9.0), "no panel");
  require(near(*ex::picture_aspect({16, 9}, ex::PanelSide::right, 0.32), 16.0 / 9.0 - 0.32),
          "side panel aspect");
  require(near(*ex::picture_aspect({16, 9}, ex::PanelSide::bottom, 0.25), 16.0 / 9.0 / 0.75),
          "bottom panel aspect");
  require(!ex::picture_aspect({16, 9}, ex::PanelSide::right, 0.01), "panel too small");

  {
    const auto right = ex::frame_layout({16, 9}, 1920, ex::PanelSide::right, 0.32);
    const auto regions = ex::panel_regions(right, ex::PanelSide::right, true, true);
    require(regions.map && regions.text, "map and text in a side panel");
    require(regions.map->x == right.panel->x + 24 && regions.map->y == 24 &&
                regions.map->width == regions.map->height &&
                regions.map->width == right.panel->width - 48,
            "square map at the top of a side panel");
    require(regions.text->y == regions.map->y + regions.map->height + 24 &&
                regions.text->y + regions.text->height == 1080 - 24,
            "text below the map");
    const auto only_map = ex::panel_regions(right, ex::PanelSide::right, true, false);
    require(only_map.map && !only_map.text, "map only");
    const auto only_text = ex::panel_regions(right, ex::PanelSide::right, false, true);
    require(!only_text.map && only_text.text && only_text.text->height == 1080 - 48, "text only");
    const auto bottom = ex::frame_layout({16, 9}, 3840, ex::PanelSide::bottom, 0.25);
    const auto below = ex::panel_regions(bottom, ex::PanelSide::bottom, true, true);
    require(below.map->height == bottom.panel->height - 96 && below.map->x == 48 &&
                below.text->x == below.map->x + below.map->width + 48,
            "bottom panel: map left, text right, margins scale with the frame");
    require(
        !ex::panel_regions(ex::frame_layout({16, 9}, 1920), ex::PanelSide::none, true, true).map,
        "no panel, no regions");
  }

  const auto presets = ex::width_presets({16, 9});
  require(presets.size() == 4U && presets[1] == 1920 && presets[3] == 3840, "16:9 presets");
  require(ex::width_presets({9, 16})[1] == 608, "9:16 presets follow the height");
}

void test_crop() {
  const double canvas = 1.6;
  auto crop = ex::fit_crop(canvas, 16.0 / 9.0, 0.5, 0.5, 2.0);
  require(near(crop.width, 1.0) && near(crop.height, 0.9) && near(crop.y, 0.05),
          "largest crop of a wider picture fills the width");
  require(ex::crop_fits(canvas, crop, 16.0 / 9.0), "crop fits");
  crop = ex::fit_crop(canvas, 1.0, 0.9, 0.5, 0.5);
  require(near(crop.height, 0.5) && near(crop.width, 0.3125) && near(crop.x + crop.width, 1.0),
          "moved inside at the edge");
  require(!ex::crop_fits(canvas, {0.0, 0.0, 0.5, 0.5}, 1.0), "wrong aspect");
  require(!ex::crop_fits(canvas, {0.6, 0.0, 0.5, 0.8}, 1.0), "outside the canvas");

  auto moved = ex::move_crop({0.1, 0.1, 0.3, 0.3}, 1.0, -1.0);
  require(near(moved.x, 0.7) && near(moved.y, 0.0), "move clamps");
  auto scaled = ex::scale_crop({0.25, 0.25, 0.5, 0.5}, 0.5, 0.5, 0.5, 0.05);
  require(near(scaled.x, 0.375) && near(scaled.width, 0.25), "scale about the centre");
  scaled = ex::scale_crop({0.25, 0.25, 0.5, 0.5}, 10.0, 0.5, 0.5, 0.05);
  require(near(scaled.height, 1.0) && near(scaled.x, 0.0), "scale up stops at the canvas");
  scaled = ex::scale_crop({0.25, 0.25, 0.5, 0.5}, 0.001, 0.5, 0.5, 0.05);
  require(near(scaled.height, 0.05), "minimum size");
}

void test_view_and_names() {
  require(ex::view_mode_name(3) == "little-planet" && ex::parse_view_mode("raw-2") == 6 &&
              !ex::parse_view_mode("sideways"),
          "view names");
  require(ex::still_file_name("GH010073", std::string("c2-05m31s400"), "rectilinear", "jpg") ==
              "GH010073_c2-05m31s400_rectilinear.jpg",
          "file name");
  require(ex::still_file_name("GS__0033", std::nullopt, "panorama", "png") ==
              "GS__0033_panorama.png",
          "photo file name");
}

void test_output_scale() {
  ex::ResolutionInput input;
  input.source = ex::SourceKind::flat;
  input.source_width = 1920;
  input.source_height = 1080;
  input.canvas_aspect = 16.0 / 9.0;
  input.crop = {0.25, 0.25, 0.5, 0.5};
  input.picture_width = 1920;
  input.picture_height = 1080;
  require(near(*ex::output_scale(input), 2.0), "half the flat picture at full size is 2×");
  input.crop = {0.0, 0.0, 1.0, 1.0};
  input.zoom = 2.0;
  require(near(*ex::output_scale(input), 2.0), "zoom counts");

  input = {};
  input.source = ex::SourceKind::max_dual_eac;
  input.source_width = 5952;
  input.source_height = 1920;
  input.mode = 0;
  input.field_of_view_degrees = 90.0;
  input.canvas_aspect = 1.0;
  input.picture_width = 1920;
  input.picture_height = 1920;
  // 90° across 1920 output pixels, as a cube face is across 1920 source pixels: about 1:1 at the
  // centre (rectilinear and the face projection agree there to within π/4·… of each other).
  const auto scale = *ex::output_scale(input);
  require(scale > 0.7 && scale < 1.3, "rectilinear 90° of a MAX face is near 1:1");
  input.crop = {0.25, 0.25, 0.5, 0.5};
  require(near(*ex::output_scale(input), 2.0 * scale), "a crop magnifies");
  input.source_width = 0;
  require(!ex::output_scale(input), "incomplete input");
}

void test_ini() {
  const auto document =
      ex::parse_ini("; comment\nschema = gb.export/1\n\n[view]\nmode = fisheye  ; "
                    "trailing\n# another\n[frame]\ncrop = 0, 0, 1, 1\r\n[view]\nyaw=3\n");
  require(document.find("", "schema")->value == "gb.export/1", "file-wide key");
  require(document.find("view", "mode")->value == "fisheye", "comment stripped");
  require(document.find("frame", "crop")->value == "0, 0, 1, 1", "CRLF");
  require(document.find("view", "yaw")->line == 10, "reopened section and line numbers");
  require(document.sections.size() == 2U, "sections listed once");
  const auto colours = ex::parse_ini("[text]\na = #ffd43b # yellow\nb = #fff;white\nc = x #\n");
  require(colours.find("text", "a")->value == "#ffd43b" &&
              colours.find("text", "b")->value == "#fff" && colours.find("text", "c")->value == "x",
          "'#' before a colour is not a comment");

  const auto error_line = [](const std::string &text) {
    try {
      (void)ex::parse_ini(text);
    } catch (const ex::SettingsError &error) {
      return error.line();
    }
    return -1;
  };
  require(error_line("[a]\nx = 1\nx = 2\n") == 3, "duplicate key");
  require(error_line("[a\n") == 1, "unterminated header");
  require(error_line("[a]\njust words\n") == 2, "not a key");
  require(error_line("[a b]\n") == 1, "invalid section name");
  require(error_line(std::string(5000, 'x')) == 1, "long line");
  require(error_line(std::string("a = b\0c", 7)) == 1, "NUL");
  require(error_line(std::string(2 * 1024 * 1024, '\n')) == 0, "large file");
}

void test_settings_writer() {
  ex::CaptureSettings settings;
  settings.generator = "gb-player 0.1.52";
  settings.source = "GH010073.MP4";
  settings.has_range = true;
  settings.start = "c2:00:05:31.400";
  settings.mode = 0;
  settings.yaw = -12.5;
  settings.pitch = 4.0;
  settings.roll = -0.0;
  settings.field_of_view = 95.0;
  settings.canvas = 16.0 / 9.0;
  settings.aspect = {16, 9};
  settings.crop = {0.12, 0.08, 0.64, 0.64};
  settings.width = 1920;
  const auto text = ex::write_capture_settings(settings);
  const auto expected = "; from gb-player 0.1.52, GH010073.MP4\n"
                        "schema = gb.export/1\n"
                        "\n[range]\n"
                        "start = c2:00:05:31.400\n"
                        "; end = (Set end in gb-player)\n"
                        "\n[view]\n"
                        "mode   = rectilinear\n"
                        "yaw    = -12.5\n"
                        "pitch  = 4.0\n"
                        "roll   = 0.0\n"
                        "fov    = 95.0\n"
                        "canvas = 1.7778\n"
                        "\n[frame]\n"
                        "aspect = 16:9\n"
                        "crop   = 0.1200, 0.0800, 0.6400, 0.6400\n"
                        "width  = 1920\n";
  require(text == expected, "settings text:\n" + text);

  const auto document = ex::parse_ini(text);
  require(document.find("", "schema")->value == ex::kSettingsSchema, "schema written");

  // A small crop of an odd canvas needs more decimals to keep its aspect ratio.
  settings.canvas = 1.234567;
  settings.panel = ex::PanelSide::right;
  settings.panel_size = 0.3;
  const auto picture = *ex::picture_aspect(settings.aspect, settings.panel, settings.panel_size);
  settings.crop = ex::fit_crop(settings.canvas, picture, 0.333333, 0.6666666, 0.0123456);
  settings.has_range = false;
  settings.mode = 7;
  settings.zoom = 2.5;
  settings.pan_x = 0.125;
  const auto small = ex::parse_ini(ex::write_capture_settings(settings));
  require(!small.find("range", "start"), "a photo has no range");
  require(small.find("view", "zoom")->value == "2.5" &&
              small.find("view", "pan")->value == "0.125, 0.0",
          "flat view");
  require(!small.find("layout", "panel") && small.find("layout", "panel_size")->value == "0.3" &&
              small.find("layout", "placement")->value == "right",
          "layout section");
  const auto crop_text = small.find("frame", "crop")->value;
  ex::Rect crop;
  require(std::sscanf(crop_text.c_str(), "%lf, %lf, %lf, %lf", &crop.x, &crop.y, &crop.width,
                      &crop.height) == 4,
          "crop parses");
  require(ex::crop_fits(std::stod(small.find("view", "canvas")->value), crop, picture),
          "written crop keeps the picture aspect: " + crop_text);
  require(!small.find("map", "width") && !small.find("telemetry", "fields"), "an empty panel");

  settings.map_width_metres = 1082.6;
  settings.telemetry = true;
  const auto filled = ex::parse_ini(ex::write_capture_settings(settings));
  require(filled.find("map", "width")->value == "1083m", "map scale as a ground distance");
  require(filled.find("telemetry", "fields")->value ==
                  "utc, latitude, longitude, altitude, speed" &&
              filled.find("units", "speed")->value == "km/h" &&
              filled.find("units", "time_zone")->value == "local",
          "telemetry fields and units");
  settings.panel = ex::PanelSide::none;
  require(!ex::parse_ini(ex::write_capture_settings(settings)).find("map", "width"),
          "no panel, no map");
}

ex::SettingsSource source(const std::string &name, const std::string &text) {
  return {name, ex::parse_ini(text), true};
}

ex::ExportSettings combine(const std::vector<std::string> &texts,
                           std::vector<std::string> *warnings = nullptr) {
  std::vector<ex::SettingsSource> sources;
  for (std::size_t index = 0; index < texts.size(); ++index)
    sources.push_back(source("file" + std::to_string(index + 1) + ".ini", texts[index]));
  return ex::combine_settings(sources, warnings);
}

std::string error_of(const std::vector<std::string> &texts) {
  try {
    (void)combine(texts);
  } catch (const ex::SettingsValidationError &error) {
    return error.what();
  }
  return {};
}

void test_values() {
  require(ex::parse_color("#ffd43b") == ex::Color{0xff, 0xd4, 0x3b, 255}, "rrggbb");
  require(ex::parse_color("#FFF") == ex::Color{255, 255, 255, 255}, "rgb");
  require(ex::parse_color("#00000080") == ex::Color{0, 0, 0, 0x80}, "rrggbbaa");
  require(ex::parse_color("white") == ex::Color{255, 255, 255, 255}, "named");
  require(!ex::parse_color("#ff") && !ex::parse_color("ffd43b") && !ex::parse_color("#ggg"),
          "invalid colours");
  require(ex::color_text({0xff, 0xd4, 0x3b, 255}) == "#ffd43b" &&
              ex::color_text({0, 0, 0, 0x80}) == "#00000080",
          "colour text");

  require(near(*ex::parse_distance("1.2km"), 1200.0) && near(*ex::parse_distance("800m"), 800.0) &&
              near(*ex::parse_distance("1mi"), 1609.344) &&
              near(*ex::parse_distance("1000ft"), 304.8) &&
              near(*ex::parse_distance(" 2 KM "), 2000.0),
          "distances");
  require(!ex::parse_distance("1200") && !ex::parse_distance("km") && !ex::parse_distance("-1km") &&
              !ex::parse_distance("0m"),
          "a distance needs a positive amount and a unit");
  require(ex::distance_text(1200.0) == "1200m" && ex::distance_text(12500.0) == "12.5km",
          "distance text");

  require(ex::parse_anchor("bottom-right") == ex::Anchor{ex::Corner::bottom_right}, "corner");
  const auto free = ex::parse_anchor("0.25, 0.9");
  require(free && free->corner == ex::Corner::free && near(free->x, 0.25) && near(free->y, 0.9),
          "free anchor");
  require(!ex::parse_anchor("1.5, 0") && !ex::parse_anchor("middle") && !ex::parse_anchor("0.5"),
          "invalid anchors");
  require(ex::anchor_text(*free) == "0.25, 0.9", "anchor text");

  require(ex::parse_field("utc") == ex::Field::datetime &&
              ex::parse_field("speed") == ex::Field::speed && !ex::parse_field("heart"),
          "fields");
  require(ex::parse_placement("over") == ex::Placement::overlay &&
              ex::parse_placement("top") == ex::Placement::top && !ex::parse_placement("beside"),
          "placements");
}

void test_export_settings() {
  // Defaults: nothing but the schema.
  auto s = combine({"schema = gb.export/1\n"});
  require(s.placement == ex::Placement::overlay && !s.map.enabled && !s.telemetry &&
              s.width == 1920 && s.output.codec == ex::Codec::h264 && s.output.audio,
          "defaults");

  // Overlay with custom fields, colours and a round map.
  s = combine({R"(schema = gb.export/1
[layout]
placement = overlay
[text]
font_size = 0.05
label_color = yellow
value_color = #ffffff
outline = yes
[telemetry]
fields = speed, altitude
[field-speed]
label = Velocidad
anchor = bottom-right
font_size = 0.08
[field-time]
label =
anchor = 0.5, 0.05
align = center
[map]
width = 1.5km
track = no
shape = circle
anchor = top-left
)"});
  require(s.telemetry && s.fields.size() == 3U, "listed fields plus customised ones");
  require(s.fields[0].field == ex::Field::speed && s.fields[0].label == "Velocidad" &&
              s.fields[0].anchor.corner == ex::Corner::bottom_right &&
              near(*s.fields[0].font_size, 0.08),
          "a customised field");
  require(s.fields[1].field == ex::Field::altitude && s.fields[1].label == "Altitude" &&
              s.fields[1].anchor.corner == ex::Corner::bottom_left,
          "a field with defaults");
  require(s.fields[2].field == ex::Field::time && s.fields[2].label.empty() &&
              s.fields[2].anchor.corner == ex::Corner::free &&
              s.fields[2].align == ex::Align::center,
          "a field only in its own section is appended; an empty label is no label");
  require(near(s.text.font_size, 0.05) && s.text.label_color == ex::Color{0xff, 0xd4, 0x3b, 255},
          "text style");
  require(s.map.enabled && near(s.map.width_metres, 1500.0) && !s.map.track &&
              s.map.shape == ex::MapShape::circle && s.map.anchor.corner == ex::Corner::top_left,
          "map");

  // Later files win key by key; enabled = no removes an overlay.
  s = combine({"schema = gb.export/1\n[map]\nwidth = 2km\n[layout]\nplacement = left\n",
               "schema = gb.export/1\n[map]\nshape = circle\n"});
  require(s.map.enabled && near(s.map.width_metres, 2000.0) &&
              s.map.shape == ex::MapShape::circle && s.placement == ex::Placement::left,
          "combined files");
  s = combine({"schema = gb.export/1\n[map]\n", "schema = gb.export/1\n[map]\nenabled = no\n"});
  require(!s.map.enabled, "map removed");

  // gb-player 0.1 wrote placement = beside with a panel key, and the map width as scale.
  s = combine({"schema = gb.export/1\n[layout]\nplacement = beside\npanel = bottom\n"
               "[map]\nscale = 832m\n[telemetry]\nfields = utc, speed\n"});
  require(s.placement == ex::Placement::bottom && near(s.map.width_metres, 832.0) &&
              s.fields.size() == 2U && s.fields[0].field == ex::Field::datetime,
          "the earlier form");

  // The settings Capture mode writes are read back.
  ex::CaptureSettings capture;
  capture.generator = "gb-player test";
  capture.source = "GH010073.MP4";
  capture.has_range = true;
  capture.start = "c2:00:05:31.400";
  capture.current = "c2:00:05:31.400";
  capture.mode = 0;
  capture.canvas = 16.0 / 9.0;
  capture.panel = ex::PanelSide::top;
  capture.panel_size = 0.25;
  capture.crop = ex::fit_crop(
      capture.canvas, *ex::picture_aspect(capture.aspect, capture.panel, 0.25), 0.5, 0.5, 0.6);
  capture.map_width_metres = 900.0;
  capture.telemetry = true;
  s = combine({ex::write_capture_settings(capture)});
  require(s.placement == ex::Placement::top && s.start == std::string("c2:00:05:31.400") &&
              s.mode == 0 && s.map.enabled && near(s.map.width_metres, 900.0) && s.telemetry &&
              s.crop.has_value(),
          "Capture settings read back");

  // --print-settings output reads back to the same settings.
  s = combine({R"(schema = gb.export/1
[view]
mode = rectilinear
yaw = -12.5
fov = 95
[frame]
aspect = 4:3
width = 1440
[layout]
placement = right
panel_size = 0.4
[units]
speed = mph
time_zone = Europe/Madrid
[telemetry]
fields = datetime, distance
[field-distance]
anchor = 0.1, 0.2
[map]
width = 3km
[output]
speed = 2.5
)"});
  const auto written = ex::write_export_settings(s, {"test"});
  const auto again = combine({written});
  require(ex::write_export_settings(again, {"test"}) == written, "printed settings round-trip");
  require(again.mode == 0 && near(again.yaw, -12.5) && again.aspect == ex::AspectRatio{4, 3} &&
              again.placement == ex::Placement::right && again.units.speed == ex::SpeedUnit::mph &&
              again.units.time_zone == "Europe/Madrid" && again.fields.size() == 2U &&
              again.fields[1].anchor.corner == ex::Corner::free && near(again.output.speed, 2.5),
          "printed settings keep their values");

  // The example is valid.
  s = combine({ex::example_settings()});
  require(s.map.enabled && s.map.shape == ex::MapShape::circle && s.telemetry &&
              near(s.output.speed, 1.0),
          "example settings");

  // --set overrides.
  s = ex::combine_settings({source("a.ini", "schema = gb.export/1\n"),
                            ex::override_source("--set", {"map.width=2.5km", "text.outline=no",
                                                          "field-speed.label=V"})});
  require(s.map.enabled && near(s.map.width_metres, 2500.0) && !s.text.outline &&
              s.fields.size() == 1U && s.fields[0].label == "V",
          "--set");
  throws([] { (void)ex::override_source("--set", {"width=3"}); }, "--set needs a section");

  // The automatic map scale.
  s = combine({"schema = gb.export/1\n[map]\nwidth = auto\nahead = 1.5\nclosest = 200m\n"});
  require(s.map.auto_width && near(s.map.ahead_seconds, 90.0) &&
              near(s.map.closest_metres, 200.0) && near(s.map.widest_metres, 100000.0),
          "width = auto, ahead in decimal minutes");
  require(near(*ex::parse_minutes("90s"), 90.0) && near(*ex::parse_minutes("0.5min"), 30.0) &&
              !ex::parse_minutes("soon") && !ex::parse_minutes("0"),
          "durations");
  {
    const auto text = ex::write_export_settings(s);
    require(text.find("width   = auto") != std::string::npos &&
                text.find("ahead   = 1.5min") != std::string::npos,
            "auto scale written");
    require(combine({text}).map.ahead_seconds == 90.0 && combine({text}).map.auto_width,
            "auto scale round-trips");
  }
  s = combine({"schema = gb.export/1\n[map]\nwidth = auto\n",
               "schema = gb.export/1\n[map]\nwidth = 900m\n"});
  require(!s.map.auto_width && near(s.map.width_metres, 900.0), "a later fixed width wins");
  {
    std::vector<std::string> ignored;
    (void)combine({"schema = gb.export/1\n[map]\nahead = 2\n"}, &ignored);
    require(ignored.size() == 1U && ignored[0].find("ahead") != std::string::npos,
            "ahead without width = auto warns");
  }
  require(error_of({"schema = gb.export/1\n[map]\nwidth = auto\nclosest = 5km\nwidest = 1km\n"})
                  .find("closest") != std::string::npos,
          "closest wider than widest");
  require(error_of({"schema = gb.export/1\n[map]\nahead = 2h\n"}).find("ahead") !=
              std::string::npos,
          "ahead needs minutes or seconds");

  // Warnings.
  std::vector<std::string> warnings;
  (void)combine({"schema = gb.export/1\n[layout]\nplacement = none\n[map]\n"}, &warnings);
  require(warnings.size() == 1U, "placement none warns about the map");

  // Errors name the file, line and key.
  require(error_of({"[map]\nwidth = 1km\n"}).find("schema") != std::string::npos, "schema");
  require(error_of({"schema = gb.export/2\n"}).find("gb.export/2") != std::string::npos,
          "schema version");
  require(error_of({"schema = gb.export/1\n[mapp]\nwidth = 1km\n"}) ==
              "file1.ini:3: unknown section [mapp]",
          "unknown section");
  require(error_of({"schema = gb.export/1\n[map]\nwidht = 1km\n"}) ==
              "file1.ini:3: [map] widht: unknown key",
          "unknown key");
  require(
      error_of({"schema = gb.export/1\n[map]\nwidth = 1200\n"}).find("file1.ini:3: [map] width") ==
          0U,
      "distance without a unit");
  require(error_of({"schema = gb.export/1\n[map]\nwidth = 1km\nscale = 2km\n"}).find("scale") !=
              std::string::npos,
          "width and scale together");
  require(error_of({"schema = gb.export/1\n[text]\nfont_size = 2\n"}).find("out of range") !=
              std::string::npos,
          "out of range");
  require(error_of({"schema = gb.export/1\n[output]\nspeed = 0.5\n"}).find("out of range") !=
              std::string::npos,
          "slower than real time");
  require(error_of({"schema = gb.export/1\n[field-heart]\nlabel = x\n"}).find("unknown field") !=
              std::string::npos,
          "unknown field section");
  require(error_of({"schema = gb.export/1\n[telemetry]\nfields = speed, speed\n"}).find("twice") !=
              std::string::npos,
          "field listed twice");
  require(error_of({"schema = gb.export/1\n[text]\nlabel_color = #12\n"}).find("colour") !=
              std::string::npos,
          "bad colour");
  require(error_of({"schema = gb.export/1\n[frame]\ncrop = 0, 0, 0.5, 0.9\n"}).find("crop") !=
              std::string::npos,
          "crop of the wrong aspect ratio");
  require(error_of({"schema = gb.export/1\n[layout]\nplacement = right\npanel_size = 0.9\n"
                    "[frame]\naspect = 1:1\n"})
                  .find("panel") != std::string::npos,
          "panel leaves no picture");
}

} // namespace

int main() {
  try {
    test_aspect();
    test_frame_layout();
    test_crop();
    test_view_and_names();
    test_output_scale();
    test_ini();
    test_settings_writer();
    test_values();
    test_export_settings();
  } catch (const std::exception &error) {
    std::cerr << "gb_export_tests: " << error.what() << '\n';
    return 1;
  }
  std::cout << "gb_export_tests: all tests passed\n";
  return 0;
}
