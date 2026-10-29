#include "gb/telemetry/telemetry.hpp"

#include "gb/metadata/gpmf.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace gb::telemetry {
namespace {

using Element = metadata::GpmfElement;
using Milliseconds = std::chrono::milliseconds;
using TimePoint = std::chrono::sys_time<Milliseconds>;

const container::Track &select_track(const container::Inspection &inspection,
                                     const std::optional<std::uint32_t> id) {
  const container::Track *selected = nullptr;
  for (const auto &track : inspection.tracks) {
    if (track.codec != "gpmd" || (id.has_value() && track.id != *id)) {
      continue;
    }
    if (selected != nullptr && !id.has_value()) {
      throw std::runtime_error("multiple gpmd tracks found; select one explicitly");
    }
    selected = &track;
  }
  if (selected == nullptr) {
    throw std::runtime_error(id.has_value() ? "selected track is not a gpmd track"
                                            : "no gpmd metadata track found");
  }
  return *selected;
}

const Element *direct_child(const Element &parent, const std::string &key) {
  const auto iterator = std::find_if(parent.children.begin(), parent.children.end(),
                                     [&key](const auto &child) { return child.key == key; });
  return iterator == parent.children.end() ? nullptr : &*iterator;
}

void gps_streams(const std::vector<Element> &elements, std::vector<const Element *> &streams) {
  for (const auto &element : elements) {
    if (element.key == "STRM" && direct_child(element, "GPS5") != nullptr) {
      streams.push_back(&element);
    }
    gps_streams(element.children, streams);
  }
}

std::optional<std::uint32_t> unsigned_value(const Element *element) {
  if (element == nullptr || element->values.empty()) {
    return std::nullopt;
  }
  try {
    const auto value = std::stoull(element->values.front());
    if (value > std::numeric_limits<std::uint32_t>::max()) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

std::optional<TimePoint> parse_gpsu(const std::string &value) {
  // GPMF GPSU is YYMMDDhhmmss.sss in UTC.
  if (value.size() != 12U && value.size() != 16U) {
    return std::nullopt;
  }
  const auto digit = [](const char byte) { return byte >= '0' && byte <= '9'; };
  if (!std::all_of(value.begin(), value.begin() + 12, digit) ||
      (value.size() == 16U &&
       (value[12U] != '.' || !std::all_of(value.begin() + 13, value.end(), digit)))) {
    return std::nullopt;
  }
  try {
    const auto number = [&value](const std::size_t offset) {
      return std::stoi(value.substr(offset, 2U));
    };
    const auto year = 2000 + number(0U);
    const auto month = static_cast<unsigned>(number(2U));
    const auto day = static_cast<unsigned>(number(4U));
    const auto hour = number(6U);
    const auto minute = number(8U);
    const auto second = number(10U);
    auto millis = 0;
    if (value.size() == 16U) {
      millis = std::stoi(value.substr(13U, 3U));
    }
    const std::chrono::year_month_day date{std::chrono::year{year},
                                            std::chrono::month{month},
                                            std::chrono::day{day}};
    if (!date.ok() || hour > 23 || minute > 59 || second > 60 || millis > 999) {
      return std::nullopt;
    }
    return TimePoint{std::chrono::sys_days{date}.time_since_epoch()} +
           std::chrono::hours{hour} + std::chrono::minutes{minute} +
           std::chrono::seconds{second} + Milliseconds{millis};
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

std::string format_utc(const TimePoint point) {
  const auto day = std::chrono::floor<std::chrono::days>(point);
  const std::chrono::year_month_day date{day};
  const std::chrono::hh_mm_ss time{point - day};
  std::ostringstream output;
  output << std::setfill('0') << std::setw(4) << static_cast<int>(date.year()) << '-'
         << std::setw(2) << static_cast<unsigned>(date.month()) << '-' << std::setw(2)
         << static_cast<unsigned>(date.day()) << 'T' << std::setw(2) << time.hours().count()
         << ':' << std::setw(2) << time.minutes().count() << ':' << std::setw(2)
         << time.seconds().count() << '.' << std::setw(3)
         << time.subseconds().count() << 'Z';
  return output.str();
}

std::optional<long double> number(const std::string &value) {
  try {
    std::size_t consumed = 0;
    const auto result = std::stold(value, &consumed);
    return consumed == value.size() && std::isfinite(result)
               ? std::optional<long double>(result)
               : std::nullopt;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

void append_gap(std::vector<Gap> &gaps, const std::int64_t start, const std::int64_t end,
                const std::string &reason) {
  if (!gaps.empty() && gaps.back().reason == reason && start <= gaps.back().end_ticks) {
    gaps.back().end_ticks = std::max(gaps.back().end_ticks, end);
    return;
  }
  gaps.push_back({start, end, reason});
}

std::int64_t ticks_to_milliseconds(const std::uint64_t ticks,
                                   const container::Rational time_base) {
  if (time_base.denominator == 0U) {
    throw std::runtime_error("media time base denominator is zero");
  }
  const auto milliseconds = static_cast<long double>(ticks) *
                            static_cast<long double>(time_base.numerator) * 1000.0L /
                            static_cast<long double>(time_base.denominator);
  if (!std::isfinite(milliseconds) || milliseconds < 0.0L ||
      milliseconds > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
    throw std::runtime_error("derived GPS UTC offset overflows");
  }
  return static_cast<std::int64_t>(milliseconds);
}

} // namespace

Extraction extract(const std::filesystem::path &path,
                   const std::optional<std::uint32_t> track_id) {
  const auto inspection = container::inspect_iso_bmff(path);
  const auto &track = select_track(inspection, track_id);
  Extraction result;
  result.track_id = track.id;
  result.time_base = track.time_base;
  result.duration_ticks = track.duration_ticks;
  result.sample_count = track.sample_count;

  for (std::size_t sample_index = 0; sample_index < track.samples.size(); ++sample_index) {
    const auto &sample = track.samples[sample_index];
    const auto payload = container::read_sample(path, sample);
    const auto elements = metadata::parse_gpmf(payload);
    std::vector<const Element *> streams;
    gps_streams(elements, streams);
    for (const auto *stream : streams) {
      const auto *gps = direct_child(*stream, "GPS5");
      const auto &values = gps->normalized_values.empty() ? gps->values : gps->normalized_values;
      if (gps->structure_size != 20U || values.empty() || values.size() % 5U != 0U) {
        result.warnings.push_back("sample " + std::to_string(sample_index) +
                                  " has an unsupported GPS5 layout");
        continue;
      }
      const auto count = values.size() / 5U;
      const auto fix = unsigned_value(direct_child(*stream, "GPSF"));
      const auto raw_dop = unsigned_value(direct_child(*stream, "GPSP"));
      const auto dop = raw_dop.has_value()
                           ? std::optional<long double>(static_cast<long double>(*raw_dop) / 100.0L)
                           : std::nullopt;
      std::optional<TimePoint> anchor;
      const auto *gpsu = direct_child(*stream, "GPSU");
      if (gpsu != nullptr && !gpsu->values.empty()) {
        anchor = parse_gpsu(gpsu->values.front());
        if (anchor.has_value()) {
          result.utc_anchors.push_back(
              {sample_index, sample.presentation_time, format_utc(*anchor)});
        } else {
          result.warnings.push_back("sample " + std::to_string(sample_index) +
                                    " has a malformed GPSU value");
        }
      }
      const auto uncertainty = std::max<std::uint64_t>(
          1U, (static_cast<std::uint64_t>(sample.duration) + count - 1U) / count);
      for (std::size_t record = 0; record < count; ++record) {
        std::array<long double, 5> decoded{};
        auto decoded_ok = true;
        for (std::size_t component = 0; component < decoded.size(); ++component) {
          const auto parsed = number(values[record * decoded.size() + component]);
          if (!parsed.has_value()) {
            decoded_ok = false;
            break;
          }
          decoded[component] = *parsed;
        }
        if (!decoded_ok) {
          result.warnings.push_back("sample " + std::to_string(sample_index) +
                                    " has a non-numeric GPS5 record");
          continue;
        }
        const auto offset = static_cast<std::uint64_t>(record) * sample.duration / count;
        GpsPoint point;
        point.source_sample = sample_index;
        point.record_index = record;
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            sample.presentation_time >
                std::numeric_limits<std::int64_t>::max() - static_cast<std::int64_t>(offset)) {
          throw std::runtime_error("GPS record presentation time overflows");
        }
        point.media_time_ticks = sample.presentation_time + static_cast<std::int64_t>(offset);
        point.timing_uncertainty_ticks = uncertainty;
        point.time_base = track.time_base;
        point.latitude = decoded[0];
        point.longitude = decoded[1];
        point.altitude = decoded[2];
        point.speed_2d = decoded[3];
        point.speed_3d = decoded[4];
        point.fix = fix;
        point.dop = dop;
        point.validity = !fix.has_value() ? Validity::unknown
                         : (*fix == 2U || *fix == 3U) ? Validity::valid
                                                      : Validity::invalid;
        if (anchor.has_value()) {
          const auto milliseconds = ticks_to_milliseconds(offset, track.time_base);
          point.utc = format_utc(*anchor + Milliseconds{milliseconds});
          point.utc_recorded = record == 0U;
        }
        if (point.validity == Validity::invalid) {
          append_gap(result.gaps, point.media_time_ticks,
                     point.media_time_ticks +
                         static_cast<std::int64_t>(point.timing_uncertainty_ticks),
                     "invalid_gps_fix");
        }
        result.gps.push_back(std::move(point));
      }
    }
  }

  std::stable_sort(result.gps.begin(), result.gps.end(), [](const auto &left, const auto &right) {
    return left.media_time_ticks < right.media_time_ticks;
  });
  std::stable_sort(result.utc_anchors.begin(), result.utc_anchors.end(),
                   [](const auto &left, const auto &right) {
                     return left.media_time_ticks < right.media_time_ticks;
                   });

  for (std::size_t index = 1; index < result.gps.size(); ++index) {
    const auto &previous = result.gps[index - 1U];
    const auto &current = result.gps[index];
    const auto threshold = static_cast<std::int64_t>(
        std::max<std::uint64_t>(previous.timing_uncertainty_ticks * 3U, 1U));
    if (current.media_time_ticks > previous.media_time_ticks + threshold) {
      append_gap(result.gaps,
                 previous.media_time_ticks +
                     static_cast<std::int64_t>(previous.timing_uncertainty_ticks),
                 current.media_time_ticks, "missing_gps_samples");
    }
  }
  if (result.gps.empty()) {
    result.warnings.emplace_back("no GPS5 telemetry found");
    if (result.duration_ticks <=
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      append_gap(result.gaps, 0, static_cast<std::int64_t>(result.duration_ticks),
                 "no_gps_records");
    }
  } else if (result.duration_ticks <=
             static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    if (result.gps.front().media_time_ticks > 0) {
      append_gap(result.gaps, 0, result.gps.front().media_time_ticks, "missing_gps_samples");
    }
    const auto track_end = static_cast<std::int64_t>(result.duration_ticks);
    const auto uncertainty = static_cast<std::int64_t>(result.gps.back().timing_uncertainty_ticks);
    const auto last_end = result.gps.back().media_time_ticks >
                                  std::numeric_limits<std::int64_t>::max() - uncertainty
                              ? std::numeric_limits<std::int64_t>::max()
                              : result.gps.back().media_time_ticks + uncertainty;
    if (last_end < track_end) {
      append_gap(result.gaps, last_end, track_end, "missing_gps_samples");
    }
  }
  std::sort(result.gaps.begin(), result.gaps.end(), [](const auto &left, const auto &right) {
    return left.start_ticks < right.start_ticks;
  });
  return result;
}

std::string validity_name(const Validity validity) {
  switch (validity) {
  case Validity::valid:
    return "valid";
  case Validity::invalid:
    return "invalid";
  case Validity::unknown:
    return "unknown";
  }
  return "unknown";
}

} // namespace gb::telemetry
