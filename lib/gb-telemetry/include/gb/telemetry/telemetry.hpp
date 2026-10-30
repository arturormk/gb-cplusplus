#pragma once

#include "gb/container/iso_bmff.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gb::telemetry {

enum class Validity { valid, invalid, unknown };

struct GpsPoint {
  std::size_t source_sample{};
  std::size_t record_index{};
  std::int64_t media_time_ticks{};
  std::uint64_t timing_uncertainty_ticks{};
  container::Rational time_base;
  std::optional<std::string> utc;
  bool utc_recorded{};
  long double latitude{};
  long double longitude{};
  long double altitude{};
  long double speed_2d{};
  long double speed_3d{};
  std::optional<std::uint32_t> fix;
  std::optional<long double> dop;
  Validity validity{Validity::unknown};
};

struct Gap {
  std::int64_t start_ticks{};
  std::int64_t end_ticks{};
  std::string reason;
};

struct UtcAnchor {
  std::size_t source_sample{};
  std::int64_t media_time_ticks{};
  std::string utc;
};

struct Extraction {
  std::uint32_t track_id{};
  container::Rational time_base;
  std::uint64_t duration_ticks{};
  std::uint32_t sample_count{};
  std::vector<GpsPoint> gps;
  std::vector<Gap> gaps;
  std::vector<UtcAnchor> utc_anchors;
  std::vector<std::string> warnings;
};

[[nodiscard]] Extraction extract(const std::filesystem::path &path,
                                 std::optional<std::uint32_t> track_id = std::nullopt);
// Returns the recorded GPS point nearest to a media tick, preferring the earlier point on ties.
// No position is interpolated. Returns nullptr when no GPS point exists.
[[nodiscard]] const GpsPoint *nearest_point(const Extraction &extraction, std::int64_t media_ticks);
[[nodiscard]] std::optional<long double> forward_azimuth_degrees(long double latitude_1,
                                                                 long double longitude_1,
                                                                 long double latitude_2,
                                                                 long double longitude_2);
// Azimuth of the horizontal velocity at points[selected], in degrees clockwise from true north
// (0 north, 90 east, 180 south, 270 west). It is the direction of travel derived from the nearest
// distinct usable positions before and after the record, not the camera's pointing direction.
// Returns nullopt for an invalid record, 2D speed at or below 0.01 m/s, or no distinct neighbour.
[[nodiscard]] std::optional<long double> motion_azimuth_degrees(const std::vector<GpsPoint> &points,
                                                                std::size_t selected);
[[nodiscard]] std::string validity_name(Validity validity);

} // namespace gb::telemetry
