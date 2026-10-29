#include "gb/container/iso_bmff.hpp"
#include "gb/metadata/gpmf.hpp"
#include "gb/telemetry/telemetry.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kVersion = GB_TIME_VERSION;

struct Options {
  bool json{};
  std::optional<std::uint32_t> track_id;
  std::optional<std::int64_t> at_ticks;
  std::filesystem::path path;
};

struct Discontinuity {
  std::size_t after_sample{};
  std::uint64_t expected{};
  std::uint64_t actual{};
};

std::string json_string(const std::string_view input) {
  std::ostringstream output;
  output << '"';
  for (const char byte : input) {
    if (byte == '"') output << "\\\"";
    else if (byte == '\\') output << "\\\\";
    else if (byte == '\n') output << "\\n";
    else output << byte;
  }
  output << '"';
  return output.str();
}

void help() {
  std::cout << "Usage: gb-time [options] <file>\n"
               "       gb-time --help\n"
               "       gb-time --version\n\n"
               "Inspect integer media timing and GPS UTC synchronization anchors.\n\n"
               "Options:\n"
               "  --json        emit schema-versioned JSON\n"
               "  --track ID    select a gpmd track when more than one exists\n"
               "  --at TICKS    report the closest GPS record at an integer media time\n"
               "  --help        show this help and exit\n"
               "  --version     show the program version and exit\n";
}

std::optional<std::uint64_t> unsigned_number(const std::string_view value) {
  if (value.empty() || !std::all_of(value.begin(), value.end(), [](const char byte) {
        return std::isdigit(static_cast<unsigned char>(byte)) != 0;
      })) return std::nullopt;
  try { return std::stoull(std::string(value)); }
  catch (const std::exception &) { return std::nullopt; }
}

std::optional<std::int64_t> signed_number(const std::string_view value) {
  if (value.empty()) return std::nullopt;
  try {
    std::size_t consumed = 0;
    const auto result = std::stoll(std::string(value), &consumed);
    return consumed == value.size() ? std::optional<std::int64_t>(result) : std::nullopt;
  } catch (const std::exception &) { return std::nullopt; }
}

std::vector<Discontinuity> discontinuities(const gb::container::Track &track) {
  std::vector<Discontinuity> result;
  for (std::size_t index = 1; index < track.samples.size(); ++index) {
    const auto expected = track.samples[index - 1U].decode_time + track.samples[index - 1U].duration;
    if (track.samples[index].decode_time != expected) {
      result.push_back({index - 1U, expected, track.samples[index].decode_time});
    }
  }
  return result;
}

std::optional<std::chrono::sys_time<std::chrono::milliseconds>> parse_iso(const std::string &text) {
  if (text.size() != 24U) return std::nullopt;
  try {
    const auto year = std::stoi(text.substr(0U, 4U));
    const auto month = static_cast<unsigned>(std::stoi(text.substr(5U, 2U)));
    const auto day = static_cast<unsigned>(std::stoi(text.substr(8U, 2U)));
    const auto hour = std::stoi(text.substr(11U, 2U));
    const auto minute = std::stoi(text.substr(14U, 2U));
    const auto second = std::stoi(text.substr(17U, 2U));
    const auto millis = std::stoi(text.substr(20U, 3U));
    const std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{month},
                                            std::chrono::day{day}};
    if (!date.ok()) return std::nullopt;
    return std::chrono::sys_time<std::chrono::milliseconds>{
               std::chrono::sys_days{date}.time_since_epoch()} +
           std::chrono::hours{hour} + std::chrono::minutes{minute} +
           std::chrono::seconds{second} + std::chrono::milliseconds{millis};
  } catch (const std::exception &) { return std::nullopt; }
}

long double drift_ms(const gb::telemetry::UtcAnchor &first,
                     const gb::telemetry::UtcAnchor &second,
                     const gb::container::Rational base) {
  const auto first_utc = parse_iso(first.utc);
  const auto second_utc = parse_iso(second.utc);
  if (!first_utc.has_value() || !second_utc.has_value()) return 0.0L;
  const auto utc_delta = static_cast<long double>((*second_utc - *first_utc).count());
  const auto tick_delta = second.media_time_ticks - first.media_time_ticks;
  const auto media_delta = static_cast<long double>(tick_delta) * base.numerator * 1000.0L /
                           static_cast<long double>(base.denominator);
  return utc_delta - media_delta;
}

const gb::telemetry::GpsPoint *nearest(const gb::telemetry::Extraction &data,
                                       const std::int64_t ticks) {
  if (data.gps.empty()) return nullptr;
  return &*std::min_element(data.gps.begin(), data.gps.end(), [ticks](const auto &left, const auto &right) {
    const auto left_distance = std::fabs(static_cast<long double>(left.media_time_ticks) - ticks);
    const auto right_distance = std::fabs(static_cast<long double>(right.media_time_ticks) - ticks);
    return left_distance < right_distance;
  });
}

