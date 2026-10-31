#include "gb/geo/mercator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gb::geo {

namespace {

constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;

} // namespace

WorldPoint project(const LatLon position) {
  const auto latitude =
      std::clamp(position.latitude, -kMaximumMercatorLatitude, kMaximumMercatorLatitude);
  const auto sine = std::sin(latitude * kRadiansPerDegree);
  const auto x = (position.longitude + 180.0) / 360.0;
  const auto y = 0.5 - std::log((1.0 + sine) / (1.0 - sine)) / (4.0 * std::numbers::pi);
  return {x - std::floor(x), std::clamp(y, 0.0, 1.0)};
}

LatLon unproject(const WorldPoint point) {
  const auto y = std::clamp(point.y, 0.0, 1.0);
  const auto latitude =
      std::atan(std::sinh(std::numbers::pi * (1.0 - 2.0 * y))) / kRadiansPerDegree;
  return {latitude, point.x * 360.0 - 180.0};
}

double distance_metres(const LatLon from, const LatLon to) {
  const auto phi1 = from.latitude * kRadiansPerDegree;
  const auto phi2 = to.latitude * kRadiansPerDegree;
  const auto half_phi = (phi2 - phi1) / 2.0;
  const auto half_lambda = (to.longitude - from.longitude) * kRadiansPerDegree / 2.0;
  const auto a = std::sin(half_phi) * std::sin(half_phi) +
                 std::cos(phi1) * std::cos(phi2) * std::sin(half_lambda) * std::sin(half_lambda);
  return 2.0 * kEarthMeanRadiusMetres * std::asin(std::min(1.0, std::sqrt(a)));
}

double metres_per_world_unit(const double latitude) {
  const auto clamped = std::clamp(latitude, -kMaximumMercatorLatitude, kMaximumMercatorLatitude);
  return 2.0 * std::numbers::pi * kEarthMeanRadiusMetres * std::cos(clamped * kRadiansPerDegree);
}

} // namespace gb::geo
