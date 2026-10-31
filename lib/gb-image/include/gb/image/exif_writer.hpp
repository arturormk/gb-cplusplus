#pragma once

#include "gb/image/photo.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gb::image {

// A UTC time split into its parts, from "YYYY-MM-DDTHH:MM:SS[.fff]Z" (GPSU and EXIF GPS form).
struct UtcTime {
  int year{};
  int month{};
  int day{};
  int hour{};
  int minute{};
  double second{};
};

[[nodiscard]] std::optional<UtcTime> parse_utc(const std::string &text);

// Builds the TIFF structure of an EXIF block holding a GPS IFD (version 2.3, WGS-84 datum,
// latitude, longitude, and altitude, speed and date/time stamps when present) and, when the UTC
// time is known, an EXIF IFD with DateTimeOriginal and OffsetTimeOriginal "+00:00". Nothing else
// is written: no camera make, model, serial number or software. Big-endian.
[[nodiscard]] std::vector<std::uint8_t> build_gps_exif(const PhotoGps &gps);

// The same block as a complete APP1 segment: marker, length, "Exif\0\0" and the TIFF bytes.
[[nodiscard]] std::vector<std::uint8_t> build_gps_exif_segment(const PhotoGps &gps);

// Returns a copy of a JPEG with `app1` placed straight after SOI. Existing JFIF APP0 and EXIF APP1
// segments are removed; other segments and the entropy-coded data are kept unchanged. Throws
// JpegError when the input is not a JPEG or its segments before SOS are malformed.
[[nodiscard]] std::vector<std::uint8_t> insert_exif_segment(const std::vector<std::uint8_t> &jpeg,
                                                            const std::vector<std::uint8_t> &app1);

} // namespace gb::image
