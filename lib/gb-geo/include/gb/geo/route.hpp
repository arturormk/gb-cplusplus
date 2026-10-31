#pragma once

#include "gb/geo/mercator.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace gb::geo {

// A recorded position in media order. Unusable points (invalid fix, unknown validity) are kept so
// indices match the source records, but they are never drawn or offered as seek targets.
struct RoutePoint {
  LatLon position;
  std::int64_t ticks{};
  bool usable{};
};

// Maximal runs [first, second) of consecutive usable points; the route is drawn per run, so an
// unusable stretch is a visible break rather than a straight line across missing data.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>>
usable_runs(const std::vector<RoutePoint> &points);

struct WorldBounds {
  WorldPoint minimum;
  WorldPoint maximum;
};
[[nodiscard]] std::optional<WorldBounds> route_bounds(const std::vector<RoutePoint> &points);

// One time the route passed near a clicked position.
struct RouteCandidate {
  std::size_t index{}; // into the points vector
  std::int64_t ticks{};
  double distance_metres{};
};

// Seek targets for a click on the map. A route can pass the same place several times (out and
// back, laps, stops), so every separate pass within the tolerance yields its own candidate: the
// recorded point of that pass nearest to the click. No position is interpolated. A pass ends when
// the route moves beyond twice the tolerance or reaches an unusable point; the hysteresis keeps
// GPS jitter at the tolerance edge from splitting one pass into several. Candidates are returned
// in media order.
[[nodiscard]] std::vector<RouteCandidate> route_candidates(const std::vector<RoutePoint> &points,
                                                           LatLon click, double tolerance_metres);

} // namespace gb::geo
