// Offscreen rendering with the player's shader. Skipped (exit code 77) when no OpenGL 3.3 or
// OpenGL ES 3.0 context can be created, so it runs wherever a GPU or Mesa's llvmpipe exists.
#include "gb/export/export_settings.hpp"
#include "gb/export/framing.hpp"
#include "gb/image/photo.hpp"
#include "gb/media/playback.hpp"
#include "gb/render/export_overlay.hpp"
#include "gb/render/panel_painter.hpp"
#include "gb/render/still.hpp"

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <QTimeZone>

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct Planes {
  std::vector<std::uint8_t> y;
  std::vector<std::uint8_t> u;
  std::vector<std::uint8_t> v;
};

// A full-range picture whose luma is a function of position, with neutral chroma (grey levels).
gb::media::PlanarPicture picture(const int width, const int height,
                                 const std::function<std::uint8_t(int, int)> &luma) {
  auto planes = std::make_shared<Planes>();
  const auto chroma_width = (width + 1) / 2;
  const auto chroma_height = (height + 1) / 2;
  planes->y.resize(static_cast<std::size_t>(width * height));
  planes->u.assign(static_cast<std::size_t>(chroma_width * chroma_height), 128U);
  planes->v.assign(static_cast<std::size_t>(chroma_width * chroma_height), 128U);
  for (int row = 0; row < height; ++row) {
    for (int column = 0; column < width; ++column)
      planes->y[static_cast<std::size_t>(row * width + column)] = luma(column, row);
  }
  gb::media::PlanarPicture result;
  result.width = width;
  result.height = height;
  result.planes = {planes->y.data(), planes->u.data(), planes->v.data()};
  result.strides = {width, chroma_width, chroma_width};
  result.full_range = true;
  result.storage = planes;
  return result;
}

int grey(const QImage &image, const int x, const int y) { return qGreen(image.pixel(x, y)); }

int largest_difference(const QImage &a, const QImage &b) {
  require(a.size() == b.size(), "images differ in size");
  int largest = 0;
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      const auto pa = a.pixel(x, y);
      const auto pb = b.pixel(x, y);
      largest = std::max({largest, std::abs(qRed(pa) - qRed(pb)), std::abs(qGreen(pa) - qGreen(pb)),
                          std::abs(qBlue(pa) - qBlue(pb))});
    }
  }
  return largest;
}

// The crop drawn at the output size equals the same part of the whole canvas drawn at a size
// where its pixels line up: the crop is applied before projection, as the plan requires.
void check_crop_matches_full(gb::render::OffscreenRenderer &renderer,
                             const gb::media::FrameSet &frames, gb::render::ViewParameters view,
                             const std::string &what) {
  view.view_crop = QRectF(0.0, 0.0, 1.0, 1.0);
  const auto full = renderer.render(frames, view, QSize(256, 128));
  view.view_crop = QRectF(0.25, 0.5, 0.5, 0.25);
  const auto crop = renderer.render(frames, view, QSize(128, 32));
  const auto expected = full.copy(64, 64, 128, 32);
  const auto difference = largest_difference(crop, expected);
  require(difference <= 2,
          what + ": crop differs from the full canvas by " + std::to_string(difference));
}

void test_flat(gb::render::OffscreenRenderer &renderer) {
  gb::media::FrameSet frames;
  // Left half dark, right half bright; a horizontal ramp in the bottom half.
  frames.pictures.push_back(picture(64, 32, [](const int x, const int y) {
    if (y >= 16)
      return static_cast<std::uint8_t>(x * 4);
    return static_cast<std::uint8_t>(x < 32 ? 40 : 200);
  }));
  gb::render::ViewParameters view;
  view.mode = gb::render::Original;
  view.layout = gb::render::FlatLayout;
  view.canvas = QSizeF(2.0, 1.0);
  const auto image = renderer.render(frames, view, QSize(128, 64));
  require(image.size() == QSize(128, 64), "output size");
  require(std::abs(grey(image, 10, 10) - 40) <= 1 && std::abs(grey(image, 118, 10) - 200) <= 1,
          "flat picture: halves at the top, not mirrored");
  require(grey(image, 10, 60) < grey(image, 118, 60), "flat picture: ramp at the bottom");

  view.view_crop = QRectF(0.5, 0.0, 0.5, 0.5);
  const auto quarter = renderer.render(frames, view, QSize(64, 32));
  require(std::abs(grey(quarter, 2, 2) - 200) <= 1 && std::abs(grey(quarter, 60, 28) - 200) <= 1,
          "crop of the top right quarter");
  check_crop_matches_full(renderer, frames, view, "flat");

  // The canvas aspect, not the output's, decides the projection: a 2:1 canvas cropped to a
  // square still shows the picture unstretched.
  view.view_crop = QRectF(0.25, 0.0, 0.5, 1.0);
  const auto square = renderer.render(frames, view, QSize(64, 64));
  require(std::abs(grey(square, 2, 2) - 40) <= 1 && std::abs(grey(square, 61, 2) - 200) <= 1,
          "square crop of a 2:1 canvas spans the middle of the picture");
}

