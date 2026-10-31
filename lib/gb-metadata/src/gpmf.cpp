#include "gb/metadata/gpmf.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

namespace gb::metadata {
namespace {

constexpr std::size_t kMaximumDepth = 16;
constexpr std::size_t kMaximumElements = 1'000'000;

std::uint16_t u16(const std::vector<std::uint8_t> &bytes, const std::size_t offset) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                    bytes[offset + 1U]);
}

std::uint32_t u32(const std::vector<std::uint8_t> &bytes, const std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) | bytes[offset + 3U];
}

std::uint64_t u64(const std::vector<std::uint8_t> &bytes, const std::size_t offset) {
  return (static_cast<std::uint64_t>(u32(bytes, offset)) << 32U) | u32(bytes, offset + 4U);
}

std::string decimal(const long double value) {
  std::ostringstream output;
  output << std::setprecision(12) << value;
  return output.str();
}

std::string text_value(const std::vector<std::uint8_t> &raw) {
  std::string value(raw.begin(), raw.end());
  while (!value.empty() && value.back() == '\0') {
    value.pop_back();
  }
  return value;
}

std::string text_range(const std::vector<std::uint8_t> &raw, const std::size_t begin,
                       const std::size_t size) {
  std::vector<std::uint8_t> part(raw.begin() + static_cast<std::ptrdiff_t>(begin),
                                 raw.begin() + static_cast<std::ptrdiff_t>(begin + size));
  return text_value(part);
}

std::vector<std::string> decode_values(const GpmfElement &element) {
  const auto &raw = element.raw;
  std::vector<std::string> values;
  if (element.type == 'c' || element.type == 'u' || element.type == 'U') {
    if ((element.key == "UNIT" || element.key == "SIUN") && element.repeat > 1U &&
        element.structure_size > 0U) {
      for (std::uint16_t index = 0; index < element.repeat; ++index) {
        values.push_back(text_range(raw, static_cast<std::size_t>(index) * element.structure_size,
                                    element.structure_size));
      }
    } else {
      values.push_back(text_value(raw));
    }
    return values;
  }
  std::size_t primitive_size = 0;
  switch (element.type) {
  case 'b':
  case 'B':
    primitive_size = 1U;
    break;
  case 's':
  case 'S':
    primitive_size = 2U;
    break;
  case 'l':
  case 'L':
  case 'q':
  case 'f':
  case 'F':
    primitive_size = 4U;
    break;
  case 'j':
  case 'J':
  case 'Q':
  case 'd':
    primitive_size = 8U;
    break;
  default:
    return values;
  }
  if (raw.size() % primitive_size != 0U) {
    return values;
  }
  for (std::size_t offset = 0; offset < raw.size(); offset += primitive_size) {
    switch (element.type) {
    case 'b':
      values.push_back(std::to_string(static_cast<std::int8_t>(raw[offset])));
      break;
    case 'B':
      values.push_back(std::to_string(raw[offset]));
      break;
    case 's':
      values.push_back(std::to_string(std::bit_cast<std::int16_t>(u16(raw, offset))));
      break;
    case 'S':
      values.push_back(std::to_string(u16(raw, offset)));
      break;
    case 'l':
      values.push_back(std::to_string(std::bit_cast<std::int32_t>(u32(raw, offset))));
      break;
    case 'L':
      values.push_back(std::to_string(u32(raw, offset)));
      break;
    case 'j':
      values.push_back(std::to_string(std::bit_cast<std::int64_t>(u64(raw, offset))));
      break;
    case 'J':
      values.push_back(std::to_string(u64(raw, offset)));
      break;
    case 'q':
      values.push_back(decimal(std::bit_cast<std::int32_t>(u32(raw, offset)) / 65536.0L));
      break;
    case 'Q':
      values.push_back(decimal(std::bit_cast<std::int64_t>(u64(raw, offset)) / 4294967296.0L));
      break;
    case 'f': {
      const auto value = std::bit_cast<float>(u32(raw, offset));
      values.push_back(decimal(value));
      break;
    }
    case 'd': {
      const auto value = std::bit_cast<double>(u64(raw, offset));
      values.push_back(decimal(value));
      break;
    }
    case 'F':
      values.emplace_back(raw.begin() + static_cast<std::ptrdiff_t>(offset),
                          raw.begin() + static_cast<std::ptrdiff_t>(offset + 4U));
      break;
    default:
      break;
    }
  }
  return values;
}

