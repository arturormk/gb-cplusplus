#include "gb/container/iso_bmff.hpp"
#include "gb/metadata/gpmf.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kVersion = GB_METADATA_VERSION;

struct Options {
  bool json{};
  bool values{};
  std::optional<std::uint32_t> track_id;
  std::optional<std::size_t> sample_index;
  std::optional<std::string> key;
  std::optional<std::filesystem::path> raw_directory;
  std::filesystem::path path;
};

struct FieldSummary {
  std::string key;
  std::set<std::string> types;
  std::set<std::uint8_t> structure_sizes;
  std::set<std::string> units;
  std::uint64_t occurrences{};
  std::uint64_t record_count{};
  std::uint64_t value_count{};
  std::size_t first_sample{};
  std::size_t last_sample{};
  bool has_normalized_values{};
  bool known{};
  std::string raw_preview;
};

std::string json_string(const std::string_view input) {
  std::ostringstream output;
  output << '"';
  constexpr char hex[] = "0123456789abcdef";
  for (const char raw : input) {
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
      if (byte < 0x20U || byte >= 0x7fU) {
        output << "\\u00" << hex[(byte >> 4U) & 0x0fU] << hex[byte & 0x0fU];
      } else {
        output << static_cast<char>(byte);
      }
    }
  }
  output << '"';
  return output.str();
}

std::string hex_bytes(const std::vector<std::uint8_t> &bytes,
                      const std::size_t limit = std::numeric_limits<std::size_t>::max()) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  const auto count = std::min(bytes.size(), limit);
  for (std::size_t index = 0; index < count; ++index) {
    output << std::setw(2) << static_cast<unsigned>(bytes[index]);
  }
  return output.str();
}

std::string display_text(const std::string_view value) {
  std::ostringstream output;
  constexpr char hex[] = "0123456789abcdef";
  for (const char raw : value) {
    const auto byte = static_cast<unsigned char>(raw);
    if (byte >= 0x20U && byte < 0x7fU) {
      output << static_cast<char>(byte);
    } else {
      output << "\\x" << hex[(byte >> 4U) & 0x0fU] << hex[byte & 0x0fU];
    }
  }
  return output.str();
}

