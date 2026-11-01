#include "gb/geo/scale_curve.hpp"

#include "gb/geo/mercator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace gb::geo {

namespace {

// Records closer together than this are thinned for the look-ahead: 18 Hz GPS adds no shape
// to a ground distance that a few records per second do not.
constexpr double kThinSeconds = 0.2;
// A current position older (or younger) than this is a GPS gap: the scale is held there.
constexpr double kStaleSeconds = 2.0;
// A look-ahead cut short by the end of the records or a jump is extrapolated when it covers at
// least this fraction of `ahead`, and held otherwise.
constexpr double kMinimumCoverage = 0.25;
// Variations of the requirement smaller than this, in zoom levels, are not followed downward,
// except down to the closest limit.
constexpr double kDeadBand = 0.1;
// Rounds the corners of the rate-limited curve.
constexpr double kSmoothSeconds = 2.0;

struct Sample {
  double seconds{};
  LatLon position;
  std::size_t segment{};
};

std::vector<double> sliding_max(const std::vector<double> &values, const std::ptrdiff_t radius) {
  const auto n = static_cast<std::ptrdiff_t>(values.size());
  std::vector<double> out(values.size());
  for (std::ptrdiff_t k = 0; k < n; ++k) {
    auto best = values[static_cast<std::size_t>(k)];
    for (auto j = std::max<std::ptrdiff_t>(0, k - radius); j <= std::min(n - 1, k + radius); ++j)
      best = std::max(best, values[static_cast<std::size_t>(j)]);
    out[static_cast<std::size_t>(k)] = best;
  }
  return out;
}

} // namespace

