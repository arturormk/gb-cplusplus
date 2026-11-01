#include "gb/geo/mercator.hpp"
#include "gb/geo/route.hpp"
#include "gb/geo/scale_curve.hpp"
#include "gb/geo/tile_cache.hpp"
#include "gb/geo/tiles.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(const bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

bool near(const double a, const double b, const double tolerance) {
  return std::fabs(a - b) <= tolerance;
}

void test_mercator() {
  using namespace gb::geo;
  const auto origin = project({0.0, 0.0});
  check(near(origin.x, 0.5, 1e-12) && near(origin.y, 0.5, 1e-12),
        "equator/prime meridian projects to the centre");
  const auto north = project({kMaximumMercatorLatitude, -180.0});
  check(near(north.x, 0.0, 1e-12) && near(north.y, 0.0, 1e-9),
        "projection limit is the top-left corner");
  check(near(project({89.9, 0.0}).y, 0.0, 1e-12), "latitudes beyond the limit are clamped");
  check(near(project({0.0, 180.0}).x, 0.0, 1e-12), "the antimeridian wraps to x = 0");
  for (const auto &position :
       {LatLon{40.4168, -3.7038}, LatLon{-33.8688, 151.2093}, LatLon{64.1466, -21.9426}}) {
    const auto back = unproject(project(position));
    check(near(back.latitude, position.latitude, 1e-9) &&
              near(back.longitude, position.longitude, 1e-9),
          "unproject inverts project");
  }
  // One degree of latitude on the mean sphere is about 111.195 km.
  check(near(distance_metres({0.0, 0.0}, {1.0, 0.0}), 111194.9, 0.5),
        "haversine distance of one degree");
  check(distance_metres({40.0, -3.0}, {40.0, -3.0}) == 0.0, "zero distance");
  check(near(metres_per_world_unit(0.0), 40030228.9, 1.0), "equatorial circumference");
  check(near(metres_per_world_unit(60.0), metres_per_world_unit(0.0) / 2.0, 1e-6),
        "scale shrinks with cos(latitude)");
}

