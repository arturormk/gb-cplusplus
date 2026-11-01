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
    if (index == points.size())
      break;
    const auto first = index++;
    while (index < points.size() && points[index].usable && !points[index].break_before)
      ++index;
    runs.emplace_back(first, index);
  }
  return runs;
}

RouteJumps mark_route_jumps(std::vector<RoutePoint> &points, const double seconds_per_tick) {
  RouteJumps result;
  for (auto &point : points)
    point.break_before = false;
  const auto seconds = [seconds_per_tick](const RoutePoint &from, const RoutePoint &to) {
    return static_cast<double>(to.ticks - from.ticks) * seconds_per_tick;
  };
  std::vector<double> steps;
  std::vector<double> window;
  std::vector<std::size_t> starts;
  for (const auto &[first, last] : usable_runs(points)) {
    if (last - first < 2U)
      continue;
    // steps[k] leads from point first + k to first + k + 1.
    steps.clear();
    for (auto index = first + 1U; index < last; ++index)
      steps.push_back(distance_metres(points[index - 1U].position, points[index].position));
    starts.assign(1U, first);
    for (std::size_t k = 0; k < steps.size(); ++k) {
      window.clear();
      const auto begin = k > kJumpWindow ? k - kJumpWindow : 0U;
      const auto end = std::min(steps.size(), k + kJumpWindow + 1U);
      for (auto other = begin; other < end; ++other) {
        if (other != k)
          window.push_back(steps[other]);
      }
      double expected = 0.0;
      if (!window.empty()) {
        const auto middle = window.begin() + static_cast<std::ptrdiff_t>(window.size() / 2U);
        std::nth_element(window.begin(), middle, window.end());
        expected = *middle;
      }
      const auto &from = points[first + k];
      const auto &to = points[first + k + 1U];
      const auto speed = std::max(from.speed_mps.value_or(0.0), to.speed_mps.value_or(0.0));
      const auto travel = speed * seconds(from, to);
      if (std::isfinite(travel))
        expected = std::max(expected, travel);
      if (steps[k] > kJumpFactor * expected + kJumpNoiseMetres)
        starts.push_back(first + k + 1U);
    }
    if (starts.size() == 1U)
      continue;
    result.jumps += starts.size() - 1U;
    starts.push_back(last);
    const auto lasting = [&](const std::size_t segment) {
      return seconds(points[starts[segment]], points[starts[segment + 1U] - 1U]) >= kOutlierSeconds;
    };
    bool any_lasting = false;
    for (std::size_t segment = 0; segment + 1U < starts.size(); ++segment)
      any_lasting = any_lasting || lasting(segment);
    for (std::size_t segment = 0; segment + 1U < starts.size(); ++segment) {
      if (any_lasting && !lasting(segment)) {
        for (auto index = starts[segment]; index < starts[segment + 1U]; ++index)
          points[index].usable = false;
        result.omitted += starts[segment + 1U] - starts[segment];
      } else if (segment > 0U) {
        points[starts[segment]].break_before = true;
      }
    }
  }
  return result;
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
    if (point.break_before)
      in_pass = false;
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
