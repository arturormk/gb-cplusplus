#include "gb/image/exif_writer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace gb::image {
namespace {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint16_t kByte = 1U;
constexpr std::uint16_t kAscii = 2U;
constexpr std::uint16_t kLong = 4U;
constexpr std::uint16_t kRational = 5U;

void put16(Bytes &out, const std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
  out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void put32(Bytes &out, const std::uint32_t value) {
  put16(out, value >> 16U);
  put16(out, value & 0xffffU);
}

struct Entry {
  std::uint16_t tag{};
  std::uint16_t type{};
  std::uint32_t count{};
  Bytes value;    // big-endian
  bool pointer{}; // a LONG holding the offset of another IFD, filled in at layout
};

Entry ascii(const std::uint16_t tag, const std::string &text) {
  Bytes value(text.begin(), text.end());
  value.push_back(0U);
  return {tag, kAscii, static_cast<std::uint32_t>(value.size()), value, false};
}

Entry bytes(const std::uint16_t tag, const Bytes &value) {
  return {tag, kByte, static_cast<std::uint32_t>(value.size()), value, false};
}

// Unsigned rationals with a fixed denominator; values are non-negative and bounded by callers.
Entry rationals(const std::uint16_t tag, const std::vector<double> &values,
                const std::vector<std::uint32_t> &denominators) {
  Entry entry{tag, kRational, static_cast<std::uint32_t>(values.size()), {}, false};
  for (std::size_t index = 0; index < values.size(); ++index) {
    const auto denominator = denominators[index];
    const auto scaled = std::llround(values[index] * denominator);
    put32(entry.value, static_cast<std::uint32_t>(std::clamp<long long>(
                           scaled, 0, std::numeric_limits<std::uint32_t>::max())));
    put32(entry.value, denominator);
  }
  return entry;
}

Entry pointer(const std::uint16_t tag) { return {tag, kLong, 1U, Bytes(4U, 0U), true}; }

// Degrees, minutes and seconds; seconds to 1/10000.
std::vector<double> dms(const double value) {
  auto magnitude = std::abs(value);
  auto degrees = std::floor(magnitude);
  auto minutes = std::floor((magnitude - degrees) * 60.0);
  auto seconds = ((magnitude - degrees) * 60.0 - minutes) * 60.0;
  if (std::llround(seconds * 10000.0) >= 600000) {
    seconds = 0.0;
    minutes += 1.0;
  }
  if (minutes >= 60.0) {
    minutes = 0.0;
    degrees += 1.0;
  }
  return {degrees, minutes, seconds};
}

std::string two(const int value) {
  std::array<char, 8> text{};
  std::snprintf(text.data(), text.size(), "%02d", value);
  return text.data();
}

// Lays out IFDs one after another, each followed by its out-of-line values. IFD 0 is first; each
// pointer entry of IFD 0 refers to the IFD with the same index in `children`.
Bytes layout(const std::vector<Entry> &ifd0, const std::vector<std::vector<Entry>> &children) {
  std::vector<const std::vector<Entry> *> ifds{&ifd0};
  for (const auto &child : children)
    ifds.push_back(&child);

  const auto ifd_size = [](const std::vector<Entry> &entries) {
    std::size_t size = 2U + entries.size() * 12U + 4U;
    for (const auto &entry : entries) {
      if (entry.value.size() > 4U)
        size += entry.value.size() + (entry.value.size() % 2U);
    }
    return size;
  };
  std::vector<std::uint32_t> offsets;
  std::size_t cursor = 8U;
  for (const auto *ifd : ifds) {
    offsets.push_back(static_cast<std::uint32_t>(cursor));
    cursor += ifd_size(*ifd);
  }

  Bytes out{'M', 'M', 0x00U, 0x2aU};
  put32(out, offsets.front());
  for (std::size_t index = 0; index < ifds.size(); ++index) {
    auto entries = *ifds[index];
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) { return a.tag < b.tag; });
    std::size_t child = 1U;
    for (auto &entry : entries) {
      if (entry.pointer) {
        entry.value.clear();
        put32(entry.value, offsets.at(child++));
      }
    }
    auto data = static_cast<std::uint32_t>(offsets[index] + 2U + entries.size() * 12U + 4U);
    Bytes area;
    put16(out, static_cast<std::uint32_t>(entries.size()));
    for (const auto &entry : entries) {
      put16(out, entry.tag);
      put16(out, entry.type);
      put32(out, entry.count);
      if (entry.value.size() <= 4U) {
        auto inline_value = entry.value;
        inline_value.resize(4U, 0U);
        out.insert(out.end(), inline_value.begin(), inline_value.end());
      } else {
        put32(out, data + static_cast<std::uint32_t>(area.size()));
        area.insert(area.end(), entry.value.begin(), entry.value.end());
        if (entry.value.size() % 2U != 0U)
          area.push_back(0U);
      }
    }
    put32(out, 0U); // no next IFD
    out.insert(out.end(), area.begin(), area.end());
  }
  return out;
}

} // namespace

