#include "gb/container/iso_bmff.hpp"

#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kVersion = "0.1.0";

std::string json_string(const std::string_view input) {
  std::ostringstream output;
  output << '"';
  constexpr char hex[] = "0123456789abcdef";
  for (const char raw_byte : input) {
    const auto byte = static_cast<unsigned char>(raw_byte);
    switch (byte) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
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

std::string display_fourcc(const std::string_view type) {
  std::ostringstream output;
  for (const char raw_byte : type) {
    const auto byte = static_cast<unsigned char>(raw_byte);
    if (byte >= 0x20U && byte < 0x7fU) {
      output << static_cast<char>(byte);
    } else {
      constexpr char hex[] = "0123456789abcdef";
      output << "\\x" << hex[(byte >> 4U) & 0x0fU] << hex[byte & 0x0fU];
    }
  }
  return output.str();
}

void print_box_json(const gb::container::Box &box, std::ostream &output, const unsigned indent) {
  const std::string pad(indent, ' ');
  output << pad << "{\n"
         << pad << "  \"type\": " << json_string(box.type) << ",\n"
         << pad << "  \"offset\": " << box.offset << ",\n"
         << pad << "  \"size\": " << box.size << ",\n"
         << pad << "  \"header_size\": " << box.header_size << ",\n"
         << pad << "  \"children\": [";
  if (!box.children.empty()) {
    output << '\n';
    for (std::size_t index = 0; index < box.children.size(); ++index) {
      print_box_json(box.children[index], output, indent + 4U);
      output << (index + 1U == box.children.size() ? "\n" : ",\n");
    }
    output << pad << "  ";
  }
  output << "]\n" << pad << '}';
}

void print_json(const gb::container::Inspection &inspection) {
  std::cout << "{\n"
            << "  \"schema\": \"gb.inspect/v1\",\n"
            << "  \"path\": " << json_string(inspection.path.string()) << ",\n"
            << "  \"file_size\": " << inspection.file_size << ",\n"
            << "  \"identity\": {\n"
            << "    \"format\": \"ISO BMFF\",\n"
            << "    \"major_brand\": "
            << json_string(inspection.identity.major_brand.value_or("")) << ",\n"
            << "    \"minor_version\": " << inspection.identity.minor_version.value_or(0U)
            << ",\n"
            << "    \"compatible_brands\": [";
  for (std::size_t index = 0; index < inspection.identity.compatible_brands.size(); ++index) {
    if (index != 0U) {
      std::cout << ", ";
    }
    std::cout << json_string(inspection.identity.compatible_brands[index]);
  }
  std::cout << "]\n  },\n  \"boxes\": [";
  if (!inspection.boxes.empty()) {
    std::cout << '\n';
    for (std::size_t index = 0; index < inspection.boxes.size(); ++index) {
      print_box_json(inspection.boxes[index], std::cout, 4U);
      std::cout << (index + 1U == inspection.boxes.size() ? "\n" : ",\n");
    }
    std::cout << "  ";
  }
  std::cout << "]\n}\n";
}

void print_box_text(const gb::container::Box &box, const unsigned depth) {
  std::cout << std::string(depth * 2U, ' ') << display_fourcc(box.type)
            << " offset=" << box.offset << " size=" << box.size
            << " header=" << box.header_size << '\n';
  for (const auto &child : box.children) {
    print_box_text(child, depth + 1U);
  }
}

void print_text(const gb::container::Inspection &inspection) {
  std::cout << "file: " << inspection.path.string() << '\n'
            << "size: " << inspection.file_size << " bytes\n"
            << "format: ISO BMFF\n"
            << "major brand: " << display_fourcc(inspection.identity.major_brand.value_or(""))
            << '\n'
            << "compatible brands:";
  for (const auto &brand : inspection.identity.compatible_brands) {
    std::cout << ' ' << display_fourcc(brand);
  }
  std::cout << "\nboxes:\n";
  for (const auto &box : inspection.boxes) {
    print_box_text(box, 0U);
  }
}

void print_help(std::ostream &output) {
  output << "Usage: gb-inspect [--json] <file>\n"
            "       gb-inspect --help\n"
            "       gb-inspect --version\n\n"
            "Inspect a file's validated ISO BMFF identity and top-level box hierarchy.\n\n"
            "Options:\n"
            "  --json     emit schema-versioned JSON to stdout\n"
            "  --help     show this help and exit\n"
            "  --version  show the program version and exit\n";
}

int error_exit_code(const gb::container::ErrorKind kind) {
  switch (kind) {
  case gb::container::ErrorKind::missing:
  case gb::container::ErrorKind::io:
    return 3;
  case gb::container::ErrorKind::corrupt:
    return 4;
  case gb::container::ErrorKind::unsupported:
    return 5;
  }
  return 1;
}

} // namespace

int main(const int argc, char **argv) {
  bool json = false;
  std::optional<std::filesystem::path> path;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      print_help(std::cout);
      return 0;
    }
    if (argument == "--version") {
      std::cout << "gb-inspect " << kVersion << '\n';
      return 0;
    }
    if (argument == "--json") {
      json = true;
      continue;
    }
    if (argument.starts_with('-')) {
      std::cerr << "gb-inspect: unknown option: " << argument << '\n';
      print_help(std::cerr);
      return 2;
    }
    if (path.has_value()) {
      std::cerr << "gb-inspect: expected exactly one input file\n";
      return 2;
    }
    path = std::filesystem::path(argument);
  }
  if (!path.has_value()) {
    std::cerr << "gb-inspect: missing input file\n";
    print_help(std::cerr);
    return 2;
  }

  try {
    const auto inspection = gb::container::inspect_iso_bmff(*path);
    json ? print_json(inspection) : print_text(inspection);
    return 0;
  } catch (const gb::container::InspectionError &error) {
    std::cerr << "gb-inspect: " << gb::container::error_kind_name(error.kind())
              << " at byte " << error.offset() << ": " << error.what() << '\n';
    return error_exit_code(error.kind());
  } catch (const std::exception &error) {
    std::cerr << "gb-inspect: unexpected: " << error.what() << '\n';
    return 1;
  }
}
