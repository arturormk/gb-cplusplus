#include "gb/container/iso_bmff.hpp"
#include "gb/image/jpeg.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kVersion = GB_INSPECT_VERSION;

enum class InputFormat { iso_bmff, jpeg };

InputFormat detect_input_format(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::status(path, error);
  if (error || !std::filesystem::exists(status)) {
    throw gb::container::InspectionError(gb::container::ErrorKind::missing, 0,
                                         "input file does not exist");
  }
  if (!std::filesystem::is_regular_file(status)) {
    throw gb::container::InspectionError(gb::container::ErrorKind::io, 0,
                                         "input is not a regular file");
  }

  std::array<unsigned char, 8> prefix{};
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw gb::container::InspectionError(gb::container::ErrorKind::io, 0,
                                         "cannot open input for reading");
  }
  input.read(reinterpret_cast<char *>(prefix.data()), static_cast<std::streamsize>(prefix.size()));
  const auto count = input.gcount();
  if (count >= 2 && prefix[0] == 0xffU && prefix[1] == 0xd8U) {
    return InputFormat::jpeg;
  }
  if (count >= 8 && prefix[4] == 'f' && prefix[5] == 't' && prefix[6] == 'y' && prefix[7] == 'p') {
    return InputFormat::iso_bmff;
  }
  throw gb::container::InspectionError(gb::container::ErrorKind::unsupported, 0,
                                       "unrecognized file signature");
}

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
            << "    \"major_brand\": " << json_string(inspection.identity.major_brand.value_or(""))
            << ",\n"
            << "    \"minor_version\": " << inspection.identity.minor_version.value_or(0U) << ",\n"
            << "    \"compatible_brands\": [";
  for (std::size_t index = 0; index < inspection.identity.compatible_brands.size(); ++index) {
    if (index != 0U) {
      std::cout << ", ";
    }
    std::cout << json_string(inspection.identity.compatible_brands[index]);
  }
  std::cout << "]\n  },\n  \"tracks\": [";
  if (!inspection.tracks.empty()) {
    std::cout << '\n';
  }
  for (std::size_t index = 0; index < inspection.tracks.size(); ++index) {
    const auto &track = inspection.tracks[index];
    std::cout << "    {\n"
              << "      \"id\": " << track.id << ",\n"
              << "      \"handler_type\": " << json_string(track.handler_type) << ",\n"
              << "      \"handler_name\": " << json_string(track.handler_name) << ",\n"
              << "      \"codec\": " << json_string(track.codec) << ",\n"
              << "      \"time_base\": {\"numerator\": " << track.time_base.numerator
              << ", \"denominator\": " << track.time_base.denominator << "},\n"
              << "      \"duration_ticks\": " << track.duration_ticks << ",\n"
              << "      \"sample_count\": " << track.sample_count << ",\n"
              << "      \"width\": " << track.width << ",\n"
              << "      \"height\": " << track.height << ",\n"
              << "      \"channel_count\": " << track.channel_count << ",\n"
              << "      \"bits_per_sample\": " << track.bits_per_sample << ",\n"
              << "      \"sample_rate\": " << track.sample_rate << "\n"
              << "    }" << (index + 1U == inspection.tracks.size() ? "\n" : ",\n");
  }
  if (!inspection.tracks.empty()) {
    std::cout << "  ";
  }
  std::cout << "],\n  \"boxes\": [";
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
  std::cout << std::string(depth * 2U, ' ') << display_fourcc(box.type) << " offset=" << box.offset
            << " size=" << box.size << " header=" << box.header_size << '\n';
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
  std::cout << "tracks:\n";
  for (const auto &track : inspection.tracks) {
    std::cout << "  id=" << track.id << " handler=" << display_fourcc(track.handler_type)
              << " codec=" << display_fourcc(track.codec)
              << " time_base=" << track.time_base.numerator << '/' << track.time_base.denominator
              << " duration_ticks=" << track.duration_ticks << " samples=" << track.sample_count;
    if (track.width != 0U || track.height != 0U) {
      std::cout << " geometry=" << track.width << 'x' << track.height;
    }
    if (track.channel_count != 0U) {
      std::cout << " audio=" << track.sample_rate << "Hz/" << track.channel_count << "ch/"
                << track.bits_per_sample << "bit";
    }
    if (!track.handler_name.empty()) {
      std::cout << " name=" << track.handler_name;
    }
    std::cout << '\n';
  }
}

void print_jpeg_segment_json(const gb::image::JpegSegment &segment, std::ostream &output,
                             const unsigned indent) {
  const std::string pad(indent, ' ');
  output << pad << "{\n"
         << pad << "  \"marker\": " << json_string(segment.name) << ",\n"
         << pad << "  \"code\": " << static_cast<unsigned>(segment.marker) << ",\n"
         << pad << "  \"content\": " << json_string(segment.content) << ",\n"
         << pad << "  \"offset\": " << segment.offset << ",\n"
         << pad << "  \"size\": " << segment.size << "\n"
         << pad << '}';
}

