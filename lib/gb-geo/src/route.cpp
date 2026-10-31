#include "gb/geo/route.hpp"

#include <algorithm>
#include <cmath>

namespace gb::geo {

std::vector<std::pair<std::size_t, std::size_t>>
usable_runs(const std::vector<RoutePoint> &points) {
  std::vector<std::pair<std::size_t, std::size_t>> runs;
  std::size_t index = 0;
  while (index < points.size()) {
    while (index < points.size() && !points[index].usable)
      ++index;
    const auto first = index;
    while (index < points.size() && points[index].usable)
      ++index;
    if (index > first)
      runs.emplace_back(first, index);
  }
  return runs;
}

std::optional<WorldBounds> route_bounds(const std::vector<RoutePoint> &points) {
  std::optional<WorldBounds> bounds;
  for (const auto &point : points) {
    if (!point.usable)
      continue;
    const auto world = project(point.position);
    if (!bounds.has_value()) {
      bounds = WorldBounds{world, world};
      continue;
    }
    bounds->minimum = {std::min(bounds->minimum.x, world.x), std::min(bounds->minimum.y, world.y)};
    bounds->maximum = {std::max(bounds->maximum.x, world.x), std::max(bounds->maximum.y, world.y)};
  }
  return bounds;
}

std::vector<RouteCandidate> route_candidates(const std::vector<RoutePoint> &points,
                                             const LatLon click, const double tolerance_metres) {
  std::vector<RouteCandidate> candidates;
  if (!(tolerance_metres > 0.0) || !std::isfinite(tolerance_metres))
    return candidates;
  bool in_pass = false;
  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto &point = points[index];
    if (!point.usable) {
      in_pass = false;
      continue;
    }
    const auto distance = distance_metres(point.position, click);
    if (distance > 2.0 * tolerance_metres) {
      in_pass = false;
    } else if (distance <= tolerance_metres) {
      if (!in_pass) {
        candidates.push_back({index, point.ticks, distance});
        in_pass = true;
      } else if (distance < candidates.back().distance_metres) {
        candidates.back() = {index, point.ticks, distance};
      }
    }
  }
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const auto &left, const auto &right) { return left.ticks < right.ticks; });
  return candidates;
}

} // namespace gb::geo