std::optional<UtcTime> parse_utc(const std::string &text) {
  UtcTime time;
  int consumed{};
  if (text.size() < 20U || text.size() > 40U || text.back() != 'Z')
    return std::nullopt;
  if (std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%lf%n", &time.year, &time.month, &time.day,
                  &time.hour, &time.minute, &time.second, &consumed) != 6 ||
      static_cast<std::size_t>(consumed) != text.size() - 1U) {
    return std::nullopt;
  }
  if (time.year < 1900 || time.year > 9999 || time.month < 1 || time.month > 12 || time.day < 1 ||
      time.day > 31 || time.hour < 0 || time.hour > 23 || time.minute < 0 || time.minute > 59 ||
      !(time.second >= 0.0 && time.second < 61.0)) {
    return std::nullopt;
  }
  return time;
}

Bytes build_gps_exif(const PhotoGps &gps) {
  if (!std::isfinite(gps.latitude) || !std::isfinite(gps.longitude) ||
      std::abs(gps.latitude) > 90.0 || std::abs(gps.longitude) > 180.0) {
    throw std::invalid_argument("GPS position out of range");
  }
  const auto utc = gps.utc ? parse_utc(*gps.utc) : std::nullopt;

  std::vector<Entry> gps_ifd;
  gps_ifd.push_back(bytes(0x0000U, {2U, 3U, 0U, 0U}));
  gps_ifd.push_back(ascii(0x0001U, gps.latitude < 0.0 ? "S" : "N"));
  gps_ifd.push_back(rationals(0x0002U, dms(gps.latitude), {1U, 1U, 10000U}));
  gps_ifd.push_back(ascii(0x0003U, gps.longitude < 0.0 ? "W" : "E"));
  gps_ifd.push_back(rationals(0x0004U, dms(gps.longitude), {1U, 1U, 10000U}));
  if (gps.altitude && std::isfinite(*gps.altitude) && std::abs(*gps.altitude) < 1.0e6) {
    gps_ifd.push_back(bytes(0x0005U, {static_cast<std::uint8_t>(*gps.altitude < 0.0 ? 1U : 0U)}));
    gps_ifd.push_back(rationals(0x0006U, {std::abs(*gps.altitude)}, {1000U}));
  }
  if (utc) {
    const auto second = std::floor(utc->second * 1000.0) / 1000.0;
    gps_ifd.push_back(rationals(
        0x0007U, {static_cast<double>(utc->hour), static_cast<double>(utc->minute), second},
        {1U, 1U, 1000U}));
  }
  if (gps.speed_mps && std::isfinite(*gps.speed_mps) && *gps.speed_mps >= 0.0 &&
      *gps.speed_mps < 1.0e5) {
    gps_ifd.push_back(ascii(0x000cU, "K"));
    gps_ifd.push_back(rationals(0x000dU, {*gps.speed_mps * 3.6}, {1000U}));
  }
  gps_ifd.push_back(ascii(0x0012U, "WGS-84"));
  if (utc) {
    gps_ifd.push_back(
        ascii(0x001dU, std::to_string(utc->year) + ":" + two(utc->month) + ":" + two(utc->day)));
  }

  std::vector<Entry> ifd0;
  std::vector<std::vector<Entry>> children;
  if (utc) {
    const auto whole = static_cast<int>(std::floor(utc->second));
    std::vector<Entry> exif_ifd;
    exif_ifd.push_back(ascii(0x9003U, std::to_string(utc->year) + ":" + two(utc->month) + ":" +
                                          two(utc->day) + " " + two(utc->hour) + ":" +
                                          two(utc->minute) + ":" + two(std::min(whole, 59))));
    exif_ifd.push_back(ascii(0x9011U, "+00:00"));
    ifd0.push_back(pointer(0x8769U));
    children.push_back(std::move(exif_ifd));
  }
  ifd0.push_back(pointer(0x8825U));
  children.push_back(std::move(gps_ifd));
  return layout(ifd0, children);
}