void test_max(gb::render::OffscreenRenderer &renderer) {
  // MAX dual-stream geometry: two side strips and a centre face as wide as the height.
  constexpr int height = 64;
  constexpr int side = 72;
  constexpr int width = side * 2 + height;
  gb::media::FrameSet frames;
  frames.pictures.push_back(picture(width, height, [](const int x, const int y) {
    return static_cast<std::uint8_t>((x * 7 + y * 3) % 256);
  }));
  frames.pictures.push_back(picture(width, height, [](const int x, const int y) {
    return static_cast<std::uint8_t>((x * 2 + y * 5) % 256);
  }));
  gb::render::ViewParameters view;
  view.layout = gb::render::MaxDualEacLayout;
  view.canvas = QSizeF(2.0, 1.0);
  view.yaw = 0.4F;
  view.pitch = -0.2F;
  for (const auto mode : {gb::render::Rectilinear, gb::render::Fisheye, gb::render::LittlePlanet,
                          gb::render::Panorama}) {
    view.mode = mode;
    view.field_of_view = mode == gb::render::Rectilinear ? 1.6F : 3.4F;
    check_crop_matches_full(renderer, frames, view, "MAX mode " + std::to_string(mode));
  }
}

void test_still_writer(gb::render::OffscreenRenderer &renderer) {
  QTemporaryDir directory;
  require(directory.isValid(), "temporary directory");
  gb::media::FrameSet frames;
  frames.pictures.push_back(
      picture(64, 32, [](const int x, const int) { return static_cast<std::uint8_t>(x * 4); }));
  gb::render::ViewParameters view;
  view.mode = gb::render::Original;
  view.canvas = QSizeF(2.0, 1.0);
  view.view_crop = QRectF(0.0, 0.0, 1.0, 1.0);
  const auto layout =
      gb::exporting::frame_layout({16, 9}, 320, gb::exporting::PanelSide::right, 0.3);
  // The picture area has the remaining aspect; this test only checks the panel and the files.
  const auto still = gb::render::render_still(renderer, frames, view, layout);
  require(still.size() == QSize(320, 180), "frame size");
  require(qRed(still.pixel(319, 90)) == 0 && qGreen(still.pixel(319, 90)) == 0, "panel is black");
  require(grey(still, layout.picture.width - 2, 90) > 200, "picture next to the panel");

  gb::render::StillOptions options;
  gb::image::PhotoGps gps;
  gps.latitude = 40.5;
  gps.longitude = -3.25;
  gps.utc = "2026-01-01T12:00:00.000Z";
  options.gps = gps;
  const auto jpeg = directory.filePath(QStringLiteral("still.jpg"));
  gb::render::write_still(still, jpeg, options);
  const auto photo = gb::image::inspect_photo(jpeg.toStdString());
  require(photo.width == 320 && photo.height == 180, "JPEG size");
  require(photo.gps && std::abs(photo.gps->latitude - 40.5) < 1e-6 &&
              std::abs(photo.gps->longitude + 3.25) < 1e-6,
          "JPEG GPS EXIF");
  require(photo.exif && !photo.exif->make && !photo.exif->model, "no camera fields");

  options.gps.reset();
  gb::render::write_still(still, jpeg, options);
  const auto plain = gb::image::inspect_photo(jpeg.toStdString());
  require(!plain.exif && !plain.gps, "no EXIF without GPS");

  options.format = gb::render::StillFormat::png;
  const auto png = directory.filePath(QStringLiteral("still.png"));
  gb::render::write_still(still, png, options);
  require(QImage(png).size() == QSize(320, 180), "PNG");
  require(gb::render::still_format_for(QStringLiteral("a.JPEG")) == gb::render::StillFormat::jpeg &&
              !gb::render::still_format_for(QStringLiteral("a.tif")),
          "format from the extension");
}