void test_tiles() {
  using namespace gb::geo;
  check((tile_containing({0.5, 0.5}, 1) == TileKey{1, 1, 1}), "centre tile at zoom 1");
  check((tile_containing({1.0, 1.0}, 2) == TileKey{2, 3, 3}),
        "edge coordinates clamp to the last tile");
  check((tile_containing(project({40.4168, -3.7038}), 10) == TileKey{10, 501, 386}),
        "known tile for Madrid at z10");
  check(is_valid({0, 0, 0}) && !is_valid({1, 2, 0}) && !is_valid({-1, 0, 0}) &&
            !is_valid({25, 0, 0}),
        "tile key validation");
  check(TileKey{1, 0, 1} < TileKey{1, 1, 0} && !(TileKey{2, 0, 0} < TileKey{1, 1, 1}),
        "tile key ordering");

  const auto &osm = openstreetmap_provider();
  check(osm.id == "osm" && find_provider("osm") != nullptr && find_provider("osm")->id == "osm",
        "osm is built in");
  check(find_provider("unknown") == nullptr, "unknown provider");
  check(tile_url(osm, {12, 2006, 1554}) == "https://tile.openstreetmap.org/12/2006/1554.png",
        "OSM tile URL");
  check(!osm.bulk_download_allowed && osm.max_concurrent_requests <= 2 && !osm.attribution.empty(),
        "OSM policy limits are recorded");
  bool threw = false;
  try {
    (void)tile_url(osm, {20, 0, 0});
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  check(threw, "zoom beyond the provider is rejected");

  TileProvider custom = osm;
  custom.id = "example";
  custom.url_template = "https://{s}.tiles.example/{z}/{x}/{y}@2x.png?key=1";
  custom.subdomains = {"a", "b", "c"};
  validate(custom);
  check(tile_url(custom, {3, 1, 2}) == "https://a.tiles.example/3/1/2@2x.png?key=1",
        "subdomain rotation (x+y=3)");
  check(tile_url(custom, {3, 1, 3}) == "https://b.tiles.example/3/1/3@2x.png?key=1",
        "subdomain rotation (x+y=4)");

  const auto rejected = [](TileProvider provider) {
    try {
      validate(provider);
    } catch (const std::invalid_argument &) {
      return true;
    }
    return false;
  };
  auto bad = osm;
  bad.id = "../escape";
  check(rejected(bad), "path characters in the id are rejected");
  bad = osm;
  bad.url_template = "https://tiles.example/{z}/{x}.png";
  check(rejected(bad), "a template without {y} is rejected");
  bad = osm;
  bad.url_template = "file:///etc/{z}/{x}/{y}";
  check(rejected(bad), "non-HTTP templates are rejected");
  bad = osm;
  bad.attribution.clear();
  check(rejected(bad), "attribution is mandatory");
}

void test_cache(const std::filesystem::path &root) {
  using namespace gb::geo;
  const auto &osm = openstreetmap_provider();
  TileCache cache(root);
  const TileKey key{5, 15, 12};
  check(cache.tile_path(osm, key) == root / "osm" / "5" / "15" / "12.png", "cache layout");
  check(!cache.load(osm, key).has_value(), "missing tile");

  const std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13};
  cache.store(osm, key, png, {1000, 2000, "\"abc\"", "Wed, 21 Oct 2015 07:28:00 GMT"});
  const auto loaded = cache.load(osm, key);
  check(loaded.has_value() && loaded->bytes == png, "stored tile reads back");
  check(loaded.has_value() && loaded->metadata.etag == "\"abc\"" &&
            loaded->metadata.last_modified == "Wed, 21 Oct 2015 07:28:00 GMT" &&
            loaded->metadata.fetched_unix == 1000 && loaded->metadata.expires_unix == 2000,
        "metadata reads back");
  check(loaded.has_value() && loaded->fresh(1999) && !loaded->fresh(2000), "freshness boundary");

  cache.store_metadata(osm, key, {3000, 4000, "\"x\"\nexpires=99999999", ""});
  const auto updated = cache.load(osm, key);
  check(updated.has_value() && updated->metadata.expires_unix == 4000 &&
            updated->metadata.etag.empty(),
        "validators with line breaks are dropped, not injected");

  // Without a sidecar a tile is kept for the provider default from its modification time.
  const TileKey bare{5, 16, 12};
  const auto bare_path = cache.tile_path(osm, bare);
  std::filesystem::create_directories(bare_path.parent_path());
  std::ofstream(bare_path, std::ios::binary)
      .write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  const auto fallback = cache.load(osm, bare);
  check(fallback.has_value() && fallback->fresh(now) &&
            near(static_cast<double>(fallback->metadata.expires_unix - now),
                 static_cast<double>(osm.default_ttl_seconds), 120.0),
        "tile without metadata uses the default lifetime");

  std::ofstream(cache.tile_path(osm, bare).string() + ".meta") << "fetched=garbage\nexpires=1\n";
  const auto corrupt = cache.load(osm, bare);
  check(corrupt.has_value() && corrupt->fresh(now),
        "corrupt metadata falls back to the default lifetime");

  bool left_temporary = false;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.path().filename().string().find(".tmp-") != std::string::npos)
      left_temporary = true;
  }
  check(!left_temporary, "atomic writes leave no temporary files");
}

