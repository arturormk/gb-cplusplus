#include "gb/container/iso_bmff.hpp"
#include "gb/metadata/gpmf.hpp"
#include "gb/telemetry/export.hpp"
#include "gb/telemetry/telemetry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kVersion = GB_TELEMETRY_VERSION;

struct Options {
  gb::telemetry::ExportOptions export_options;
  std::optional<std::uint32_t> track_id;
  std::filesystem::path path;
};

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

} // namespace

int main(const int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      help();
      return 0;
    }
    if (argument == "--version") {
      std::cout << "gb-telemetry " << kVersion << '\n';
      return 0;
    }
    if (argument == "--include-invalid") {
      options.export_options.include_invalid = true;
      continue;
    }
    if (argument == "--format" || argument == "--track") {
      if (++index >= argc) {
        std::cerr << "gb-telemetry: missing value for " << argument << '\n';
        return 2;
      }
      if (argument == "--format") {
        const auto format = gb::telemetry::parse_export_format(argv[index]);
        if (!format.has_value()) {
          std::cerr << "gb-telemetry: format must be json, csv, gpx, or geojson\n";
          return 2;
        }
        options.export_options.format = *format;
      } else {
        options.track_id = parse_id(argv[index]);
        if (!options.track_id.has_value()) {
          std::cerr << "gb-telemetry: invalid track ID\n";
          return 2;
        }
      }
      continue;
    }
    if (argument.starts_with('-')) {
      std::cerr << "gb-telemetry: unknown option: " << argument << '\n';
      return 2;
    }
    if (!options.path.empty()) {
      std::cerr << "gb-telemetry: expected exactly one input file\n";
      return 2;
    }
    options.path = argument;
  }
  if (options.path.empty()) {
    std::cerr << "gb-telemetry: missing input file\n";
    return 2;
  }
  try {
    const std::vector<gb::telemetry::ExportSource> sources{
        {options.path, gb::telemetry::extract(options.path, options.track_id)}};
    gb::telemetry::write_export(std::cout, sources, options.export_options);
    return 0;
  } catch (const gb::metadata::GpmfError &error) {
    std::cerr << "gb-telemetry: corrupt GPMF at payload byte " << error.offset() << ": "
              << error.what() << '\n';
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