// The side panel needs no OpenGL: tiles, route, attribution and telemetry text.
void test_panel() {
  namespace ex = gb::exporting;
  gb::image::PhotoGps fix;
  fix.latitude = 40.4167754;
  fix.longitude = -3.7037902;
  fix.altitude = 657.4;
  fix.speed_mps = 5.0;
  fix.utc = "2026-07-01T10:20:30Z";
  const auto lines = gb::render::telemetry_lines(fix, QTimeZone(7200));
  require(lines.size() == 5U, "five telemetry lines");
  require(lines[0].second == QStringLiteral("2026-07-01 12:20:30 +02:00"), "local date and time");
  require(lines[1].second == QStringLiteral("40.41678° N") &&
              lines[2].second == QStringLiteral("3.70379° W"),
          "coordinates with hemispheres");
  require(lines[3].second == QStringLiteral("657 m") &&
              lines[4].second == QStringLiteral("18.0 km/h"),
          "altitude and speed");
  const auto unknown = gb::render::telemetry_lines(std::nullopt, QTimeZone(0));
  require(std::all_of(unknown.begin(), unknown.end(),
                      [](const auto &line) { return line.second == QStringLiteral("—"); }),
          "no fix: every value unknown");

  QImage tile(256, 256, QImage::Format_RGB888);
  tile.fill(QColor(0x20, 0xc0, 0x40));
  const std::vector<gb::geo::WorldPoint> route{
      gb::geo::project({fix.latitude, fix.longitude}),
      gb::geo::project({fix.latitude + 0.002, fix.longitude + 0.002})};
  std::vector<gb::geo::RoutePoint> points(2);
  for (std::size_t index = 0; index < points.size(); ++index) {
    points[index].position = {fix.latitude + 0.002 * static_cast<double>(index),
                              fix.longitude + 0.002 * static_cast<double>(index)};
    points[index].usable = true;
  }
  gb::render::PanelContent content;
  content.scene.center = route.front();
  content.scene.zoom = 15.0;
  content.scene.route = &route;
  content.scene.points = &points;
  content.scene.record = 0;
  content.scene.marker = gb::render::MapMarker::valid;
  content.scene.tile = [&tile](gb::geo::TileKey) { return &tile; };
  content.attribution = QStringLiteral("© Test tiles");
  content.lines = lines;

  const auto layout = ex::frame_layout({16, 9}, 1920, ex::PanelSide::right, 0.32);
  const auto regions = ex::panel_regions(layout, ex::PanelSide::right, true, true);
  QImage frame(layout.width, layout.height, QImage::Format_RGB888);
  frame.fill(Qt::white);
  const auto result = gb::render::paint_panel(frame, layout, ex::PanelSide::right, content);
  require(result.missing_tiles == 0, "every tile available");
  require(frame.pixelColor(layout.panel->x + 4, 4) == QColor(Qt::black), "panel margin is black");
  require(frame.pixelColor(layout.picture.x + 10, 10) == QColor(Qt::white), "picture untouched");
  const auto map_corner = frame.pixelColor(regions.map->x + 3, regions.map->y + 3);
  require(map_corner == QColor(0x20, 0xc0, 0x40), "tiles drawn in the map square");
  const auto attribution = frame.pixelColor(regions.map->x + regions.map->width - 3,
                                            regions.map->y + regions.map->height - 3);
  require(attribution.lightness() > 200, "attribution box in the map's corner");
  int bright = 0;
  for (int y = regions.text->y; y < regions.text->y + regions.text->height; ++y) {
    for (int x = regions.text->x; x < regions.text->x + regions.text->width; ++x)
      bright += frame.pixelColor(x, y).lightness() > 160 ? 1 : 0;
  }
  require(bright > 500, "telemetry text drawn");
  require(!gb::render::panel_tiles(layout, ex::PanelSide::right, content).empty(), "panel tiles");
  const auto metres = gb::render::panel_map_width_metres(layout, ex::PanelSide::right, content);
  // A 298 px map (a 346 px panel less two margins) at zoom 15 near 40.4° N: about
  // 298 × 4.78 m × cos(40.4°) ≈ 1083 m.
  require(metres && std::abs(*metres - 1083.0) < 10.0, "ground width of the map");

  content.scene.tile = [](gb::geo::TileKey) -> const QImage * { return nullptr; };
  QImage missing(layout.width, layout.height, QImage::Format_RGB888);
  const auto none = gb::render::paint_panel(missing, layout, ex::PanelSide::right, content);
  require(none.missing_tiles > 0, "missing tiles reported");
  require(!gb::render::panel_map_width_metres(layout, ex::PanelSide::none, content),
          "no panel, no map");
}