Bytes build_gps_exif_segment(const PhotoGps &gps) {
  const auto tiff = build_gps_exif(gps);
  static constexpr std::array<std::uint8_t, 6> kSignature{'E', 'x', 'i', 'f', 0U, 0U};
  const auto length = 2U + kSignature.size() + tiff.size();
  if (length > 0xffffU)
    throw std::length_error("EXIF segment too large");
  Bytes out{0xffU, 0xe1U};
  put16(out, static_cast<std::uint32_t>(length));
  out.insert(out.end(), kSignature.begin(), kSignature.end());
  out.insert(out.end(), tiff.begin(), tiff.end());
  return out;
}

Bytes insert_exif_segment(const Bytes &jpeg, const Bytes &app1) {
  if (jpeg.size() < 4U || jpeg[0] != 0xffU || jpeg[1] != 0xd8U)
    throw JpegError(JpegErrorKind::corrupt, 0U, "not a JPEG: missing start-of-image marker");
  Bytes out{0xffU, 0xd8U};
  out.insert(out.end(), app1.begin(), app1.end());
  std::size_t cursor = 2U;
  while (true) {
    if (cursor + 4U > jpeg.size() || jpeg[cursor] != 0xffU)
      throw JpegError(JpegErrorKind::corrupt, cursor, "expected a JPEG marker");
    const auto marker = jpeg[cursor + 1U];
    if (marker == 0xdaU || marker == 0xd9U) {
      out.insert(out.end(), jpeg.begin() + static_cast<std::ptrdiff_t>(cursor), jpeg.end());
      return out;
    }
    const std::size_t length = (static_cast<std::size_t>(jpeg[cursor + 2U]) << 8U) |
                               static_cast<std::size_t>(jpeg[cursor + 3U]);
    if (length < 2U || cursor + 2U + length > jpeg.size())
      throw JpegError(JpegErrorKind::corrupt, cursor, "JPEG segment extends beyond the file");
    const auto payload = jpeg.begin() + static_cast<std::ptrdiff_t>(cursor + 4U);
    const auto payload_size = length - 2U;
    const auto starts = [&](const std::string_view signature) {
      return payload_size >= signature.size() &&
             std::equal(signature.begin(), signature.end(), payload);
    };
    const auto drop = (marker == 0xe0U && starts(std::string_view("JFIF\0", 5U))) ||
                      (marker == 0xe1U && starts(std::string_view("Exif\0\0", 6U)));
    if (!drop) {
      out.insert(out.end(), jpeg.begin() + static_cast<std::ptrdiff_t>(cursor),
                 jpeg.begin() + static_cast<std::ptrdiff_t>(cursor + 2U + length));
    }
    cursor += 2U + length;
  }
}

} // namespace gb::image
