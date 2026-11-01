#pragma once

#include "gb/geo/mercator.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace gb::geo {

// A recorded position in media order. Unusable points (invalid fix, unknown validity, or an outlier
// found by mark_route_jumps) are kept so indices match the source records, but they are never
// drawn or offered as seek targets.
struct RoutePoint {
  LatLon position;
  std::int64_t ticks{};
  bool usable{};
  // The receiver's ground speed in m/s, when recorded. It is measured by the receiver, not derived
  // from the positions, so it vouches for long steps and never for a jump.
  std::optional<double> speed_mps;
  // Set by mark_route_jumps: the step from the previous usable point is an implausible jump, so
  // the route is not drawn across it.
  bool break_before{};
};

// Maximal runs [first, second) of consecutive usable points with no jump between them; the route
// is drawn per run, so an unusable stretch or a jump is a visible break rather than a straight
// line across missing or implausible data.
[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>>
usable_runs(const std::vector<RoutePoint> &points);

struct RouteJumps {
  std::size_t jumps{};   // breaks found, including those around omitted points
  std::size_t omitted{}; // points made unusable as outliers
};

// Finds implausible jumps between consecutive usable points and the outliers they isolate.
// Whether a step is a jump depends on its context, so walking and flying recordings are judged
// alike: a step is a jump when it is longer than kJumpFactor times the expected step plus
// kJumpNoiseMetres of fix noise. The expected step is the larger of the median of the neighbouring
// steps (kJumpWindow on each side, within the run) and the reported speed times the interval.
// Jumps split a run into segments. A segment that lasts less than kOutlierSeconds is an outlier
// cluster and its points become unusable, unless no segment of the run lasts that long; every
// other jump gets break_before on the point after it. Earlier marks are cleared first.
// `seconds_per_tick` converts the points' ticks.
inline constexpr double kJumpFactor = 8.0;
inline constexpr double kJumpNoiseMetres = 10.0;
inline constexpr std::size_t kJumpWindow = 10;
inline constexpr double kOutlierSeconds = 2.0;
RouteJumps mark_route_jumps(std::vector<RoutePoint> &points, double seconds_per_tick);

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
// GPS jitter at the tolerance edge from splitting one pass into several. A jump also ends a pass.
// Candidates are returned in media order.
[[nodiscard]] std::vector<RouteCandidate> route_candidates(const std::vector<RoutePoint> &points,
                                                           LatLon click, double tolerance_metres);

} // namespace gb::geo
