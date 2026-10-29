#include "gb/container/iso_bmff.hpp"
#include "gb/metadata/gpmf.hpp"
#include "gb/telemetry/telemetry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kVersion = GB_TELEMETRY_VERSION;

struct Options {
  std::string format{"json"};
  std::optional<std::uint32_t> track_id;
  bool include_invalid{};
  std::filesystem::path path;
};

std::string json_string(const std::string_view input) {
  std::ostringstream output;
  output << '"';
  for (const auto raw : input) {
    const auto byte = static_cast<unsigned char>(raw);
    switch (byte) {
    case '"': output << "\\\""; break;
    case '\\': output << "\\\\"; break;
    case '\n': output << "\\n"; break;
    case '\r': output << "\\r"; break;
    case '\t': output << "\\t"; break;
    default:
      if (byte < 0x20U) {
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<unsigned>(byte) << std::dec;
      } else {
        output << static_cast<char>(byte);
      }
    }
  }
  output << '"';
  return output.str();
}

std::string xml(const std::string_view input) {
  std::string output;
  for (const char byte : input) {
    switch (byte) {
    case '&': output += "&amp;"; break;
    case '<': output += "&lt;"; break;
    case '>': output += "&gt;"; break;
    case '\"': output += "&quot;"; break;
    case '\'': output += "&apos;"; break;
    default: output += byte; break;
    }
  }
  return output;
}

void help() {
  std::cout << "Usage: gb-telemetry [options] <file>\n"
               "       gb-telemetry --help\n"
               "       gb-telemetry --version\n\n"
               "Extract timestamped GPS telemetry without modifying the source.\n\n"
               "Options:\n"
               "  --format FORMAT     json (default), csv, gpx, or geojson\n"
               "  --track ID          select a gpmd track by track ID\n"
               "  --include-invalid   include invalid/unknown-fix points in GPS formats\n"
               "  --help              show this help and exit\n"
               "  --version           show the program version and exit\n";
}