void test_cache_headers() {
  using namespace gb::geo;
  const auto &osm = openstreetmap_provider();
  const auto parsed = parse_cache_control("public, max-age=86400, stale-while-revalidate=60");
  check(parsed.max_age_seconds == 86400 && !parsed.no_cache && !parsed.no_store, "max-age parsing");
  check(parse_cache_control("Max-Age = \"30\"").max_age_seconds == 30,
        "case-insensitive, quoted max-age");
  check(parse_cache_control("no-cache").no_cache && parse_cache_control("no-store").no_store,
        "no-cache and no-store");
  check(!parse_cache_control("max-age=abc").max_age_seconds.has_value(), "invalid max-age ignored");
  check(parse_cache_control("max-age=99999999999999999999999").max_age_seconds == 2147483647,
        "overlong max-age capped");
  check(tile_expiry(osm, 100, parse_cache_control("max-age=50"), 999999) == 150,
        "max-age wins over Expires");
  check(tile_expiry(osm, 100, {}, 500) == 500, "Expires used without max-age");
  check(tile_expiry(osm, 100, {}, std::nullopt) == 100 + osm.default_ttl_seconds,
        "default lifetime without headers");
  check(tile_expiry(osm, 100, parse_cache_control("no-cache, max-age=50"), std::nullopt) == 100,
        "no-cache forces revalidation");

  const std::vector<std::uint8_t> jpeg{0xff, 0xd8, 0xff, 0xe0};
  const std::vector<std::uint8_t> webp{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'};
  const std::vector<std::uint8_t> html{'<', 'h', 't', 'm', 'l', '>'};
  check(looks_like_tile_image(jpeg) && looks_like_tile_image(webp) &&
            !looks_like_tile_image(html) && !looks_like_tile_image(std::vector<std::uint8_t>{}),
        "image signature check");
}

gb::geo::RoutePoint at(const double north_metres, const double east_metres,
                       const std::int64_t ticks, const bool usable = true) {
  // Local offsets around 40°N 3°W; one degree of latitude is 111,194.9 m on the mean sphere.
  constexpr double kMetresPerDegree = 111194.9;
  const auto latitude = 40.0 + north_metres / kMetresPerDegree;
  const auto longitude =
      -3.0 + east_metres / (kMetresPerDegree * std::cos(40.0 * 3.14159265358979323846 / 180.0));
  return {{latitude, longitude}, ticks, usable, std::nullopt, false};
}

void test_route() {
  using namespace gb::geo;
  // Out along the y axis and back on the same line, one point every 10 m.
  std::vector<RoutePoint> route;
  std::int64_t ticks = 0;
  for (int metres = 0; metres <= 200; metres += 10)
    route.push_back(at(metres, 0.0, ticks += 1000));
  for (int metres = 190; metres >= 0; metres -= 10)
    route.push_back(at(metres, 1.0, ticks += 1000));

  const auto click = at(100.0, 0.5, 0).position;
  const auto passes = route_candidates(route, click, 15.0);
  check(passes.size() == 2U, "out-and-back route passes the click twice");
  if (passes.size() == 2U) {
    check(passes[0].index == 10U && passes[1].index == 30U, "each pass picks its nearest record");
    check(passes[0].ticks < passes[1].ticks, "candidates in media order");
    check(passes[0].distance_metres < 1.0 && passes[1].distance_metres < 1.0,
          "candidate distances");
  }
  check(route_candidates(route, at(100.0, 500.0, 0).position, 15.0).empty(),
        "click far from the route");
  check(route_candidates(route, click, 0.0).empty(), "zero tolerance");

  // Jitter across the tolerance edge does not create extra passes: the point at 22 m is beyond the
  // tolerance but within twice it, so the pass continues.
  std::vector<RoutePoint> jitter{at(0, 0, 1), at(5, 0, 2),   at(22, 0, 3),
                                 at(6, 0, 4), at(100, 0, 5), at(4, 0, 6)};
  check(route_candidates(jitter, at(0, 0, 0).position, 15.0).size() == 2U,
        "hysteresis keeps one pass per visit");

  // An invalid fix between two visits is a break, and is never a candidate itself.
  std::vector<RoutePoint> broken{at(0, 0, 1), at(0, 0, 2, false), at(0, 0, 3)};
  const auto split = route_candidates(broken, at(0, 0, 0).position, 15.0);
  check(split.size() == 2U && split[0].index == 0U && split[1].index == 2U,
        "invalid record splits passes");

  const auto runs =
      usable_runs({at(0, 0, 1, false), at(0, 0, 2), at(0, 0, 3), at(0, 0, 4, false), at(0, 0, 5)});
  check(runs.size() == 2U && runs[0] == std::pair<std::size_t, std::size_t>{1, 3} &&
            runs[1] == std::pair<std::size_t, std::size_t>{4, 5},
        "usable runs");
  check(usable_runs({at(0, 0, 1, false)}).empty() &&
            !route_bounds({at(0, 0, 1, false)}).has_value(),
        "no usable points: no runs and no bounds");
  const auto bounds = route_bounds(route);
  check(bounds.has_value() && bounds->minimum.y < bounds->maximum.y &&
            bounds->minimum.x < bounds->maximum.x,
        "route bounds");
}

// A straight track at 18 records per second (55 ms apart) with deterministic fix noise.
std::vector<gb::geo::RoutePoint> straight_track(const double speed, const double noise,
                                                const int records) {
  std::vector<gb::geo::RoutePoint> track;
  for (int index = 0; index < records; ++index) {
    const auto seconds = 0.055 * index;
    auto point = at(speed * seconds + noise * std::sin(index * 1.7), noise * std::cos(index * 2.3),
                    static_cast<std::int64_t>(index) * 55);
    point.speed_mps = speed;
    track.push_back(point);
  }
  return track;
}

std::size_t count_unusable(const std::vector<gb::geo::RoutePoint> &points) {
  std::size_t count = 0;
  for (const auto &point : points)
    count += point.usable ? 0U : 1U;
  return count;
}

void test_route_jumps() {
  using namespace gb::geo;
  constexpr double kSecondsPerTick = 0.001;
  // Walking (1.4 m/s, ±1 m noise), cruising (250 m/s, ±3 m noise) and standing still (±2 m noise)
  // recordings have no jumps, with or without a reported speed.
  for (const auto &[speed, noise] : {std::pair{1.4, 1.0}, {250.0, 3.0}, {0.0, 2.0}}) {
    auto clean = straight_track(speed, noise, 600);
    auto found = mark_route_jumps(clean, kSecondsPerTick);
    check(found.jumps == 0U && count_unusable(clean) == 0U,
          "no jumps in a clean track at " + std::to_string(speed) + " m/s");
    for (auto &point : clean)
      point.speed_mps.reset();
    found = mark_route_jumps(clean, kSecondsPerTick);
    check(found.jumps == 0U, "no jumps without speed at " + std::to_string(speed) + " m/s");
  }

  for (const auto &[speed, offset] : {std::pair{1.4, 600.0}, {250.0, 3000.0}}) {
    const auto label = " at " + std::to_string(speed) + " m/s";
    // As in a real recording: the first half second of fixes sits far from where the track
    // continues, and later one record jumps away and back.
    auto track = straight_track(speed, 1.0, 600);
    for (std::size_t index = 0; index < 10U; ++index)
      track[index].position.latitude += offset / 111194.9;
    track[300].position.longitude += offset / 85000.0;
    const auto found = mark_route_jumps(track, kSecondsPerTick);
    check(found.omitted == 11U && count_unusable(track) == 11U, "outliers omitted" + label);
    check(!track[0].usable && !track[9].usable && track[10].usable && !track[300].usable &&
              track[299].usable && track[301].usable,
          "the right records omitted" + label);
    check(found.jumps == 3U && track[301].break_before, "breaks around the outliers" + label);
    const auto runs = usable_runs(track);
    check(runs.size() == 2U && runs[0] == std::pair<std::size_t, std::size_t>{10, 300} &&
              runs[1] == std::pair<std::size_t, std::size_t>{301, 600},
          "runs skip the outliers" + label);

    // A lasting shift is a break; neither side is an outlier.
    auto shifted = straight_track(speed, 1.0, 600);
    for (std::size_t index = 200; index < shifted.size(); ++index)
      shifted[index].position.longitude += offset / 85000.0;
    const auto shift = mark_route_jumps(shifted, kSecondsPerTick);
    check(shift.jumps == 1U && shift.omitted == 0U && shifted[200].break_before &&
              usable_runs(shifted).size() == 2U,
          "a lasting shift is a break" + label);
    check(route_candidates(shifted, shifted[199].position, offset * 4.0 / 3.0).size() == 2U,
          "a jump ends a click pass" + label);
  }

  // One fix a second at 10 m/s, with no record for 50 s in the middle (a GPS gap): the reported
  // speed vouches for the 500 m step, which the neighbouring 10 m steps alone would call a jump.
  std::vector<RoutePoint> sparse;
  for (int index = 0; index < 40; ++index) {
    const auto second = index < 20 ? index : index + 49;
    auto point = at(10.0 * second, 0.0, static_cast<std::int64_t>(second) * 1000);
    point.speed_mps = 10.0;
    sparse.push_back(point);
  }
  check(mark_route_jumps(sparse, kSecondsPerTick).jumps == 0U, "reported speed vouches");
  for (auto &point : sparse)
    point.speed_mps.reset();
  check(mark_route_jumps(sparse, kSecondsPerTick).jumps == 1U && sparse[20].break_before,
        "without speed it is a jump");

  // When no segment lasts, nothing is omitted: the jumps are only breaks.
  auto brief = straight_track(1.4, 0.5, 30);
  for (std::size_t index = 15; index < brief.size(); ++index)
    brief[index].position.latitude += 500.0 / 111194.9;
  const auto short_run = mark_route_jumps(brief, kSecondsPerTick);
  check(short_run.jumps == 1U && short_run.omitted == 0U && brief[15].break_before,
        "short runs are not emptied");
}

} // namespace