std::optional<std::uint64_t> parse_unsigned(const std::string_view value) {
  if (value.empty() || !std::all_of(value.begin(), value.end(), [](const char byte) {
        return std::isdigit(static_cast<unsigned char>(byte)) != 0;
      })) {
    return std::nullopt;
  }
  try {
    std::size_t consumed = 0;
    const auto result = std::stoull(std::string(value), &consumed);
    return consumed == value.size() ? std::optional<std::uint64_t>(result) : std::nullopt;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

void print_help(std::ostream &output) {
  output << "Usage: gb-metadata [options] <file>\n"
            "       gb-metadata --help\n"
            "       gb-metadata --version\n\n"
            "Inventory and inspect timed GoPro GPMF metadata without modifying the source.\n\n"
            "Options:\n"
            "  --json            emit schema-versioned JSON\n"
            "  --track ID        select a gpmd track by track ID\n"
            "  --sample INDEX    show one zero-based sample with raw and normalized values\n"
            "  --key FOURCC      filter friendly text or inventory output to one key\n"
            "  --values          print matching values across the track (requires --key)\n"
            "  --dump-raw DIR    export selected raw GPMF sample payloads\n"
            "  --help            show this help and exit\n"
            "  --version         show the program version and exit\n";
}

std::optional<std::string_view> semantic_description(const std::string &key) {
  static const std::map<std::string, std::string_view> descriptions{
      {"DEVC", "device container"},       {"DVID", "device identifier"},
      {"DVNM", "device name"},            {"STRM", "metadata stream container"},
      {"STNM", "stream name"},            {"SCAL", "value divisors"},
      {"SIUN", "SI component units"},     {"UNIT", "component units"},
      {"TSMP", "cumulative sample count"},{"STMP", "first-sample timestamp"},
      {"GPS5", "GPS position and speed"}, {"GPSU", "GPS UTC date/time"},
      {"GPSF", "GPS fix classification"}, {"GPSP", "GPS precision"},
      {"ACCL", "accelerometer samples"},  {"GYRO", "gyroscope samples"},
      {"MAGN", "magnetometer samples"},   {"GRAV", "gravity-vector samples"},
      {"ORIN", "input component orientation"},
      {"ORIO", "output component orientation"}};
  const auto iterator = descriptions.find(key);
  return iterator == descriptions.end() ? std::nullopt
                                         : std::optional<std::string_view>(iterator->second);
}

std::vector<std::string_view> component_labels(const gb::metadata::GpmfElement &element,
                                               const std::size_t components) {
  if (element.key == "GPS5" && components == 5U) {
    return {"latitude", "longitude", "altitude", "speed_2d", "speed_3d"};
  }
  return {};
}

std::vector<std::string> split_units(const std::string &unit) {
  std::vector<std::string> result;
  std::size_t begin = 0;
  while (begin <= unit.size()) {
    const auto end = unit.find(',', begin);
    result.push_back(
        unit.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1U;
  }
  return result;
}

bool contains_key(const gb::metadata::GpmfElement &element, const std::string &key) {
  if (element.key == key) {
    return true;
  }
  return std::any_of(element.children.begin(), element.children.end(),
                     [&key](const auto &child) { return contains_key(child, key); });
}

void print_element_values(const gb::metadata::GpmfElement &element, const unsigned indent) {
  const std::string pad(indent, ' ');
  const auto &display_values = element.normalized_values.empty() ? element.values
                                                                 : element.normalized_values;
  if (display_values.empty()) {
    if (element.type != 0U) {
      std::cout << pad << "values: not decoded (" << gb::metadata::type_name(element.type)
                << ")\n" << pad << "raw: " << hex_bytes(element.raw, 64U)
                << (element.raw.size() > 64U ? "..." : "") << '\n';
    }
    return;
  }
  const auto records = std::max<std::size_t>(element.repeat, 1U);
  const auto components = display_values.size() % records == 0U
                              ? display_values.size() / records
                              : display_values.size();
  const auto labels = component_labels(element, components);
  const auto units = split_units(element.unit);
  for (std::size_t record = 0; record < records && record * components < display_values.size();
       ++record) {
    std::cout << pad << (records == 1U ? "value" : "record[" + std::to_string(record) + "]")
              << ": ";
    for (std::size_t component = 0; component < components; ++component) {
      if (component != 0U) {
        std::cout << ", ";
      }
      if (!labels.empty()) {
        std::cout << labels[component] << '=';
      } else if (components > 1U) {
        std::cout << '[' << component << "]=";
      }
      const auto value_index = record * components + component;
      std::cout << display_text(display_values[value_index]);
      if (!units.empty() && !units[component % units.size()].empty()) {
        std::cout << ' ' << display_text(units[component % units.size()]);
      }
    }
    std::cout << '\n';
  }
  if (!element.normalized_values.empty()) {
    std::cout << pad << "encoding: sibling SCAL applied to "
              << gb::metadata::type_name(element.type) << " storage\n"
              << pad << "raw values: [";
    for (std::size_t index = 0; index < element.values.size(); ++index) {
      std::cout << (index == 0U ? "" : ", ") << display_text(element.values[index]);
    }
    std::cout << "]\n";
  }
  if (!gb::metadata::is_known_key(element.key)) {
    std::cout << pad << "raw bytes: " << hex_bytes(element.raw, 64U)
              << (element.raw.size() > 64U ? "..." : "") << '\n';
  }
}

void print_element_text(const gb::metadata::GpmfElement &element, const unsigned indent,
                        const std::optional<std::string> &key_filter) {
  if (key_filter.has_value() && !contains_key(element, *key_filter)) {
    return;
  }
  const std::string pad(indent, ' ');
  const auto description = semantic_description(element.key);
  std::cout << pad << element.key << " (" << gb::metadata::type_name(element.type)
            << ", structure=" << static_cast<unsigned>(element.structure_size)
            << ", repeat=" << element.repeat << ')';
  if (description.has_value()) {
    std::cout << " - " << *description;
  } else if (!gb::metadata::is_known_key(element.key)) {
    std::cout << " [UNKNOWN KEY: storage decoded, semantics unknown]";
  } else {
    std::cout << " [generic decoding; component semantics not assigned]";
  }
  std::cout << '\n';
  if (!key_filter.has_value() || element.key == *key_filter) {
    print_element_values(element, indent + 2U);
  }
  for (const auto &child : element.children) {
    print_element_text(child, indent + 2U, key_filter);
  }
}

std::size_t print_matching_values(const std::vector<gb::metadata::GpmfElement> &elements,
                                  const std::string &key, const unsigned indent) {
  std::size_t matches = 0;
  for (const auto &element : elements) {
    if (element.key == key) {
      print_element_text(element, indent, key);
      ++matches;
    }
    matches += print_matching_values(element.children, key, indent);
  }
  return matches;
}

std::size_t count_matching_values(const std::vector<gb::metadata::GpmfElement> &elements,
                                  const std::string &key) {
  std::size_t matches = 0;
  for (const auto &element : elements) {
    matches += element.key == key ? 1U : 0U;
    matches += count_matching_values(element.children, key);
  }
  return matches;
}

bool has_iso_bmff_signature(const std::filesystem::path &path) {
  std::array<char, 8> prefix{};
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return true;
  }
  input.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
  return input.gcount() == static_cast<std::streamsize>(prefix.size()) && prefix[4] == 'f' &&
         prefix[5] == 't' && prefix[6] == 'y' && prefix[7] == 'p';
}

void summarize(const std::vector<gb::metadata::GpmfElement> &elements,
               const std::size_t sample_index, std::map<std::string, FieldSummary> &fields) {
  for (const auto &element : elements) {
    auto [iterator, inserted] = fields.try_emplace(element.key);
    auto &field = iterator->second;
    if (inserted) {
      field.key = element.key;
      field.first_sample = sample_index;
      field.known = gb::metadata::is_known_key(element.key);
      field.raw_preview = hex_bytes(element.raw, 32U);
    }
    field.last_sample = sample_index;
    ++field.occurrences;
    field.record_count += element.type == 0U ? 0U : element.repeat;
    field.value_count += element.type == 0U ? 0U
                                            : (element.values.empty() ? element.repeat
                                                                      : element.values.size());
    field.types.insert(gb::metadata::type_name(element.type));
    field.structure_sizes.insert(element.structure_size);
    if (!element.unit.empty()) {
      field.units.insert(element.unit);
    }
    field.has_normalized_values = field.has_normalized_values || !element.normalized_values.empty();
    summarize(element.children, sample_index, fields);
  }
}

void print_string_array(const std::vector<std::string> &values, std::ostream &output) {
  output << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      output << ", ";
    }
    output << json_string(values[index]);
  }
  output << ']';
}

