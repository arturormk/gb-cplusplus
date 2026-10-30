#pragma once

namespace gb::geo {

// WGS 84 coordinates in decimal degrees.
struct LatLon {
  double latitude{};
  double longitude{};
};

// Normalized spherical Web Mercator (EPSG:3857) coordinates: x grows east from the antimeridian
// over [0, 1), y grows south from the northern projection limit over [0, 1]. Multiply by
// tile_size * 2^zoom to obtain slippy-map pixel coordinates.
struct WorldPoint {
  double x{};
  double y{};
};

// Latitude at which Web Mercator becomes square; the projection is clamped to it.
inline constexpr double kMaximumMercatorLatitude = 85.05112877980659;
inline constexpr double kEarthMeanRadiusMetres = 6371008.8;

[[nodiscard]] WorldPoint project(LatLon position);
[[nodiscard]] LatLon unproject(WorldPoint point);
// Great-circle (haversine) distance on the mean-radius sphere.
[[nodiscard]] double distance_metres(LatLon from, LatLon to);
// Ground distance represented by one normalized world unit at a latitude.
[[nodiscard]] double metres_per_world_unit(double latitude);

} // namespace gb::geo