// A straight walk east from Madrid: one record every 0.1 s (ticks of 1/1000 s), 1 m apart.
std::vector<gb::telemetry::GpsPoint> synthetic_walk(const std::size_t count) {
  std::vector<gb::telemetry::GpsPoint> points(count);
  for (std::size_t index = 0; index < count; ++index) {
    auto &point = points[index];
    point.media_time_ticks = static_cast<std::int64_t>(index) * 100;
    point.latitude = 40.4167754L;
    point.longitude = -3.7037902L + static_cast<long double>(index) * 0.0000118L; // ~1 m
    point.altitude = 650.0L + static_cast<long double>(index) * 0.1L;
    point.speed_2d = 10.0L;
    point.validity = gb::telemetry::Validity::valid;
    point.utc = "2026-07-01T10:20:30.000Z";
  }
  return points;
}

int count_colour(const QImage &image, const QRect &area, const QColor &colour,
                 const int tolerance) {
  int count = 0;
  for (int y = std::max(0, area.top()); y <= std::min(image.height() - 1, area.bottom()); ++y) {
    for (int x = std::max(0, area.left()); x <= std::min(image.width() - 1, area.right()); ++x) {
      const auto pixel = image.pixelColor(x, y);
      if (std::abs(pixel.red() - colour.red()) <= tolerance &&
          std::abs(pixel.green() - colour.green()) <= tolerance &&
          std::abs(pixel.blue() - colour.blue()) <= tolerance) {
        ++count;
      }
    }
  }
  return count;
}