void print_element_json(const gb::metadata::GpmfElement &element, std::ostream &output,
                        const unsigned indent) {
  const std::string pad(indent, ' ');
  output << pad << "{\n"
         << pad << "  \"key\": " << json_string(element.key) << ",\n"
         << pad << "  \"known\": " << (gb::metadata::is_known_key(element.key) ? "true" : "false")
         << ",\n"
         << pad << "  \"type\": " << json_string(gb::metadata::type_name(element.type)) << ",\n"
         << pad << "  \"type_code\": " << static_cast<unsigned>(element.type) << ",\n"
         << pad << "  \"structure_size\": " << static_cast<unsigned>(element.structure_size)
         << ",\n"
         << pad << "  \"repeat\": " << element.repeat << ",\n"
         << pad << "  \"payload_offset\": " << element.offset << ",\n"
         << pad << "  \"unit\": " << json_string(element.unit) << ",\n"
         << pad << "  \"raw_hex\": " << json_string(hex_bytes(element.raw)) << ",\n"
         << pad << "  \"values\": ";
  print_string_array(element.values, output);
  output << ",\n" << pad << "  \"normalized_values\": ";
  print_string_array(element.normalized_values, output);
  output << ",\n" << pad << "  \"children\": [";
  if (!element.children.empty()) {
    output << '\n';
  }
  for (std::size_t index = 0; index < element.children.size(); ++index) {
    print_element_json(element.children[index], output, indent + 4U);
    output << (index + 1U == element.children.size() ? "\n" : ",\n");
  }
  if (!element.children.empty()) {
    output << pad << "  ";
  }
  output << "]\n" << pad << '}';
}

