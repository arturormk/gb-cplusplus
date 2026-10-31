#include "gb/geo/mercator.hpp"
#include "gb/geo/route.hpp"
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
  return {{latitude, longitude}, ticks, usable};
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

} // namespace

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
