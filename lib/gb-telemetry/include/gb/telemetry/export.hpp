#pragma once

#include "gb/telemetry/telemetry.hpp"

#include <filesystem>
#include <optional>
#include <ostream>
#include <string_view>
#include <vector>

namespace gb::telemetry {

enum class ExportFormat { json, csv, gpx, geojson };

// One recorded file and its extraction. Media times stay relative to that file.
struct ExportSource {
  std::filesystem::path path;
  Extraction extraction;
};

struct ExportOptions {
  ExportFormat format{ExportFormat::json};
  // GPX and GeoJSON omit invalid and unknown-validity records unless this is set. JSON and CSV
  // always keep every record.
  bool include_invalid{};
};

[[nodiscard]] std::optional<ExportFormat> parse_export_format(std::string_view name);
[[nodiscard]] std::string_view export_format_name(ExportFormat format);
// Conventional file extension, without the dot.
[[nodiscard]] std::string_view export_format_extension(ExportFormat format);

// Writes the sources in the given order. A single source produces the gb-telemetry formats
// unchanged. Several sources (the chapters of one recording) produce one document:
// JSON becomes gb.telemetry.sequence/v1 with one gb.telemetry/v1 object per source, CSV gains a
// leading source_file column, GPX has one track with a segment per source, and GeoJSON features
// gain a source_file property. Throws std::invalid_argument when sources is empty.
void write_export(std::ostream &output, const std::vector<ExportSource> &sources,
                  const ExportOptions &options);

} // namespace gb::telemetry