void dump_raw(const std::filesystem::path &directory, const std::uint32_t track_id,
              const std::size_t sample_index, const std::vector<std::uint8_t> &payload) {
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) {
    throw std::runtime_error("cannot create raw output directory: " + error.message());
  }
  const auto path = directory / ("track-" + std::to_string(track_id) + "-sample-" +
                                 std::to_string(sample_index) + ".gpmf");
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char *>(payload.data()),
               static_cast<std::streamsize>(payload.size()));
  if (!output) {
    throw std::runtime_error("cannot write raw payload: " + path.string());
  }
}

const gb::container::Track &select_track(const gb::container::Inspection &inspection,
                                         const std::optional<std::uint32_t> id) {
  const gb::container::Track *selected = nullptr;
  for (const auto &track : inspection.tracks) {
    if (track.codec != "gpmd" || (id.has_value() && track.id != *id)) {
      continue;
    }
    if (selected != nullptr && !id.has_value()) {
      throw std::runtime_error("multiple gpmd tracks found; select one with --track");
    }
    selected = &track;
  }
  if (selected == nullptr) {
    throw std::runtime_error(id.has_value() ? "selected track is not a gpmd track"
                                            : "no gpmd metadata track found");
  }
  return *selected;
}

void print_inventory_json(const Options &options, const gb::container::Track &track,
                          const std::map<std::string, FieldSummary> &fields) {
  std::cout << "{\n"
            << "  \"schema\": \"gb.metadata/v1\",\n"
            << "  \"path\": " << json_string(options.path.string()) << ",\n"
            << "  \"source\": \"gpmf\",\n"
            << "  \"track\": {\"id\": " << track.id << ", \"handler\": "
            << json_string(track.handler_type) << ", \"codec\": " << json_string(track.codec)
            << ", \"time_base\": {\"numerator\": " << track.time_base.numerator
            << ", \"denominator\": " << track.time_base.denominator << "}, \"duration_ticks\": "
            << track.duration_ticks << ", \"sample_count\": " << track.sample_count << "},\n"
            << "  \"fields\": [" << (fields.empty() ? "" : "\n");
  std::size_t index = 0;
  for (const auto &[key, field] : fields) {
    (void)key;
    std::vector<std::string> types(field.types.begin(), field.types.end());
    std::vector<std::string> units(field.units.begin(), field.units.end());
    std::cout << "    {\"key\": " << json_string(field.key)
              << ", \"known\": " << (field.known ? "true" : "false") << ", \"types\": ";
    print_string_array(types, std::cout);
    std::cout << ", \"structure_sizes\": [";
    std::size_t size_index = 0;
    for (const auto size : field.structure_sizes) {
      std::cout << (size_index++ == 0U ? "" : ", ") << static_cast<unsigned>(size);
    }
    std::cout << "], \"units\": ";
    print_string_array(units, std::cout);
    std::cout << ", \"occurrences\": " << field.occurrences
              << ", \"record_count\": " << field.record_count
              << ", \"value_count\": " << field.value_count
              << ", \"record_rate_hz_estimate\": ";
    if (field.record_count == 0U || track.duration_ticks == 0U ||
        track.time_base.numerator == 0U) {
      std::cout << "null";
    } else {
      const auto rate = static_cast<long double>(field.record_count) *
                        static_cast<long double>(track.time_base.denominator) /
                        (static_cast<long double>(track.duration_ticks) *
                         static_cast<long double>(track.time_base.numerator));
      std::cout << std::setprecision(10) << rate;
    }
    std::cout
              << ", \"first_sample\": " << field.first_sample
              << ", \"last_sample\": " << field.last_sample
              << ", \"has_normalized_values\": "
              << (field.has_normalized_values ? "true" : "false")
              << ", \"raw_hex_preview\": " << json_string(field.raw_preview) << "}"
              << (++index == fields.size() ? "\n" : ",\n");
  }
  std::cout << (fields.empty() ? "" : "  ") << "]\n}\n";
}

