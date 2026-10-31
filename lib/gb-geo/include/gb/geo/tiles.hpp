#pragma once

#include "gb/geo/mercator.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gb::geo {

// One slippy-map raster tile (XYZ scheme, y = 0 at the north edge).
struct TileKey {
  int zoom{};
  int x{};
  int y{};
  // Explicit operators rather than a defaulted <=>: Qt 6.2 moc cannot parse <compare>.
  friend bool operator==(const TileKey &left, const TileKey &right) {
    return left.zoom == right.zoom && left.x == right.x && left.y == right.y;
  }
  friend bool operator<(const TileKey &left, const TileKey &right) {
    if (left.zoom != right.zoom)
      return left.zoom < right.zoom;
    if (left.x != right.x)
      return left.x < right.x;
    return left.y < right.y;
  }
};

inline constexpr int kMaximumTileZoom = 24;

[[nodiscard]] bool is_valid(TileKey key);
// The tile containing a world point at a zoom level; coordinates are clamped to the map.
[[nodiscard]] TileKey tile_containing(WorldPoint point, int zoom);

// Describes a raster tile source and the usage policy the fetcher must respect. Providers are
// data, not code: adding one is adding a description, and every consumer reads its limits here.
struct TileProvider {
  std::string id;           // cache directory and configuration name: [a-z0-9_-]+
  std::string name;         // human-readable name
  std::string url_template; // {z}, {x}, {y} and optional {s}
  std::vector<std::string> subdomains;
  int min_zoom{0};
  int max_zoom{19};
  int tile_size{256};
  std::string file_extension{"png"};
  std::string attribution;     // text that must stay visible on the map
  std::string attribution_url; // where the attribution links
  // Cache lifetime when the server sends no usable Cache-Control or Expires header.
  std::int64_t default_ttl_seconds{7 * 24 * 3600};
  int max_concurrent_requests{2};
  // Whether tiles outside the user's current view may be fetched (prefetch, offline packs, export
  // pre-seeding). OpenStreetMap forbids this.
  bool bulk_download_allowed{false};
};

// The OpenStreetMap Foundation standard tile layer, used under its tile usage policy:
// https://operations.osmfoundation.org/policies/tiles/
[[nodiscard]] const TileProvider &openstreetmap_provider();
[[nodiscard]] const std::vector<TileProvider> &builtin_providers();
// Returns nullptr for an unknown id.
[[nodiscard]] const TileProvider *find_provider(std::string_view id);
// Throws std::invalid_argument when the description cannot be used safely.
void validate(const TileProvider &provider);
// Throws std::invalid_argument for a key outside the provider's zoom range.
[[nodiscard]] std::string tile_url(const TileProvider &provider, TileKey key);

} // namespace gb::geo
