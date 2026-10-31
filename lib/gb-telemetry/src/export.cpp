#include "gb/telemetry/export.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace gb::telemetry {
namespace {

std::string json_string(const std::string_view input) {
  std::ostringstream output;
  output << '"';
  for (const auto raw : input) {
    const auto byte = static_cast<unsigned char>(raw);
    switch (byte) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
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
    case '&':
      output += "&amp;";
      break;
    case '<':
      output += "&lt;";
      break;
    case '>':
      output += "&gt;";
      break;
    case '\"':
      output += "&quot;";
      break;
    case '\'':
      output += "&apos;";
      break;
    default:
      output += byte;
      break;
    }
  }
  return output;
}

std::string csv_field(const std::string_view input) {
  if (input.find_first_of(",\"\r\n") == std::string_view::npos)
    return std::string(input);
  std::string output{"\""};
  for (const char byte : input) {
    if (byte == '"')
      output += '"';
    output += byte;
  }
  return output + '"';
}

std::string file_name(const ExportSource &source) { return source.path.filename().string(); }

bool include(const GpsPoint &point, const bool include_invalid) {
  return include_invalid || point.validity == Validity::valid;
}

void json_document(std::ostream &output, const ExportSource &source) {
  const auto &data = source.extraction;
  output << std::setprecision(12) << "{\n  \"schema\": \"gb.telemetry/v1\",\n"
         << "  \"path\": " << json_string(source.path.string()) << ",\n"
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
    output << "    {\"source_sample\": " << point.source_sample
           << ", \"record_index\": " << point.record_index
           << ", \"media_time_ticks\": " << point.media_time_ticks
           << ", \"timing_uncertainty_ticks\": " << point.timing_uncertainty_ticks << ", \"utc\": ";
    point.utc.has_value() ? output << json_string(*point.utc) : output << "null";
    output << ", \"utc_status\": "
           << json_string(point.utc.has_value() ? (point.utc_recorded ? "recorded" : "derived")
                                                : "unavailable")
           << ", \"latitude\": " << point.latitude << ", \"longitude\": " << point.longitude
           << ", \"altitude\": " << point.altitude << ", \"speed_2d\": " << point.speed_2d
           << ", \"speed_3d\": " << point.speed_3d << ", \"fix\": ";
    point.fix.has_value() ? output << *point.fix : output << "null";
    output << ", \"dop\": ";
    point.dop.has_value() ? output << *point.dop : output << "null";
    output << ", \"validity\": " << json_string(validity_name(point.validity))
           << ", \"value_status\": \"recorded\"}" << (index + 1U == data.gps.size() ? "\n" : ",\n");
  }
  output << (data.gps.empty() ? "" : "  ") << "],\n  \"gaps\": [";
  for (std::size_t index = 0; index < data.gaps.size(); ++index) {
    const auto &gap = data.gaps[index];
    output << (index == 0U ? "" : ", ") << "{\"start_ticks\": " << gap.start_ticks
           << ", \"end_ticks\": " << gap.end_ticks << ", \"reason\": " << json_string(gap.reason)
           << '}';
  }
  output << "],\n  \"warnings\": [";
  for (std::size_t index = 0; index < data.warnings.size(); ++index) {
    output << (index == 0U ? "" : ", ") << json_string(data.warnings[index]);
  }
  output << "]\n}";
}

void json(std::ostream &output, const std::vector<ExportSource> &sources) {
  if (sources.size() == 1U) {
    json_document(output, sources.front());
    output << '\n';
    return;
  }
  output << "{\n  \"schema\": \"gb.telemetry.sequence/v1\",\n  \"sources\": [\n";
  for (std::size_t index = 0; index < sources.size(); ++index) {
    std::ostringstream document;
    json_document(document, sources[index]);
    std::istringstream lines(document.str());
    std::string line;
    auto first = true;
    while (std::getline(lines, line)) {
      output << (first ? "" : "\n") << "    " << line;
      first = false;
    }
    output << (index + 1U == sources.size() ? "\n" : ",\n");
  }
  output << "  ]\n}\n";
}

void csv(std::ostream &output, const std::vector<ExportSource> &sources) {
  const auto sequence = sources.size() > 1U;
  output << (sequence ? "source_file," : "")
         << "media_time_ticks,time_base_numerator,time_base_denominator,utc,utc_status,"
            "latitude_deg,longitude_deg,altitude_m,speed_2d_m_s,speed_3d_m_s,fix,dop,"
            "validity,timing_uncertainty_ticks,source_sample,record_index\n"
         << std::setprecision(12);
  for (const auto &source : sources) {
    const auto name = csv_field(file_name(source));
    for (const auto &point : source.extraction.gps) {
      if (sequence)
        output << name << ',';
      output << point.media_time_ticks << ',' << point.time_base.numerator << ','
             << point.time_base.denominator << ',' << point.utc.value_or("") << ','
             << (point.utc.has_value() ? (point.utc_recorded ? "recorded" : "derived")
                                       : "unavailable")
             << ',' << point.latitude << ',' << point.longitude << ',' << point.altitude << ','
             << point.speed_2d << ',' << point.speed_3d << ',';
      if (point.fix.has_value())
        output << *point.fix;
      output << ',';
      if (point.dop.has_value())
        output << *point.dop;
      output << ',' << validity_name(point.validity) << ',' << point.timing_uncertainty_ticks << ','
             << point.source_sample << ',' << point.record_index << '\n';
    }
  }
}