void print_jpeg_json(const gb::image::JpegInspection &inspection) {
  std::cout << "{\n"
            << "  \"schema\": \"gb.inspect.jpeg/v1\",\n"
            << "  \"path\": " << json_string(inspection.path.string()) << ",\n"
            << "  \"file_size\": " << inspection.file_size << ",\n"
            << "  \"identity\": {\n"
            << "    \"format\": \"JPEG\",\n"
            << "    \"image_count\": " << inspection.images.size() << ",\n"
            << "    \"has_jfif\": " << (inspection.has_jfif ? "true" : "false") << ",\n"
            << "    \"has_exif\": " << (inspection.has_exif ? "true" : "false") << ",\n"
            << "    \"has_xmp\": " << (inspection.has_xmp ? "true" : "false") << ",\n"
            << "    \"has_mpf\": " << (inspection.has_mpf ? "true" : "false") << ",\n"
            << "    \"has_app6\": " << (inspection.has_app6 ? "true" : "false") << "\n"
            << "  },\n"
            << "  \"trailing_bytes\": " << inspection.trailing_bytes << ",\n"
            << "  \"images\": [";
  if (!inspection.images.empty()) {
    std::cout << '\n';
  }
  for (std::size_t image_index = 0; image_index < inspection.images.size(); ++image_index) {
    const auto &image = inspection.images[image_index];
    std::cout << "    {\n"
              << "      \"offset\": " << image.offset << ",\n"
              << "      \"size\": " << image.size << ",\n"
              << "      \"width\": " << image.width << ",\n"
              << "      \"height\": " << image.height << ",\n"
              << "      \"precision\": " << static_cast<unsigned>(image.precision) << ",\n"
              << "      \"components\": " << static_cast<unsigned>(image.components) << ",\n"
              << "      \"complete\": " << (image.complete ? "true" : "false") << ",\n"
              << "      \"segments\": [";
    if (!image.segments.empty()) {
      std::cout << '\n';
    }
    for (std::size_t segment_index = 0; segment_index < image.segments.size(); ++segment_index) {
      print_jpeg_segment_json(image.segments[segment_index], std::cout, 8U);
      std::cout << (segment_index + 1U == image.segments.size() ? "\n" : ",\n");
    }
    if (!image.segments.empty()) {
      std::cout << "      ";
    }
    std::cout << "]\n    }" << (image_index + 1U == inspection.images.size() ? "\n" : ",\n");
  }
  if (!inspection.images.empty()) {
    std::cout << "  ";
  }
  std::cout << "]\n}\n";
}

void print_jpeg_text(const gb::image::JpegInspection &inspection) {
  std::cout << "file: " << inspection.path.string() << '\n'
            << "size: " << inspection.file_size << " bytes\n"
            << "format: JPEG\n"
            << "images: " << inspection.images.size() << '\n'
            << "metadata:" << (inspection.has_jfif ? " JFIF" : "")
            << (inspection.has_exif ? " Exif" : "") << (inspection.has_xmp ? " XMP" : "")
            << (inspection.has_mpf ? " MPF" : "") << (inspection.has_app6 ? " APP6" : "") << '\n';
  for (std::size_t index = 0; index < inspection.images.size(); ++index) {
    const auto &image = inspection.images[index];
    std::cout << "image[" << index << "]: offset=" << image.offset << " size=" << image.size
              << " width=" << image.width << " height=" << image.height
              << " precision=" << static_cast<unsigned>(image.precision)
              << " components=" << static_cast<unsigned>(image.components) << '\n';
    for (const auto &segment : image.segments) {
      std::cout << "  " << segment.name << " offset=" << segment.offset << " size=" << segment.size;
      if (!segment.content.empty()) {
        std::cout << " content=" << segment.content;
      }
      std::cout << '\n';
    }
  }
  if (inspection.trailing_bytes != 0U) {
    std::cout << "trailing bytes: " << inspection.trailing_bytes << '\n';
  }
}

void print_help(std::ostream &output) {
  output << "Usage: gb-inspect [--json] <file>\n"
            "       gb-inspect --help\n"
            "       gb-inspect --version\n\n"
            "Inspect validated ISO BMFF/MP4 box structure or JPEG marker structure.\n\n"
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
    switch (detect_input_format(*path)) {
    case InputFormat::iso_bmff: {
      const auto inspection = gb::container::inspect_iso_bmff(*path);
      json ? print_json(inspection) : print_text(inspection);
      break;
    }
    case InputFormat::jpeg: {
      const auto inspection = gb::image::inspect_jpeg(*path);
      json ? print_jpeg_json(inspection) : print_jpeg_text(inspection);
      break;
    }
    }
    return 0;
  } catch (const gb::image::JpegError &error) {
    const auto is_io = error.kind() == gb::image::JpegErrorKind::io;
    std::cerr << "gb-inspect: " << (is_io ? "io" : "corrupt") << " at byte " << error.offset()
              << ": " << error.what() << '\n';
    return is_io ? 3 : 4;
  } catch (const gb::container::InspectionError &error) {
    std::cerr << "gb-inspect: " << gb::container::error_kind_name(error.kind()) << " at byte "
              << error.offset() << ": " << error.what() << '\n';
    return error_exit_code(error.kind());
  } catch (const std::exception &error) {
    std::cerr << "gb-inspect: unexpected: " << error.what() << '\n';
    return 1;
  }
}
