#include "gb/image/jpeg.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>

namespace gb::image {
namespace {

constexpr std::size_t kPrefixLimit = 64;
constexpr std::size_t kMaxImages = 16;
constexpr std::size_t kMaxSegmentsPerImage = 4096;

std::uint8_t read_u8(std::ifstream &input, const std::uint64_t offset) {
  unsigned char value{};
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  input.read(reinterpret_cast<char *>(&value), 1);
  if (!input) {
    throw JpegError(offset, "truncated byte");
  }
  return value;
}

std::uint16_t read_u16(std::ifstream &input, const std::uint64_t offset) {
  const auto high = static_cast<std::uint16_t>(read_u8(input, offset));
  const auto low = static_cast<std::uint16_t>(read_u8(input, offset + 1U));
  return static_cast<std::uint16_t>((high << 8U) | low);
}

std::vector<std::uint8_t> read_prefix(std::ifstream &input, const std::uint64_t offset,
                                      const std::uint64_t available) {
  const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(available, kPrefixLimit));
  std::vector<std::uint8_t> bytes(count);
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    throw JpegError(offset, "truncated segment payload");
  }
  return bytes;
}

bool starts_with(const std::vector<std::uint8_t> &bytes, const std::string_view signature) {
  if (bytes.size() < signature.size()) {
    return false;
  }
  return std::equal(signature.begin(), signature.end(), bytes.begin(),
                    [](const char left, const std::uint8_t right) {
                      return static_cast<std::uint8_t>(left) == right;
                    });
}

bool is_standalone_marker(const std::uint8_t marker) {
  return marker == 0x01U || (marker >= 0xd0U && marker <= 0xd9U);
}

bool is_start_of_frame(const std::uint8_t marker) {
  return (marker >= 0xc0U && marker <= 0xc3U) || (marker >= 0xc5U && marker <= 0xc7U) ||
         (marker >= 0xc9U && marker <= 0xcbU) || (marker >= 0xcdU && marker <= 0xcfU);
}

std::string classify_content(const std::uint8_t marker,
                             const std::vector<std::uint8_t> &prefix) {
  if (marker == 0xe0U && starts_with(prefix, std::string_view("JFIF\0", 5))) {
    return "JFIF";
  }
  if (marker == 0xe1U && starts_with(prefix, std::string_view("Exif\0\0", 6))) {
    return "Exif";
  }
  if (marker == 0xe1U &&
      starts_with(prefix, std::string_view("http://ns.adobe.com/xap/1.0/\0", 29))) {
    return "XMP";
  }
  if (marker == 0xe2U && starts_with(prefix, std::string_view("MPF\0", 4))) {
    return "MPF";
  }
  if (marker == 0xe6U) {
    return "application-specific";
  }
  return {};
}

struct PendingMarker {
  std::uint64_t offset{};
  std::uint8_t marker{};
};

PendingMarker scan_entropy_data(std::ifstream &input, std::uint64_t &offset,
                                const std::uint64_t file_size) {
  input.clear();
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!input) {
    throw JpegError(offset, "cannot seek to entropy-coded data");
  }
  const auto next_byte = [&input, &offset]() {
    const auto value = input.get();
    if (value == std::char_traits<char>::eof()) {
      throw JpegError(offset, "truncated entropy-coded data");
    }
    ++offset;
    return static_cast<std::uint8_t>(static_cast<unsigned char>(value));
  };

  while (offset < file_size) {
    if (next_byte() != 0xffU) {
      continue;
    }
    const auto marker_offset = offset - 1U;
    std::uint8_t marker{};
    do {
      if (offset >= file_size) {
        throw JpegError(marker_offset, "truncated marker in entropy-coded data");
      }
      marker = next_byte();
    } while (marker == 0xffU);

    if (marker == 0x00U || (marker >= 0xd0U && marker <= 0xd7U)) {
      continue;
    }
    return {marker_offset, marker};
  }
  throw JpegError(offset, "missing JPEG end-of-image marker");
}