void print_json(const Options &options, const gb::container::Inspection &inspection,
                const gb::telemetry::Extraction &telemetry) {
  std::cout << "{\n  \"schema\": \"gb.time/v1\",\n  \"path\": "
            << json_string(options.path.string()) << ",\n  \"tracks\": ["
            << (inspection.tracks.empty() ? "" : "\n");
  for (std::size_t index = 0; index < inspection.tracks.size(); ++index) {
    const auto &track = inspection.tracks[index];
    const auto gaps = discontinuities(track);
    const auto composition_offsets = std::count_if(
        track.samples.begin(), track.samples.end(),
        [](const auto &sample) {
          return sample.presentation_time != static_cast<std::int64_t>(sample.decode_time);
        });
    std::cout << "    {\"id\": " << track.id << ", \"handler\": "
              << json_string(track.handler_type) << ", \"codec\": " << json_string(track.codec)
              << ", \"time_base\": {\"numerator\": " << track.time_base.numerator
              << ", \"denominator\": " << track.time_base.denominator
              << "}, \"duration_ticks\": " << track.duration_ticks
              << ", \"sample_count\": " << track.sample_count
              << ", \"mapped_sample_count\": " << track.samples.size()
              << ", \"composition_offset_sample_count\": " << composition_offsets
              << ", \"decode_discontinuities\": [";
    for (std::size_t gap = 0; gap < gaps.size(); ++gap) {
      std::cout << (gap == 0U ? "" : ", ") << "{\"after_sample\": " << gaps[gap].after_sample
                << ", \"expected_ticks\": " << gaps[gap].expected << ", \"actual_ticks\": "
                << gaps[gap].actual << '}';
    }
    std::cout << "]}" << (index + 1U == inspection.tracks.size() ? "\n" : ",\n");
  }
  std::cout << (inspection.tracks.empty() ? "" : "  ") << "],\n  \"gps_clock\": {\n"
            << "    \"track_id\": ";
  telemetry.track_id == 0U ? std::cout << "null" : std::cout << telemetry.track_id;
  std::cout << ",\n"
            << "    \"mapping\": \"piecewise GPSU anchors; GPS5 records uniformly assigned within each GPMF sample\",\n"
            << "    \"uncertainty\": \"one derived GPS record interval; no capture-time mapping claimed\",\n"
            << "    \"anchors\": [";
  for (std::size_t index = 0; index < telemetry.utc_anchors.size(); ++index) {
    const auto &anchor = telemetry.utc_anchors[index];
    std::cout << (index == 0U ? "" : ", ") << "{\"source_sample\": " << anchor.source_sample
              << ", \"media_time_ticks\": " << anchor.media_time_ticks << ", \"utc\": "
              << json_string(anchor.utc) << '}';
  }
  std::cout << "],\n    \"anchor_intervals\": [" << std::setprecision(12);
  for (std::size_t index = 1; index < telemetry.utc_anchors.size(); ++index) {
    std::cout << (index == 1U ? "" : ", ") << "{\"from_sample\": "
              << telemetry.utc_anchors[index - 1U].source_sample << ", \"to_sample\": "
              << telemetry.utc_anchors[index].source_sample << ", \"media_delta_ticks\": "
              << telemetry.utc_anchors[index].media_time_ticks -
                     telemetry.utc_anchors[index - 1U].media_time_ticks
              << ", \"utc_minus_media_ms_estimate\": "
              << drift_ms(telemetry.utc_anchors[index - 1U], telemetry.utc_anchors[index],
                          telemetry.time_base) << '}';
  }
  std::cout << "]\n  },\n  \"warnings\": [";
  for (std::size_t index = 0; index < telemetry.warnings.size(); ++index) {
    std::cout << (index == 0U ? "" : ", ") << json_string(telemetry.warnings[index]);
  }
  std::cout << ']';
  if (options.at_ticks.has_value()) {
    const auto *point = nearest(telemetry, *options.at_ticks);
    std::cout << ",\n  \"query\": {\"requested_ticks\": " << *options.at_ticks << ", \"nearest\": ";
    if (point == nullptr) std::cout << "null";
    else {
      std::cout << "{\"media_time_ticks\": " << point->media_time_ticks
                << ", \"timing_uncertainty_ticks\": " << point->timing_uncertainty_ticks
                << ", \"utc\": ";
      point->utc.has_value() ? std::cout << json_string(*point->utc) : std::cout << "null";
      std::cout << ", \"validity\": "
                << json_string(gb::telemetry::validity_name(point->validity)) << '}';
    }
    std::cout << '}';
  }
  std::cout << "\n}\n";
}

