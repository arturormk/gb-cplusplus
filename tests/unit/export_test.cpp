#include "gb/export/framing.hpp"
#include "gb/export/settings.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

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
  require(ex::parse_panel_side("right") == ex::PanelSide::right && !ex::parse_panel_side("top"),
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
  require(small.find("layout", "panel")->value == "right" &&
              small.find("layout", "panel_size")->value == "0.3" &&
              small.find("layout", "placement")->value == "beside",
          "layout section");
  const auto crop_text = small.find("frame", "crop")->value;
  ex::Rect crop;
  require(std::sscanf(crop_text.c_str(), "%lf, %lf, %lf, %lf", &crop.x, &crop.y, &crop.width,
                      &crop.height) == 4,
          "crop parses");
  require(ex::crop_fits(std::stod(small.find("view", "canvas")->value), crop, picture),
          "written crop keeps the picture aspect: " + crop_text);
  require(!small.find("map", "scale") && !small.find("telemetry", "fields"), "an empty panel");

  settings.map_width_metres = 1082.6;
  settings.telemetry = true;
  const auto filled = ex::parse_ini(ex::write_capture_settings(settings));
  require(filled.find("map", "scale")->value == "1083m", "map scale as a ground distance");
  require(filled.find("telemetry", "fields")->value ==
                  "utc, latitude, longitude, altitude, speed" &&
              filled.find("units", "speed")->value == "km/h" &&
              filled.find("units", "time_zone")->value == "local",
          "telemetry fields and units");
  settings.panel = ex::PanelSide::none;
  require(!ex::parse_ini(ex::write_capture_settings(settings)).find("map", "scale"),
          "no panel, no map");
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
  } catch (const std::exception &error) {
    std::cerr << "gb_export_tests: " << error.what() << '\n';
    return 1;
  }
  std::cout << "gb_export_tests: all tests passed\n";
  return 0;
}