ScaleCurve::ScaleCurve(const std::vector<RoutePoint> &points, const std::int64_t start,
                       const std::int64_t end, const double seconds_per_tick,
                       const ScaleCurveSettings &settings) {
  if (!(seconds_per_tick > 0.0) || end < start)
    return;
  const auto ahead = std::max(1.0, settings.ahead_seconds);
  const auto closest = std::max(1.0, settings.closest_metres);
  const auto widest = std::max(closest, settings.widest_metres);
  fallback_ = closest;
  start_ = start;
  ticks_per_step_ = kScaleStepSeconds / seconds_per_tick;

  // Usable records, thinned, with a segment number that changes at each jump.
  std::vector<Sample> samples;
  std::size_t segment = 0;
  for (const auto &point : points) {
    if (!point.usable)
      continue;
    const auto seconds = static_cast<double>(point.ticks) * seconds_per_tick;
    if (point.break_before)
      ++segment;
    else if (!samples.empty() && seconds - samples.back().seconds < kThinSeconds)
      continue;
    samples.push_back({seconds, point.position, segment});
  }

  const auto start_seconds = static_cast<double>(start) * seconds_per_tick;
  const auto steps = static_cast<std::size_t>(std::floor(static_cast<double>(end - start) *
                                                         seconds_per_tick / kScaleStepSeconds)) +
                     1U;
  const auto low = std::log2(closest);
  const auto high = std::log2(widest);
  const auto nan = std::numeric_limits<double>::quiet_NaN();

  // 1. The requirement: the map's width, in log2 metres, that puts the farthest record of the
  // next `ahead` seconds at kScaleAheadFraction of its radius.
  std::vector<double> required(steps, nan);
  for (std::size_t k = 0; k < steps; ++k) {
    const auto t = start_seconds + static_cast<double>(k) * kScaleStepSeconds;
    const auto after = std::upper_bound(
        samples.begin(), samples.end(), t,
        [](const double value, const Sample &sample) { return value < sample.seconds; });
    std::size_t current = 0;
    if (after != samples.begin() && t - std::prev(after)->seconds <= kStaleSeconds)
      current = static_cast<std::size_t>(std::prev(after) - samples.begin());
    else if (after != samples.end() && after->seconds - t <= kStaleSeconds)
      current = static_cast<std::size_t>(after - samples.begin());
    else
      continue; // no current position: held below
    const auto &here = samples[current];
    double reach = 0.0;
    double covered = 0.0;
    bool complete = false;
    for (auto j = current + 1U; j < samples.size() && samples[j].segment == here.segment; ++j) {
      const auto distance = distance_metres(here.position, samples[j].position);
      const auto dt = samples[j].seconds - t;
      if (dt <= ahead) {
        reach = std::max(reach, distance);
        covered = std::max(covered, dt);
        continue;
      }
      // The first record beyond the look-ahead. After a GPS gap it may be much later: assume
      // steady travel across the gap rather than showing all of it.
      reach = std::max(reach, distance * ahead / dt);
      complete = true;
      break;
    }
    if (!complete) {
      if (covered < kMinimumCoverage * ahead)
        continue; // the end of the track, or just before a jump: held below
      reach *= ahead / covered;
    }
    const auto width = 2.0 * reach / kScaleAheadFraction;
    required[k] = std::clamp(std::log2(std::max(width, 1.0)), low, high);
  }

  // 2. Without a current position, hold the last value (or the first, before it).
  const auto first_valid =
      std::find_if(required.begin(), required.end(), [](const double v) { return !std::isnan(v); });
  if (first_valid == required.end()) {
    widths_.assign(steps, closest);
    return;
  }
  auto held = *first_valid;
  for (auto &value : required) {
    if (std::isnan(value))
      value = held;
    else
      held = value;
  }

  // 3. A short dip in the requirement is not followed.
  const auto hold_radius =
      static_cast<std::ptrdiff_t>(std::lround(kScaleHoldSeconds / 2.0 / kScaleStepSeconds));
  auto curve = sliding_max(required, hold_radius);

  // 4. Never narrower than required, zooming out ahead of time at a bounded rate (backwards),
  // and zooming in slowly and only past the dead band (forwards).
  const auto out_step = kScaleZoomOutRate * kScaleStepSeconds;
  const auto in_step = kScaleZoomInRate * kScaleStepSeconds;
  for (auto k = steps - 1U; k-- > 0U;)
    curve[k] = std::max(curve[k], curve[k + 1U] - out_step);
  for (std::size_t k = 1; k < steps; ++k) {
    const auto previous = curve[k - 1U];
    const auto within_band = previous - curve[k] < kDeadBand && curve[k] > low;
    curve[k] = within_band ? std::max(curve[k], previous) : std::max(curve[k], previous - in_step);
  }

  // 5. Round the corners: a Gaussian over a local maximum, so it stays at or above the curve.
  const auto sigma = kSmoothSeconds / kScaleStepSeconds;
  const auto radius = static_cast<std::ptrdiff_t>(std::ceil(2.0 * sigma));
  const auto lifted = sliding_max(curve, radius);
  std::vector<double> weights;
  for (std::ptrdiff_t j = -2 * radius; j <= 2 * radius; ++j)
    weights.push_back(std::exp(-0.5 * static_cast<double>(j * j) / (sigma * sigma)));
  const auto n = static_cast<std::ptrdiff_t>(steps);
  widths_.resize(steps);
  for (std::ptrdiff_t k = 0; k < n; ++k) {
    double sum = 0.0;
    double total = 0.0;
    for (std::ptrdiff_t j = -2 * radius; j <= 2 * radius; ++j) {
      const auto index = std::clamp<std::ptrdiff_t>(k + j, 0, n - 1);
      const auto weight = weights[static_cast<std::size_t>(j + 2 * radius)];
      sum += weight * lifted[static_cast<std::size_t>(index)];
      total += weight;
    }
    widths_[static_cast<std::size_t>(k)] = std::exp2(std::clamp(sum / total, low, high));
  }
}

double ScaleCurve::width_at(const std::int64_t ticks) const {
  if (widths_.empty())
    return fallback_;
  const auto position = static_cast<double>(ticks - start_) / ticks_per_step_;
  if (!(position > 0.0))
    return widths_.front();
  const auto index = static_cast<std::size_t>(position);
  if (index + 1U >= widths_.size())
    return widths_.back();
  const auto fraction = position - static_cast<double>(index);
  return widths_[index] + (widths_[index + 1U] - widths_[index]) * fraction;
}

ScaleCurve::Extreme ScaleCurve::closest() const {
  if (widths_.empty())
    return {start_, fallback_};
  const auto found = std::min_element(widths_.begin(), widths_.end());
  return {start_ + std::llround(static_cast<double>(found - widths_.begin()) * ticks_per_step_),
          *found};
}

ScaleCurve::Extreme ScaleCurve::widest() const {
  if (widths_.empty())
    return {start_, fallback_};
  const auto found = std::max_element(widths_.begin(), widths_.end());
  return {start_ + std::llround(static_cast<double>(found - widths_.begin()) * ticks_per_step_),
          *found};
}

} // namespace gb::geo