std::optional<std::uint32_t> parse_id(const std::string_view value) {
  if (value.empty() || !std::all_of(value.begin(), value.end(), [](const char byte) {
        return std::isdigit(static_cast<unsigned char>(byte)) != 0;
      })) {
    return std::nullopt;
  }
  try {
    const auto number = std::stoull(std::string(value));
    return number <= UINT32_MAX ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(number))
                                : std::nullopt;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

bool include(const gb::telemetry::GpsPoint &point, const bool include_invalid) {
  return include_invalid || point.validity == gb::telemetry::Validity::valid;
}

void json(const Options &options, const gb::telemetry::Extraction &data) {
  std::cout << std::setprecision(12)
            << "{\n  \"schema\": \"gb.telemetry/v1\",\n"
            << "  \"path\": " << json_string(options.path.string()) << ",\n"
            << "  \"source\": \"gpmf/GPS5\",\n"
            << "  \"track\": {\"id\": " << data.track_id
            << ", \"time_base\": {\"numerator\": " << data.time_base.numerator
            << ", \"denominator\": " << data.time_base.denominator
            << "}, \"duration_ticks\": " << data.duration_ticks
            << ", \"sample_count\": " << data.sample_count << "},\n"
            << "  \"units\": {\"latitude\": \"deg\", \"longitude\": \"deg\", "
               "\"altitude\": \"m\", \"speed_2d\": \"m/s\", \"speed_3d\": "
               "\"m/s\", \"dop\": \"dimensionless\"},\n"
            << "  \"timing\": {\"media_time\": \"integer decode ticks\", "
               "\"record_assignment\": \"uniform_within_gpmf_sample\", "
               "\"utc\": \"recorded GPSU anchor plus derived media offset\"},\n"
            << "  \"points\": [" << (data.gps.empty() ? "" : "\n");
  for (std::size_t index = 0; index < data.gps.size(); ++index) {
    const auto &point = data.gps[index];
    std::cout << "    {\"source_sample\": " << point.source_sample
              << ", \"record_index\": " << point.record_index
              << ", \"media_time_ticks\": " << point.media_time_ticks
              << ", \"timing_uncertainty_ticks\": " << point.timing_uncertainty_ticks
              << ", \"utc\": ";
    point.utc.has_value() ? std::cout << json_string(*point.utc) : std::cout << "null";
    std::cout << ", \"utc_status\": "
              << json_string(point.utc.has_value() ? (point.utc_recorded ? "recorded" : "derived")
                                                    : "unavailable")
              << ", \"latitude\": " << point.latitude << ", \"longitude\": "
              << point.longitude << ", \"altitude\": " << point.altitude
              << ", \"speed_2d\": " << point.speed_2d << ", \"speed_3d\": "
              << point.speed_3d << ", \"fix\": ";
    point.fix.has_value() ? std::cout << *point.fix : std::cout << "null";
    std::cout << ", \"dop\": ";
    point.dop.has_value() ? std::cout << *point.dop : std::cout << "null";
    std::cout << ", \"validity\": " << json_string(gb::telemetry::validity_name(point.validity))
              << ", \"value_status\": \"recorded\"}"
              << (index + 1U == data.gps.size() ? "\n" : ",\n");
  }
  std::cout << (data.gps.empty() ? "" : "  ") << "],\n  \"gaps\": [";
  for (std::size_t index = 0; index < data.gaps.size(); ++index) {
    const auto &gap = data.gaps[index];
    std::cout << (index == 0U ? "" : ", ") << "{\"start_ticks\": " << gap.start_ticks
              << ", \"end_ticks\": " << gap.end_ticks << ", \"reason\": "
              << json_string(gap.reason) << '}';
  }
  std::cout << "],\n  \"warnings\": [";
  for (std::size_t index = 0; index < data.warnings.size(); ++index) {
    std::cout << (index == 0U ? "" : ", ") << json_string(data.warnings[index]);
  }
  std::cout << "]\n}\n";
}

void csv(const gb::telemetry::Extraction &data) {
  std::cout << "media_time_ticks,time_base_numerator,time_base_denominator,utc,utc_status,"
               "latitude_deg,longitude_deg,altitude_m,speed_2d_m_s,speed_3d_m_s,fix,dop,"
               "validity,timing_uncertainty_ticks,source_sample,record_index\n"
            << std::setprecision(12);
  for (const auto &point : data.gps) {
    std::cout << point.media_time_ticks << ',' << point.time_base.numerator << ','
              << point.time_base.denominator << ',' << point.utc.value_or("") << ','
              << (point.utc.has_value() ? (point.utc_recorded ? "recorded" : "derived")
                                        : "unavailable")
              << ',' << point.latitude << ',' << point.longitude << ',' << point.altitude << ','
              << point.speed_2d << ',' << point.speed_3d << ',';
    if (point.fix.has_value()) std::cout << *point.fix;
    std::cout << ',';
    if (point.dop.has_value()) std::cout << *point.dop;
    std::cout << ',' << gb::telemetry::validity_name(point.validity) << ','
              << point.timing_uncertainty_ticks << ',' << point.source_sample << ','
              << point.record_index << '\n';
  }
}

void gpx(const Options &options, const gb::telemetry::Extraction &data) {
  std::cout << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<gpx version=\"1.1\" creator=\"gb-telemetry\" "
               "xmlns=\"http://www.topografix.com/GPX/1/1\" "
               "xmlns:gb=\"urn:gb:telemetry:v1\"><trk><name>"
            << xml(options.path.filename().string()) << "</name><trkseg>\n" << std::setprecision(12);
  for (const auto &point : data.gps) {
    if (!include(point, options.include_invalid)) continue;
    std::cout << "<trkpt lat=\"" << point.latitude << "\" lon=\"" << point.longitude
              << "\"><ele>" << point.altitude << "</ele>";
    if (point.utc.has_value()) std::cout << "<time>" << xml(*point.utc) << "</time>";
    std::cout << "<extensions><gb:mediaTime ticks=\"" << point.media_time_ticks
              << "\" numerator=\"" << point.time_base.numerator << "\" denominator=\""
              << point.time_base.denominator << "\" uncertaintyTicks=\""
              << point.timing_uncertainty_ticks << "\"/><gb:speed2d>" << point.speed_2d
              << "</gb:speed2d><gb:speed3d>" << point.speed_3d << "</gb:speed3d><gb:validity>"
              << gb::telemetry::validity_name(point.validity) << "</gb:validity></extensions></trkpt>\n";
  }
  std::cout << "</trkseg></trk></gpx>\n";
}

void geojson(const Options &options, const gb::telemetry::Extraction &data) {
  std::cout << "{\"type\":\"FeatureCollection\",\"gb_schema\":\"gb.telemetry.geojson/v1\","
               "\"features\":[" << std::setprecision(12);
  auto first = true;
  for (const auto &point : data.gps) {
    if (!include(point, options.include_invalid)) continue;
    std::cout << (first ? "" : ",") << "{\"type\":\"Feature\",\"geometry\":{\"type\":"
                 "\"Point\",\"coordinates\":[" << point.longitude << ',' << point.latitude
              << ',' << point.altitude << "]},\"properties\":{\"media_time_ticks\":"
              << point.media_time_ticks << ",\"time_base_numerator\":"
              << point.time_base.numerator << ",\"time_base_denominator\":"
              << point.time_base.denominator << ",\"utc\":";
    point.utc.has_value() ? std::cout << json_string(*point.utc) : std::cout << "null";
    std::cout << ",\"speed_2d_m_s\":" << point.speed_2d << ",\"speed_3d_m_s\":"
              << point.speed_3d << ",\"validity\":"
              << json_string(gb::telemetry::validity_name(point.validity)) << "}}";
    first = false;
  }
  std::cout << "]}\n";
}

} // namespace