std::vector<GpmfElement> parse_range(const std::vector<std::uint8_t> &bytes,
                                     const std::size_t begin, const std::size_t end,
                                     const std::size_t depth, std::size_t &element_count) {
  if (depth > kMaximumDepth) {
    throw GpmfError(begin, "GPMF nesting exceeds the limit of 16");
  }
  std::vector<GpmfElement> elements;
  auto offset = begin;
  while (offset < end) {
    if (end - offset < 8U) {
      const auto padding = std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                       bytes.begin() + static_cast<std::ptrdiff_t>(end),
                                       [](const std::uint8_t byte) { return byte == 0U; });
      if (padding) {
        break;
      }
      throw GpmfError(offset, "truncated GPMF record header");
    }
    if (++element_count > kMaximumElements) {
      throw GpmfError(offset, "GPMF element count exceeds the safety limit");
    }
    GpmfElement element;
    element.offset = offset;
    element.key.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                       bytes.begin() + static_cast<std::ptrdiff_t>(offset + 4U));
    element.type = bytes[offset + 4U];
    element.structure_size = bytes[offset + 5U];
    element.repeat = u16(bytes, offset + 6U);
    const auto data_size = static_cast<std::uint64_t>(element.structure_size) * element.repeat;
    const auto padded_size = (data_size + 3U) & ~std::uint64_t{3U};
    if (data_size > end - offset - 8U || padded_size > end - offset - 8U) {
      throw GpmfError(offset, "GPMF record extends beyond its parent payload");
    }
    const auto data_begin = offset + 8U;
    const auto data_end = data_begin + static_cast<std::size_t>(data_size);
    element.raw.assign(bytes.begin() + static_cast<std::ptrdiff_t>(data_begin),
                       bytes.begin() + static_cast<std::ptrdiff_t>(data_end));
    if (element.type == 0U) {
      element.children = parse_range(bytes, data_begin, data_end, depth + 1U, element_count);
    } else {
      element.values = decode_values(element);
    }
    elements.push_back(std::move(element));
    offset = data_begin + static_cast<std::size_t>(padded_size);
  }
  return elements;
}

void normalize(std::vector<GpmfElement> &elements) {
  std::vector<long double> scale;
  std::string unit;
  for (const auto &element : elements) {
    if (element.key == "SCAL") {
      for (const auto &value : element.values) {
        try {
          scale.push_back(std::stold(value));
        } catch (const std::exception &) {
          scale.clear();
          break;
        }
      }
    } else if ((element.key == "SIUN" || element.key == "UNIT") && !element.values.empty()) {
      unit.clear();
      for (std::size_t index = 0; index < element.values.size(); ++index) {
        unit += (index == 0U ? "" : ",") + element.values[index];
      }
    }
  }
  static const std::set<std::string> non_data_keys{"SCAL", "SIUN", "UNIT", "STNM", "DVNM", "DVID",
                                                   "TSMP", "STMP", "TICK", "TOCK", "TIMO", "TYPE",
                                                   "ORIN", "ORIO", "GPSU", "GPSF", "GPSP", "GPSA"};
  for (auto &element : elements) {
    if (!element.children.empty()) {
      normalize(element.children);
    }
    if (!non_data_keys.contains(element.key)) {
      element.unit = unit;
    }
    if (scale.empty() || element.values.empty() || non_data_keys.contains(element.key) ||
        element.type == 'c' || element.type == 'u' || element.type == 'U' || element.type == 'F') {
      continue;
    }
    for (std::size_t index = 0; index < element.values.size(); ++index) {
      const auto divisor = scale[index % scale.size()];
      if (divisor == 0.0L) {
        element.normalized_values.clear();
        break;
      }
      try {
        element.normalized_values.push_back(decimal(std::stold(element.values[index]) / divisor));
      } catch (const std::exception &) {
        element.normalized_values.clear();
        break;
      }
    }
  }
}

} // namespace

GpmfError::GpmfError(const std::uint64_t offset, const std::string &message)
    : std::runtime_error(message), offset_(offset) {}

std::uint64_t GpmfError::offset() const noexcept { return offset_; }

std::vector<GpmfElement> parse_gpmf(const std::vector<std::uint8_t> &bytes) {
  std::size_t element_count = 0;
  auto elements = parse_range(bytes, 0, bytes.size(), 0, element_count);
  normalize(elements);
  return elements;
}

std::string type_name(const std::uint8_t type) {
  switch (type) {
  case 0:
    return "nested";
  case 'c':
    return "ascii";
  case 'u':
    return "utf8";
  case 'U':
    return "utc_datetime";
  case 'b':
    return "int8";
  case 'B':
    return "uint8";
  case 's':
    return "int16";
  case 'S':
    return "uint16";
  case 'l':
    return "int32";
  case 'L':
    return "uint32";
  case 'j':
    return "int64";
  case 'J':
    return "uint64";
  case 'q':
    return "q15.16";
  case 'Q':
    return "q31.32";
  case 'f':
    return "float32";
  case 'd':
    return "float64";
  case 'F':
    return "fourcc";
  case '?':
    return "complex";
  case '#':
    return "compressed";
  case 'G':
    return "guid";
  default:
    return "unknown";
  }
}

bool is_known_key(const std::string &key) {
  static const std::set<std::string> keys{
      "DEVC", "DVID", "DVNM", "STRM", "STNM", "SIUN", "UNIT", "MTRX", "ORIN", "ORIO", "SCAL",
      "TYPE", "TSMP", "TICK", "TOCK", "TIMO", "STMP", "STPS", "PFRM", "TMPC", "EMPT", "QUAN",
      "VERS", "FREE", "RMRK", "GPS5", "GPS9", "GPSU", "GPSF", "GPSP", "ACCL", "GYRO", "MAGN",
      "GRAV", "CORI", "IORI", "MWET", "ISOE", "SHUT", "WBAL", "FACE"};
  return keys.contains(key);
}

} // namespace gb::metadata