// A track along the equator, one record per second (ticks are milliseconds), at the speed
// `speed(t)` in m/s; records for which `missing(t)` holds are left out.
template <typename Speed, typename Missing>
std::vector<gb::geo::RoutePoint> equator_track(const int seconds, Speed speed, Missing missing) {
  constexpr double kMetresPerDegree = 6371008.8 * 3.14159265358979323846 / 180.0;
  std::vector<gb::geo::RoutePoint> points;
  double metres = 0.0;
  for (int t = 0; t <= seconds; ++t) {
    if (!missing(t))
      points.push_back({{0.0, metres / kMetresPerDegree}, t * 1000LL, true, speed(t), false});
    metres += speed(t);
  }
  return points;
}

void test_scale_curve() {
  using namespace gb::geo;
  const auto none = [](int) { return false; };
  const ScaleCurveSettings settings{60.0, 150.0, 100000.0};
  const auto at = [](const ScaleCurve &curve, const int seconds) {
    return curve.width_at(seconds * 1000LL);
  };
  // The route of the next minute at three quarters of the radius: 600 m ahead, 1600 m across.
  const auto expected = 2.0 * 600.0 / kScaleAheadFraction;

  const ScaleCurve steady(equator_track(
                              900, [](int) { return 10.0; }, none),
                          0, 900000, 0.001, settings);
  check(!steady.empty(), "a curve over the range");
  check(at(steady, 300) >= 0.95 * expected && at(steady, 300) <= 1.12 * expected,
        "steady speed: the next minute fills the map with a margin");
  double steepest = 0.0;
  for (int t = 1; t <= 900; ++t)
    steepest = std::max(steepest, std::fabs(std::log2(at(steady, t) / at(steady, t - 1))));
  check(steepest <= kScaleZoomOutRate + 1e-9, "the zoom never changes faster than its rate");

  const ScaleCurve still(equator_track(
                             300, [](int) { return 0.0; }, none),
                         0, 300000, 0.001, settings);
  check(near(at(still, 150), 150.0, 1.0), "standing still: the closest limit");
  const ScaleCurve no_fix({}, 0, 300000, 0.001, settings);
  check(near(at(no_fix, 10), 150.0, 1.0), "no fix at all: the closest limit");

  // Speeding up from 2 to 20 m/s at 300 s: the map is already wide when it happens.
  const ScaleCurve faster(equator_track(
                              900, [](int t) { return t < 300 ? 2.0 : 20.0; }, none),
                          0, 900000, 0.001, settings);
  check(at(faster, 300) >= 0.95 * 2.0 * 1200.0 / kScaleAheadFraction,
        "zooming out happens ahead of time, so the route ahead stays in view");
  check(at(faster, 200) < 1000.0, "but not long before");

  // A ten-second stop is not followed; a five-minute stop is, slowly.
  const ScaleCurve short_stop(
      equator_track(
          900, [](int t) { return t >= 400 && t < 410 ? 0.0 : 10.0; }, none),
      0, 900000, 0.001, settings);
  double narrowest = 1e9;
  for (int t = 100; t <= 800; ++t)
    narrowest = std::min(narrowest, at(short_stop, t));
  check(narrowest >= 0.95 * expected * (50.0 / 60.0), "a short stop barely moves the map");
  const ScaleCurve long_stop(
      equator_track(
          1200, [](int t) { return t >= 400 && t < 900 ? 0.0 : 10.0; }, none),
      0, 1200000, 0.001, settings);
  double fastest_in = 0.0;
  for (int t = 1; t <= 1200; ++t)
    fastest_in = std::max(fastest_in, std::log2(at(long_stop, t - 1) / at(long_stop, t)));
  check(fastest_in <= kScaleZoomInRate + 1e-9, "zooming in is slow");
  check(at(long_stop, 380) >= 2.0 * 200.0 / kScaleAheadFraction,
        "approaching the stop, the rest of the way stays in view");
  check(near(at(long_stop, 750), 150.0, 30.0), "a long stop ends at the closest limit");

  // A five-minute GPS gap at steady speed: the next fix after it is taken as steady travel, so
  // the gap does not zoom the map out to its whole length.
  const ScaleCurve gap(equator_track(
                           900, [](int) { return 10.0; }, [](int t) { return t > 300 && t < 600; }),
                       0, 900000, 0.001, settings);
  double widest = 0.0;
  for (int t = 0; t <= 900; ++t)
    widest = std::max(widest, at(gap, t));
  check(widest <= 1.15 * expected, "a GPS gap does not widen the map");
  check(at(gap, 450) >= 0.9 * expected, "the scale is held through the gap");

  // The limits.
  const ScaleCurve fast(equator_track(
                            600, [](int) { return 300.0; }, none),
                        0, 600000, 0.001, {60.0, 150.0, 20000.0});
  check(near(at(fast, 300), 20000.0, 1.0), "the widest limit");
  check(near(fast.widest().metres, 20000.0, 1.0) && fast.closest().metres <= 20000.0,
        "the extremes are reported");
}

int main() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("gb-geo-test-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    test_mercator();
    test_tiles();
    test_cache(root);
    test_cache_headers();
    test_route();
    test_route_jumps();
    test_scale_curve();
  } catch (const std::exception &error) {
    std::cerr << "FAIL: unexpected exception: " << error.what() << '\n';
    ++failures;
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  if (failures > 0)
    return 1;
  std::cout << "gb_geo_tests: all checks passed\n";
  return 0;
}
