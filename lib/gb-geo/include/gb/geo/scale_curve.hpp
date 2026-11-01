#pragma once

#include "gb/geo/route.hpp"

#include <cstdint>
#include <optional>
#include <vector>

// The automatic map scale (docs/plans/export/pipeline.md#map-scale): the ground width of a map
// centred on the current position that keeps the route of the next `ahead` seconds in view,
// computed once over a whole export from the GPS records, so the map zooms out before it is
// needed and never changes erratically.
namespace gb::geo {

struct ScaleCurveSettings {
  double ahead_seconds{60.0};
  double closest_metres{150.0};
  double widest_metres{100000.0};
};

// Where the route ahead falls within the map's radius: the rest is margin.
inline constexpr double kScaleAheadFraction = 0.75;
// Zoom levels per second (a level halves or doubles the width). Zooming out is prepared ahead
// of time, so its rate only sets how early it starts; zooming in is slow on purpose.
inline constexpr double kScaleZoomOutRate = 0.25;
inline constexpr double kScaleZoomInRate = 0.03;
// A requirement lower than the map for less than this is ignored: a short stop or a straight
// stretch between bends does not make the map breathe.
inline constexpr double kScaleHoldSeconds = 15.0;
inline constexpr double kScaleStepSeconds = 0.5;

class ScaleCurve {
public:
  ScaleCurve() = default;
  // `points` are every record in media order with the usable and break_before flags of
  // mark_route_jumps, including those outside [start, end], which the look-ahead and the current
  // position at the ends may use. The curve covers [start, end] in the points' ticks.
  ScaleCurve(const std::vector<RoutePoint> &points, std::int64_t start, std::int64_t end,
             double seconds_per_tick, const ScaleCurveSettings &settings);

  [[nodiscard]] bool empty() const { return widths_.empty(); }
  // Ground distance across the map at `ticks`, in metres; clamped to the curve's ends.
  [[nodiscard]] double width_at(std::int64_t ticks) const;

  struct Extreme {
    std::int64_t ticks{};
    double metres{};
  };
  [[nodiscard]] Extreme closest() const;
  [[nodiscard]] Extreme widest() const;

private:
  std::int64_t start_{};
  double ticks_per_step_{1.0};
  std::vector<double> widths_; // metres, one per step from start_
  double fallback_{1000.0};
};

} // namespace gb::geo