void print_text(const Options &options, const gb::container::Inspection &inspection,
                const gb::telemetry::Extraction &telemetry) {
  std::cout << "file: " << options.path.string() << "\ntracks:\n";
  for (const auto &track : inspection.tracks) {
    const auto gaps = discontinuities(track);
    const auto composition_offsets = std::count_if(
        track.samples.begin(), track.samples.end(),
        [](const auto &sample) {
          return sample.presentation_time != static_cast<std::int64_t>(sample.decode_time);
        });
    std::cout << "  id=" << track.id << " handler=" << track.handler_type << " codec="
              << track.codec << " time_base=" << track.time_base.numerator << '/'
              << track.time_base.denominator << " duration_ticks=" << track.duration_ticks
              << " samples=" << track.sample_count << " mapped_samples=" << track.samples.size()
              << " composition_offsets=" << composition_offsets
              << " decode_discontinuities=" << gaps.size()
              << '\n';
  }
  std::cout << "gps_clock:\n  track=";
  telemetry.track_id == 0U ? std::cout << "unavailable" : std::cout << telemetry.track_id;
  std::cout << " anchors="
            << telemetry.utc_anchors.size() << " gps_records=" << telemetry.gps.size()
            << "\n  mapping=piecewise GPSU anchors; records uniformly assigned within GPMF samples"
               "\n  uncertainty=one GPS record interval; capture-time mapping unavailable\n";
  for (std::size_t index = 1; index < telemetry.utc_anchors.size(); ++index) {
    std::cout << "  anchor_interval " << telemetry.utc_anchors[index - 1U].source_sample << ".."
              << telemetry.utc_anchors[index].source_sample << " media_delta_ticks="
              << telemetry.utc_anchors[index].media_time_ticks -
                     telemetry.utc_anchors[index - 1U].media_time_ticks
              << " utc_minus_media_ms_estimate=" << std::setprecision(8)
              << drift_ms(telemetry.utc_anchors[index - 1U], telemetry.utc_anchors[index],
                          telemetry.time_base) << '\n';
  }
  for (const auto &warning : telemetry.warnings) {
    std::cout << "  warning=" << warning << '\n';
  }
  if (options.at_ticks.has_value()) {
    const auto *point = nearest(telemetry, *options.at_ticks);
    std::cout << "query: requested_ticks=" << *options.at_ticks;
    if (point == nullptr) std::cout << " nearest=unavailable\n";
    else std::cout << " nearest_ticks=" << point->media_time_ticks << " uncertainty_ticks="
                   << point->timing_uncertainty_ticks << " utc=" << point->utc.value_or("unavailable")
                   << " validity=" << gb::telemetry::validity_name(point->validity) << '\n';
  }
}

} // namespace

int main(const int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") { help(); return 0; }
    if (argument == "--version") { std::cout << "gb-time " << kVersion << '\n'; return 0; }
    if (argument == "--json") { options.json = true; continue; }
    if (argument == "--track" || argument == "--at") {
      if (++index >= argc) { std::cerr << "gb-time: missing value for " << argument << '\n'; return 2; }
      const auto unsigned_value = unsigned_number(argv[index]);
      const auto signed_value = signed_number(argv[index]);
      if ((argument == "--track" &&
           (!unsigned_value.has_value() || *unsigned_value > UINT32_MAX)) ||
          (argument == "--at" && !signed_value.has_value())) {
        std::cerr << "gb-time: invalid numeric value for " << argument << '\n'; return 2;
      }
      if (argument == "--track") {
        options.track_id = static_cast<std::uint32_t>(*unsigned_value);
      } else {
        options.at_ticks = *signed_value;
      }
      continue;
    }
    if (argument.starts_with('-')) { std::cerr << "gb-time: unknown option: " << argument << '\n'; return 2; }
    if (!options.path.empty()) { std::cerr << "gb-time: expected exactly one input file\n"; return 2; }
    options.path = argument;
  }
  if (options.path.empty()) { std::cerr << "gb-time: missing input file\n"; return 2; }
  try {
    const auto inspection = gb::container::inspect_iso_bmff(options.path);
    const auto has_gpmd = std::any_of(inspection.tracks.begin(), inspection.tracks.end(),
                                      [&options](const auto &track) {
      return track.codec == "gpmd" &&
             (!options.track_id.has_value() || track.id == *options.track_id);
    });
    gb::telemetry::Extraction telemetry;
    if (has_gpmd || options.track_id.has_value()) {
      telemetry = gb::telemetry::extract(options.path, options.track_id);
    } else {
      telemetry.warnings.emplace_back("no gpmd metadata track found");
    }
    options.json ? print_json(options, inspection, telemetry)
                 : print_text(options, inspection, telemetry);
    return 0;
  } catch (const gb::metadata::GpmfError &error) {
    std::cerr << "gb-time: corrupt GPMF at payload byte " << error.offset() << ": " << error.what() << '\n';
    return 4;
  } catch (const gb::container::InspectionError &error) {
    std::cerr << "gb-time: " << gb::container::error_kind_name(error.kind()) << " at byte "
              << error.offset() << ": " << error.what() << '\n';
    return error.kind() == gb::container::ErrorKind::corrupt ? 4 : 3;
  } catch (const std::exception &error) {
    std::cerr << "gb-time: unsupported: " << error.what() << '\n';
    return 5;
  }
}