JpegCodestream parse_codestream(std::ifstream &input, const std::uint64_t start,
                                const std::uint64_t file_size, JpegInspection &inspection) {
  if (file_size - start < 2U || read_u8(input, start) != 0xffU ||
      read_u8(input, start + 1U) != 0xd8U) {
    throw JpegError(start, "missing JPEG start-of-image marker");
  }

  JpegCodestream image{};
  image.offset = start;
  image.segments.push_back({0xd8U, jpeg_marker_name(0xd8U), {}, start, 2U});
  auto offset = start + 2U;
  bool in_entropy_data = false;

  while (offset < file_size) {
    if (image.segments.size() >= kMaxSegmentsPerImage) {
      throw JpegError(offset, "JPEG segment count exceeds the limit of 4096");
    }

    std::uint64_t marker_offset{};
    std::uint8_t marker{};
    if (in_entropy_data) {
      const auto pending = scan_entropy_data(input, offset, file_size);
      marker_offset = pending.offset;
      marker = pending.marker;
      in_entropy_data = false;
    } else {
      marker_offset = offset;
      if (read_u8(input, offset) != 0xffU) {
        throw JpegError(offset, "expected JPEG marker prefix");
      }
      do {
        ++offset;
        if (offset >= file_size) {
          throw JpegError(marker_offset, "truncated JPEG marker");
        }
      } while (read_u8(input, offset) == 0xffU);
      marker = read_u8(input, offset);
      ++offset;
      if (marker == 0x00U) {
        throw JpegError(marker_offset, "stuffed byte outside entropy-coded data");
      }
    }

    if (marker == 0xd8U) {
      throw JpegError(marker_offset, "unexpected nested start-of-image marker");
    }
    if (marker == 0xd9U) {
      image.segments.push_back({marker, jpeg_marker_name(marker), {}, marker_offset, 2U});
      image.size = offset - start;
      image.complete = true;
      return image;
    }
    if (is_standalone_marker(marker)) {
      image.segments.push_back({marker, jpeg_marker_name(marker), {}, marker_offset, 2U});
      continue;
    }

    if (file_size - offset < 2U) {
      throw JpegError(marker_offset, "truncated JPEG segment length");
    }
    const auto encoded_length = static_cast<std::uint64_t>(read_u16(input, offset));
    if (encoded_length < 2U) {
      throw JpegError(marker_offset, "JPEG segment length is smaller than its header");
    }
    const auto segment_size = encoded_length + 2U;
    if (segment_size > file_size - marker_offset) {
      throw JpegError(marker_offset, "JPEG segment extends beyond the file");
    }
    const auto payload_offset = offset + 2U;
    const auto payload_size = encoded_length - 2U;
    const auto prefix = read_prefix(input, payload_offset, payload_size);
    const auto content = classify_content(marker, prefix);
    image.segments.push_back(
        {marker, jpeg_marker_name(marker), content, marker_offset, segment_size});

    inspection.has_jfif = inspection.has_jfif || content == "JFIF";
    inspection.has_exif = inspection.has_exif || content == "Exif";
    inspection.has_xmp = inspection.has_xmp || content == "XMP";
    inspection.has_mpf = inspection.has_mpf || content == "MPF";
    inspection.has_app6 = inspection.has_app6 || marker == 0xe6U;

    if (is_start_of_frame(marker)) {
      if (payload_size < 6U) {
        throw JpegError(marker_offset, "start-of-frame payload is too short");
      }
      image.precision = read_u8(input, payload_offset);
      image.height = read_u16(input, payload_offset + 1U);
      image.width = read_u16(input, payload_offset + 3U);
      image.components = read_u8(input, payload_offset + 5U);
      const auto required = 6U + static_cast<std::uint64_t>(image.components) * 3U;
      if (payload_size < required) {
        throw JpegError(marker_offset, "start-of-frame component table is truncated");
      }
      if (image.width == 0U || image.height == 0U || image.components == 0U) {
        throw JpegError(marker_offset, "start-of-frame contains zero geometry or components");
      }
    }

    offset = marker_offset + segment_size;
    if (marker == 0xdaU) {
      in_entropy_data = true;
    }
  }
  throw JpegError(offset, "missing JPEG end-of-image marker");
}

} // namespace

JpegError::JpegError(const std::uint64_t offset, const std::string &message)
    : JpegError(JpegErrorKind::corrupt, offset, message) {}

JpegError::JpegError(const JpegErrorKind kind, const std::uint64_t offset,
                     const std::string &message)
    : std::runtime_error(message), kind_(kind), offset_(offset) {}

JpegErrorKind JpegError::kind() const noexcept { return kind_; }
std::uint64_t JpegError::offset() const noexcept { return offset_; }

JpegInspection inspect_jpeg(const std::filesystem::path &path) {
  std::error_code error;
  const auto file_size = std::filesystem::file_size(path, error);
  if (error) {
    throw JpegError(JpegErrorKind::io, 0, "cannot determine input file size");
  }
  if (file_size > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    throw JpegError(JpegErrorKind::io, 0, "input is too large for this platform's file API");
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw JpegError(JpegErrorKind::io, 0, "cannot open input for reading");
  }

  JpegInspection inspection{path, file_size, false, false, false, false, false, 0, {}};
  auto offset = std::uint64_t{0};
  while (offset < file_size && inspection.images.size() < kMaxImages) {
    const auto image = parse_codestream(input, offset, file_size, inspection);
    offset += image.size;
    inspection.images.push_back(image);
    if (file_size - offset < 2U || read_u8(input, offset) != 0xffU ||
        read_u8(input, offset + 1U) != 0xd8U) {
      break;
    }
  }
  if (inspection.images.size() == kMaxImages && offset < file_size) {
    throw JpegError(offset, "JPEG codestream count exceeds the limit of 16");
  }
  inspection.trailing_bytes = file_size - offset;
  return inspection;
}

std::string jpeg_marker_name(const std::uint8_t marker) {
  if (marker >= 0xe0U && marker <= 0xefU) {
    return "APP" + std::to_string(marker - 0xe0U);
  }
  if (marker >= 0xd0U && marker <= 0xd7U) {
    return "RST" + std::to_string(marker - 0xd0U);
  }
  switch (marker) {
  case 0x01U:
    return "TEM";
  case 0xc0U:
    return "SOF0";
  case 0xc1U:
    return "SOF1";
  case 0xc2U:
    return "SOF2";
  case 0xc4U:
    return "DHT";
  case 0xc8U:
    return "JPG";
  case 0xccU:
    return "DAC";
  case 0xd8U:
    return "SOI";
  case 0xd9U:
    return "EOI";
  case 0xdaU:
    return "SOS";
  case 0xdbU:
    return "DQT";
  case 0xdcU:
    return "DNL";
  case 0xddU:
    return "DRI";
  case 0xdeU:
    return "DHP";
  case 0xdfU:
    return "EXP";
  case 0xfeU:
    return "COM";
  default:
    if (is_start_of_frame(marker)) {
      return "SOF";
    }
    return "0x" + [](const std::uint8_t value) {
      constexpr char hex[] = "0123456789abcdef";
      std::string result(2, '0');
      result[0] = hex[(value >> 4U) & 0x0fU];
      result[1] = hex[value & 0x0fU];
      return result;
    }(marker);
  }
}

} // namespace gb::image