void gpx(std::ostream &output, const std::vector<ExportSource> &sources,
         const bool include_invalid) {
  const auto sequence = sources.size() > 1U;
  const auto name = sequence ? file_name(sources.front()) + " to " + file_name(sources.back())
                             : file_name(sources.front());
  output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<gpx version=\"1.1\" creator=\"gb-telemetry\" "
            "xmlns=\"http://www.topografix.com/GPX/1/1\" "
            "xmlns:gb=\"urn:gb:telemetry:v1\"><trk><name>"
         << xml(name) << "</name>" << std::setprecision(12);
  for (std::size_t index = 0; index < sources.size(); ++index) {
    output << "<trkseg>\n";
    for (const auto &point : sources[index].extraction.gps) {
      if (!include(point, include_invalid))
        continue;
      output << "<trkpt lat=\"" << point.latitude << "\" lon=\"" << point.longitude << "\"><ele>"
             << point.altitude << "</ele>";
      if (point.utc.has_value())
        output << "<time>" << xml(*point.utc) << "</time>";
      output << "<extensions><gb:mediaTime ticks=\"" << point.media_time_ticks << "\" numerator=\""
             << point.time_base.numerator << "\" denominator=\"" << point.time_base.denominator
             << "\" uncertaintyTicks=\"" << point.timing_uncertainty_ticks << "\"/><gb:speed2d>"
             << point.speed_2d << "</gb:speed2d><gb:speed3d>" << point.speed_3d
             << "</gb:speed3d><gb:validity>" << validity_name(point.validity)
             << "</gb:validity></extensions></trkpt>\n";
    }
    // Media times restart in every file: name the file each segment's times belong to.
    if (sequence) {
      output << "<extensions><gb:source>" << xml(file_name(sources[index]))
             << "</gb:source></extensions>";
    }
    output << "</trkseg>" << (index + 1U == sources.size() ? "" : "\n");
  }
  output << "</trk></gpx>\n";
}

void geojson(std::ostream &output, const std::vector<ExportSource> &sources,
             const bool include_invalid) {
  const auto sequence = sources.size() > 1U;
  output << "{\"type\":\"FeatureCollection\",\"gb_schema\":\"gb.telemetry.geojson/v1\","
            "\"features\":["
         << std::setprecision(12);
  auto first = true;
  for (const auto &source : sources) {
    const auto name = json_string(file_name(source));
    for (const auto &point : source.extraction.gps) {
      if (!include(point, include_invalid))
        continue;
      output << (first ? "" : ",")
             << "{\"type\":\"Feature\",\"geometry\":{\"type\":"
                "\"Point\",\"coordinates\":["
             << point.longitude << ',' << point.latitude << ',' << point.altitude
             << "]},\"properties\":{\"media_time_ticks\":" << point.media_time_ticks
             << ",\"time_base_numerator\":" << point.time_base.numerator
             << ",\"time_base_denominator\":" << point.time_base.denominator << ",\"utc\":";
      point.utc.has_value() ? output << json_string(*point.utc) : output << "null";
      output << ",\"speed_2d_m_s\":" << point.speed_2d << ",\"speed_3d_m_s\":" << point.speed_3d
             << ",\"validity\":" << json_string(validity_name(point.validity));
      if (sequence)
        output << ",\"source_file\":" << name;
      output << "}}";
      first = false;
    }
  }
  output << "]}\n";
}

} // namespace

std::optional<ExportFormat> parse_export_format(const std::string_view name) {
  if (name == "json")
    return ExportFormat::json;
  if (name == "csv")
    return ExportFormat::csv;
  if (name == "gpx")
    return ExportFormat::gpx;
  if (name == "geojson")
    return ExportFormat::geojson;
  return std::nullopt;
}

std::string_view export_format_name(const ExportFormat format) {
  switch (format) {
  case ExportFormat::json:
    return "json";
  case ExportFormat::csv:
    return "csv";
  case ExportFormat::gpx:
    return "gpx";
  case ExportFormat::geojson:
    return "geojson";
  }
  return "json";
}

std::string_view export_format_extension(const ExportFormat format) {
  return export_format_name(format);
}

void write_export(std::ostream &output, const std::vector<ExportSource> &sources,
                  const ExportOptions &options) {
  if (sources.empty())
    throw std::invalid_argument("no telemetry sources to export");
  switch (options.format) {
  case ExportFormat::json:
    json(output, sources);
    break;
  case ExportFormat::csv:
    csv(output, sources);
    break;
  case ExportFormat::gpx:
    gpx(output, sources, options.include_invalid);
    break;
  case ExportFormat::geojson:
    geojson(output, sources, options.include_invalid);
    break;
  }
}

} // namespace gb::telemetry