void print_inventory_text(const Options &options, const gb::container::Track &track,
                          const std::map<std::string, FieldSummary> &fields) {
  std::cout << "file: " << options.path.string() << "\nsource: GPMF\ntrack: " << track.id
            << " time_base=" << track.time_base.numerator << '/' << track.time_base.denominator
            << " duration_ticks=" << track.duration_ticks << " samples=" << track.sample_count
            << "\nfields:\n";
  for (const auto &[key, field] : fields) {
    if (options.key.has_value() && key != *options.key) {
      continue;
    }
    std::cout << "  " << key << " occurrences=" << field.occurrences
              << " records=" << field.record_count << " values=" << field.value_count
              << " samples=" << field.first_sample << ".."
              << field.last_sample << " type=";
    for (const auto &type : field.types) {
      std::cout << type << ' ';
    }
    if (!field.units.empty()) {
      std::cout << "units=";
      for (const auto &unit : field.units) {
        std::cout << unit << ' ';
      }
    }
    if (field.record_count != 0U && track.duration_ticks != 0U &&
        track.time_base.numerator != 0U) {
      const auto rate = static_cast<long double>(field.record_count) *
                        static_cast<long double>(track.time_base.denominator) /
                        (static_cast<long double>(track.duration_ticks) *
                         static_cast<long double>(track.time_base.numerator));
      std::cout << "rate_estimate=" << std::setprecision(6) << rate << "Hz ";
    }
    std::cout << (field.known ? "" : "unknown=true") << '\n';
  }
}

void print_sample_json(const Options &options, const gb::container::Track &track,
                       const std::size_t index, const gb::container::SampleReference &sample,
                       const std::vector<gb::metadata::GpmfElement> &elements) {
  std::cout << "{\n"
            << "  \"schema\": \"gb.metadata.sample/v1\",\n"
            << "  \"path\": " << json_string(options.path.string()) << ",\n"
            << "  \"source\": \"gpmf\",\n"
            << "  \"track_id\": " << track.id << ",\n"
            << "  \"sample_index\": " << index << ",\n"
            << "  \"offset\": " << sample.offset << ",\n"
            << "  \"size\": " << sample.size << ",\n"
            << "  \"decode_time_ticks\": " << sample.decode_time << ",\n"
            << "  \"duration_ticks\": " << sample.duration << ",\n"
            << "  \"time_base\": {\"numerator\": " << track.time_base.numerator
            << ", \"denominator\": " << track.time_base.denominator << "},\n"
            << "  \"elements\": [" << (elements.empty() ? "" : "\n");
  for (std::size_t element_index = 0; element_index < elements.size(); ++element_index) {
    print_element_json(elements[element_index], std::cout, 4U);
    std::cout << (element_index + 1U == elements.size() ? "\n" : ",\n");
  }
  std::cout << (elements.empty() ? "" : "  ") << "]\n}\n";
}

void print_sample_text(const Options &options, const gb::container::Track &track,
                       const std::size_t index, const gb::container::SampleReference &sample,
                       const std::vector<gb::metadata::GpmfElement> &elements) {
  std::cout << "file: " << options.path.string() << "\nsource: GPMF\ntrack: " << track.id
            << "\nsample: " << index << " offset=" << sample.offset << " size=" << sample.size
            << " decode_time_ticks=" << sample.decode_time << " duration_ticks=" << sample.duration
            << " time_base=" << track.time_base.numerator << '/' << track.time_base.denominator
            << "\nmetadata:\n";
  for (const auto &element : elements) {
    print_element_text(element, 2U, options.key);
  }
}

} // namespace