void test_export_overlays() {
  namespace ex = gb::exporting;
  namespace gr = gb::render;

  // The track: lookup, the one-interval rule, gaps and distance.
  auto points = synthetic_walk(600); // 60 s
  points[300].validity = gb::telemetry::Validity::invalid;
  const gr::ExportTrack track(points, 10000, 50000, 0.001); // 10 s to 50 s
  require(track.nearest(1049) == 10U && track.nearest(1050) == 10U && track.nearest(1051) == 11U,
          "nearest record, earlier on ties");
  require(track.fix_at(1000) != nullptr && track.fix_at(30000) == nullptr,
          "an invalid record is no fix");
  require(track.fix_at(80000) == nullptr, "no fix beyond one record interval of the last record");
  require(track.route().size() == 403U, "the range's records and one beyond each end");
  const auto distance = track.distance_at(20000);
  require(distance && std::abs(*distance - 100.0) < 6.0, "distance from the start of the range");

  // Field texts in units and a time zone.
  ex::ExportSettings settings;
  settings.telemetry = true;
  for (const auto field : {ex::Field::time, ex::Field::speed, ex::Field::altitude,
                           ex::Field::distance, ex::Field::latitude}) {
    ex::FieldSettings f;
    f.field = field;
    f.label = ex::default_label(field);
    f.anchor = ex::Anchor{ex::Corner::bottom_left};
    settings.fields.push_back(f);
  }
  settings.units.speed = ex::SpeedUnit::mph;
  settings.units.altitude = ex::AltitudeUnit::ft;
  const auto zone = gr::time_zone_for("+02:00");
  require(zone.isValid() && gr::time_zone_for("Europe/Madrid").isValid() &&
              !gr::time_zone_for("Mars/Olympus").isValid(),
          "time zones");
  auto texts = gr::field_texts(settings, zone, track.fix_at(20000), track.distance_at(20000));
  require(texts[0].value == QStringLiteral("12:20:30") &&
              texts[1].value == QStringLiteral("22 mph") &&
              texts[2].value.endsWith(QStringLiteral(" ft")) &&
              texts[3].value == QStringLiteral("0.10 km") &&
              texts[4].value == QStringLiteral("40.41678° N"),
          "field values in their units");
  const auto unknown = gr::field_texts(settings, zone, nullptr, std::nullopt);
  require(std::all_of(unknown.begin(), unknown.end(),
                      [](const auto &text) { return text.value == QStringLiteral("—"); }),
          "no fix: every value unknown, never zero");

  // The map over the picture: a circle at the top right, with the route.
  settings.map.enabled = true;
  settings.map.width_metres = 300.0;
  settings.map.shape = ex::MapShape::circle;
  settings.map.anchor = ex::Anchor{ex::Corner::top_right};
  settings.map.size = 0.4;
  settings.placement = ex::Placement::overlay;
  QImage tile(256, 256, QImage::Format_ARGB32_Premultiplied);
  tile.fill(QColor(0x20, 0xc0, 0x40));
  gr::OverlayFrame frame;
  frame.scene.route = &track.route();
  frame.scene.points = &track.route_points();
  frame.scene.record = *track.route_index(20000);
  frame.scene.center = track.route()[*frame.scene.record];
  frame.scene.marker = gr::MapMarker::valid;
  frame.scene.tile = [&tile](gb::geo::TileKey) { return &tile; };
  frame.fields = texts;

  const auto render = [&](const ex::ExportSettings &s, const int width) {
    const auto layout =
        ex::frame_layout(s.aspect, width, ex::panel_side(s.placement), s.panel_size);
    QImage image(layout.width, layout.height, QImage::Format_RGB32);
    image.fill(QColor(0xff, 0xff, 0xff));
    QPainter painter(&image);
    const auto result =
        gr::paint_export_overlays(painter, s, layout, frame, QStringLiteral("© Test"));
    painter.end();
    require(result.missing_tiles == 0, "every tile available");
    return std::make_pair(image, layout);
  };
  auto [image, layout] = render(settings, 1920);
  const auto area = gr::export_map_area(settings, layout);
  require(area && std::abs(area->right() - (1920.0 - 0.03 * 1080.0)) < 1.0 &&
              std::abs(area->top() - 0.03 * 1080.0) < 1.0 && std::abs(area->width() - 432.0) < 1.0,
          "map square at the top right, inside the margin");
  const auto map_rect = area->toRect();
  require(image.pixelColor(map_rect.left() + 3, map_rect.top() + 3) == QColor(Qt::white),
          "circle: the square's corners show the picture");
  require(count_colour(image, map_rect, QColor(0x20, 0xc0, 0x40), 8) > 50000,
          "tiles drawn inside the circle");
  const QColor remaining(0x3b, 0x5b, 0xdb);
  const QColor travelled(0xf7, 0x67, 0x07);
  require(count_colour(image, map_rect, remaining, 12) > 100 &&
              count_colour(image, map_rect, travelled, 12) > 100,
          "the route ahead and behind is drawn");
  settings.map.track = false;
  auto without = render(settings, 1920).first;
  require(count_colour(without, map_rect, remaining, 12) == 0, "track = no: no route");
  require(count_colour(without, map_rect, travelled, 12) > 0, "track = no: the position is drawn");

  // Text: an outline in the outline colour around the values, at the bottom left.
  const QRect text_area(0, 540, 960, 540);
  require(count_colour(image, text_area, QColor(0, 0, 0), 30) > 1000, "outlined text");
  settings.text.outline = false;
  require(count_colour(render(settings, 1920).first, text_area, QColor(0, 0, 0), 30) < 50,
          "no outline");

  // The same layout at half the resolution.
  const auto half = render(settings, 960);
  const auto half_area = gr::export_map_area(settings, half.second);
  require(half_area && std::abs(half_area->x() * 2.0 - area->x()) < 2.0 &&
              std::abs(half_area->width() * 2.0 - area->width()) < 2.0,
          "the layout scales with the frame");

  // A top panel: black, map on the left, text on its right; the picture is untouched.
  settings.placement = ex::Placement::top;
  settings.panel_size = 0.3;
  auto [panel, panel_layout] = render(settings, 1920);
  require(panel_layout.panel && panel_layout.panel->y == 0 && panel_layout.picture.y > 0,
          "top panel above the picture");
  require(panel.pixelColor(1910, 4) == QColor(Qt::black), "panel is black");
  require(panel.pixelColor(1910, panel_layout.picture.y + 10) == QColor(Qt::white),
          "picture untouched");
  const auto panel_map = gr::export_map_area(settings, panel_layout);
  require(panel_map && panel_map->left() < 100.0 &&
              panel_map->bottom() <= panel_layout.panel->height,
          "panel map on the left");

  // Nothing at all with placement = none.
  settings.placement = ex::Placement::none;
  auto [plain, plain_layout] = render(settings, 1920);
  require(count_colour(plain, QRect(0, 0, 1920, 1080), QColor(Qt::white), 0) == 1920 * 1080,
          "placement none draws nothing");
}

} // namespace

int main(int argc, char **argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication application(argc, argv);
  // As in the applications: Qt adopts the user's locale; the parsers use the C locale.
  std::setlocale(LC_NUMERIC, "C");
  try {
    test_panel();
    test_export_overlays();
    gb::render::OffscreenRenderer renderer;
    if (!renderer.initialize()) {
      std::cout << "gb_render_tests: skipped: " << renderer.error() << '\n';
      return 77;
    }
    std::cout << "gb_render_tests: " << renderer.description() << '\n';
    test_flat(renderer);
    test_max(renderer);
    test_still_writer(renderer);
  } catch (const std::exception &error) {
    std::cerr << "gb_render_tests: " << error.what() << '\n';
    return 1;
  }
  std::cout << "gb_render_tests: all tests passed\n";
  return 0;
}
