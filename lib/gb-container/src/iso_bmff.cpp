#include "gb/container/iso_bmff.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>

namespace gb::container {
namespace {

constexpr std::size_t kMaxDepth = 32;
constexpr std::uint64_t kBasicHeaderSize = 8;
constexpr std::uint64_t kExtendedHeaderSize = 16;
constexpr std::uint64_t kUuidExtraSize = 16;

std::uint32_t read_u32(std::ifstream &input, const std::uint64_t offset) {
  std::array<unsigned char, 4> bytes{};
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    throw InspectionError(ErrorKind::corrupt, offset, "truncated 32-bit field");
  }
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
}

std::uint64_t read_u64(std::ifstream &input, const std::uint64_t offset) {
  const auto high = static_cast<std::uint64_t>(read_u32(input, offset));
  const auto low = static_cast<std::uint64_t>(read_u32(input, offset + 4U));
  return (high << 32U) | low;
}

std::string read_fourcc(std::ifstream &input, const std::uint64_t offset) {
  std::array<char, 4> value{};
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  input.read(value.data(), static_cast<std::streamsize>(value.size()));
  if (!input) {
    throw InspectionError(ErrorKind::corrupt, offset, "truncated FourCC field");
  }
  return {value.data(), value.size()};
}

bool is_container_box(const std::string &type) {
  static const std::set<std::string> types{
      "moov", "trak", "mdia", "minf", "dinf", "stbl", "edts", "udta", "mvex",
      "moof", "traf", "mfra", "sinf", "schi", "tref", "meco"};
  return types.contains(type);
}

std::vector<Box> parse_boxes(std::ifstream &input, const std::uint64_t begin,
                             const std::uint64_t end, const std::size_t depth) {
  if (depth > kMaxDepth) {
    throw InspectionError(ErrorKind::corrupt, begin, "box nesting exceeds the limit of 32");
  }

  std::vector<Box> boxes;
  auto offset = begin;
  while (offset < end) {
    const auto remaining = end - offset;
    if (remaining < kBasicHeaderSize) {
      throw InspectionError(ErrorKind::corrupt, offset, "trailing bytes are shorter than a box header");
    }

    const auto size32 = read_u32(input, offset);
    const auto type = read_fourcc(input, offset + 4U);
    std::uint64_t header_size = kBasicHeaderSize;
    std::uint64_t box_size = size32;

    if (size32 == 1U) {
      if (remaining < kExtendedHeaderSize) {
        throw InspectionError(ErrorKind::corrupt, offset, "truncated extended-size box header");
      }
      box_size = read_u64(input, offset + kBasicHeaderSize);
      header_size = kExtendedHeaderSize;
    } else if (size32 == 0U) {
      box_size = remaining;
    }

    if (type == "uuid") {
      if (remaining < header_size + kUuidExtraSize) {
        throw InspectionError(ErrorKind::corrupt, offset, "truncated uuid box header");
      }
      header_size += kUuidExtraSize;
    }
    if (box_size < header_size) {
      throw InspectionError(ErrorKind::corrupt, offset, "box size is smaller than its header");
    }
    if (box_size > remaining) {
      throw InspectionError(ErrorKind::corrupt, offset, "box extends beyond its parent or file");
    }

    Box box{type, offset, box_size, header_size, {}};
    if (is_container_box(type) && box_size > header_size) {
      box.children = parse_boxes(input, offset + header_size, offset + box_size, depth + 1U);
    }
    boxes.push_back(std::move(box));
    offset += box_size;
  }
  return boxes;
}

FileIdentity identify(std::ifstream &input, const std::vector<Box> &boxes) {
  FileIdentity identity{};
  for (const auto &box : boxes) {
    if (box.type != "ftyp") {
      continue;
    }
    identity.is_iso_bmff = true;
    const auto payload_size = box.size - box.header_size;
    if (payload_size < 8U || ((payload_size - 8U) % 4U) != 0U) {
      throw InspectionError(ErrorKind::corrupt, box.offset, "invalid ftyp payload length");
    }
    const auto payload = box.offset + box.header_size;
    identity.major_brand = read_fourcc(input, payload);
    identity.minor_version = read_u32(input, payload + 4U);
    for (std::uint64_t pos = payload + 8U; pos < box.offset + box.size; pos += 4U) {
      identity.compatible_brands.push_back(read_fourcc(input, pos));
    }
    break;
  }
  return identity;
}

} // namespace

InspectionError::InspectionError(const ErrorKind kind, const std::uint64_t offset,
                                 const std::string &message)
    : std::runtime_error(message), kind_(kind), offset_(offset) {}

ErrorKind InspectionError::kind() const noexcept { return kind_; }
std::uint64_t InspectionError::offset() const noexcept { return offset_; }

Inspection inspect_iso_bmff(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::status(path, error);
  if (error || !std::filesystem::exists(status)) {
    throw InspectionError(ErrorKind::missing, 0, "input file does not exist");
  }
  if (!std::filesystem::is_regular_file(status)) {
    throw InspectionError(ErrorKind::io, 0, "input is not a regular file");
  }
  const auto file_size = std::filesystem::file_size(path, error);
  if (error) {
    throw InspectionError(ErrorKind::io, 0, "cannot determine input file size");
  }
  if (file_size == 0U) {
    throw InspectionError(ErrorKind::unsupported, 0, "empty input is not ISO BMFF");
  }
  if (file_size > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
    throw InspectionError(ErrorKind::io, 0, "input is too large for this platform's file API");
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw InspectionError(ErrorKind::io, 0, "cannot open input for reading");
  }

  Inspection result{path, file_size, {}, parse_boxes(input, 0, file_size, 0)};
  result.identity = identify(input, result.boxes);
  if (!result.identity.is_iso_bmff) {
    throw InspectionError(ErrorKind::unsupported, 0, "no top-level ftyp box found");
  }
  return result;
}

std::string error_kind_name(const ErrorKind kind) {
  switch (kind) {
  case ErrorKind::missing:
    return "missing";
  case ErrorKind::io:
    return "io";
  case ErrorKind::unsupported:
    return "unsupported";
  case ErrorKind::corrupt:
    return "corrupt";
  }
  return "unexpected";
}

} // namespace gb::container