int main(const int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") { help(); return 0; }
    if (argument == "--version") { std::cout << "gb-telemetry " << kVersion << '\n'; return 0; }
    if (argument == "--include-invalid") { options.include_invalid = true; continue; }
    if (argument == "--format" || argument == "--track") {
      if (++index >= argc) { std::cerr << "gb-telemetry: missing value for " << argument << '\n'; return 2; }
      if (argument == "--format") options.format = argv[index];
      else {
        options.track_id = parse_id(argv[index]);
        if (!options.track_id.has_value()) { std::cerr << "gb-telemetry: invalid track ID\n"; return 2; }
      }
      continue;
    }
    if (argument.starts_with('-')) { std::cerr << "gb-telemetry: unknown option: " << argument << '\n'; return 2; }
    if (!options.path.empty()) { std::cerr << "gb-telemetry: expected exactly one input file\n"; return 2; }
    options.path = argument;
  }
  if (options.path.empty()) { std::cerr << "gb-telemetry: missing input file\n"; return 2; }
  if (options.format != "json" && options.format != "csv" && options.format != "gpx" &&
      options.format != "geojson") {
    std::cerr << "gb-telemetry: format must be json, csv, gpx, or geojson\n"; return 2;
  }
  try {
    const auto data = gb::telemetry::extract(options.path, options.track_id);
    if (options.format == "json") json(options, data);
    else if (options.format == "csv") csv(data);
    else if (options.format == "gpx") gpx(options, data);
    else geojson(options, data);
    return 0;
  } catch (const gb::metadata::GpmfError &error) {
    std::cerr << "gb-telemetry: corrupt GPMF at payload byte " << error.offset() << ": " << error.what() << '\n';
    return 4;
  } catch (const gb::container::InspectionError &error) {
    std::cerr << "gb-telemetry: " << gb::container::error_kind_name(error.kind()) << " at byte "
              << error.offset() << ": " << error.what() << '\n';
    return error.kind() == gb::container::ErrorKind::corrupt ? 4 : 3;
  } catch (const std::exception &error) {
    std::cerr << "gb-telemetry: unsupported: " << error.what() << '\n';
    return 5;
  }
}