int main(const int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      print_help(std::cout);
      return 0;
    }
    if (argument == "--version") {
      std::cout << "gb-metadata " << kVersion << '\n';
      return 0;
    }
    if (argument == "--json") {
      options.json = true;
      continue;
    }
    if (argument == "--values") {
      options.values = true;
      continue;
    }
    if (argument == "--track" || argument == "--sample" || argument == "--dump-raw" ||
        argument == "--key") {
      if (++index >= argc) {
        std::cerr << "gb-metadata: missing value for " << argument << '\n';
        return 2;
      }
      if (argument == "--dump-raw") {
        options.raw_directory = std::filesystem::path(argv[index]);
        continue;
      }
      if (argument == "--key") {
        const std::string key(argv[index]);
        if (key.size() != 4U) {
          std::cerr << "gb-metadata: --key requires an exact four-byte FourCC\n";
          return 2;
        }
        options.key = key;
        continue;
      }
      const auto value = parse_unsigned(argv[index]);
      if (!value.has_value() || (argument == "--track" && *value > UINT32_MAX)) {
        std::cerr << "gb-metadata: invalid numeric value for " << argument << '\n';
        return 2;
      }
      if (argument == "--track") {
        options.track_id = static_cast<std::uint32_t>(*value);
      } else {
        options.sample_index = static_cast<std::size_t>(*value);
      }
      continue;
    }
    if (argument.starts_with('-')) {
      std::cerr << "gb-metadata: unknown option: " << argument << '\n';
      return 2;
    }
    if (!options.path.empty()) {
      std::cerr << "gb-metadata: expected exactly one input file\n";
      return 2;
    }
    options.path = argument;
  }
  if (options.path.empty()) {
    std::cerr << "gb-metadata: missing input file\n";
    return 2;
  }
  if (options.values && !options.key.has_value()) {
    std::cerr << "gb-metadata: --values requires --key to avoid unbounded accidental output\n";
    return 2;
  }
  if (options.values && options.sample_index.has_value()) {
    std::cerr << "gb-metadata: use --sample with --key directly; do not combine it with --values\n";
    return 2;
  }
  if (options.json && (options.values || options.key.has_value())) {
    std::cerr << "gb-metadata: --key and --values are friendly-text options; use JSON filtering "
                 "tools for --json\n";
    return 2;
  }

  try {
    if (!has_iso_bmff_signature(options.path)) {
      std::cerr << "gb-metadata: unsupported: input is not an ISO BMFF file\n";
      return 5;
    }
    const auto inspection = gb::container::inspect_iso_bmff(options.path);
    const auto &track = select_track(inspection, options.track_id);
    if (options.sample_index.has_value() && *options.sample_index >= track.samples.size()) {
      std::cerr << "gb-metadata: sample index is outside the track\n";
      return 2;
    }
    std::map<std::string, FieldSummary> fields;
    const auto first = options.sample_index.value_or(0U);
    const auto end = options.sample_index.has_value() ? first + 1U : track.samples.size();
    std::vector<gb::metadata::GpmfElement> selected_elements;
    std::size_t value_matches = 0;
    if (options.values) {
      std::cout << "file: " << options.path.string() << "\nsource: GPMF\ntrack: " << track.id
                << "\nkey: " << *options.key << "\nvalues:\n";
    }
    for (std::size_t index = first; index < end; ++index) {
      const auto payload = gb::container::read_sample(options.path, track.samples[index]);
      const auto elements = gb::metadata::parse_gpmf(payload);
      summarize(elements, index, fields);
      if (options.values) {
        const auto matches = count_matching_values(elements, *options.key);
        if (matches != 0U) {
          std::cout << "  sample[" << index << "] decode_time_ticks="
                    << track.samples[index].decode_time << " duration_ticks="
                    << track.samples[index].duration << " matches=" << matches << '\n';
          (void)print_matching_values(elements, *options.key, 4U);
          value_matches += matches;
        }
      }
      if (options.raw_directory.has_value()) {
        dump_raw(*options.raw_directory, track.id, index, payload);
      }
      if (options.sample_index.has_value()) {
        selected_elements = elements;
      }
    }
    if (options.values) {
      if (value_matches == 0U) {
        std::cout << "  no matching fields\n";
      }
    } else if (options.sample_index.has_value()) {
      options.json
          ? print_sample_json(options, track, first, track.samples[first], selected_elements)
          : print_sample_text(options, track, first, track.samples[first], selected_elements);
    } else {
      options.json ? print_inventory_json(options, track, fields)
                   : print_inventory_text(options, track, fields);
    }
    return 0;
  } catch (const gb::metadata::GpmfError &error) {
    std::cerr << "gb-metadata: corrupt GPMF at payload byte " << error.offset() << ": "
              << error.what() << '\n';
    return 4;
  } catch (const gb::container::InspectionError &error) {
    std::cerr << "gb-metadata: " << gb::container::error_kind_name(error.kind()) << " at byte "
              << error.offset() << ": " << error.what() << '\n';
    return error.kind() == gb::container::ErrorKind::corrupt ? 4 : 3;
  } catch (const std::exception &error) {
    std::cerr << "gb-metadata: unsupported: " << error.what() << '\n';
    return 5;
  }
}
