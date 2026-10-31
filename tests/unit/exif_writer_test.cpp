#include "gb/image/exif_writer.hpp"
#include "gb/image/photo.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

bool near(const double value, const double expected, const double tolerance) {
  return std::abs(value - expected) <= tolerance;
}

Bytes tiff_of(const Bytes &segment) {
  require(segment.size() > 10U && segment[0] == 0xffU && segment[1] == 0xe1U, "APP1 marker");
  const auto length = (static_cast<std::size_t>(segment[2]) << 8U) | segment[3];
  require(length + 2U == segment.size(), "APP1 length covers the segment");
  require(std::string(segment.begin() + 4, segment.begin() + 8) == "Exif", "EXIF signature");
  return {segment.begin() + 10, segment.end()};
}

void test_round_trip() {
  gb::image::PhotoGps gps;
  gps.latitude = 40.4167754;
  gps.longitude = -3.7037902;
  gps.altitude = 657.25;
  gps.speed_mps = 5.5;
  gps.utc = "2026-01-01T12:34:56.789Z";
  std::optional<gb::image::PhotoGps> parsed;
  const auto exif = gb::image::parse_exif(tiff_of(gb::image::build_gps_exif_segment(gps)), parsed);
  require(parsed.has_value(), "GPS IFD parses back");
  require(near(parsed->latitude, gps.latitude, 1.0e-6), "latitude round trip");
  require(near(parsed->longitude, gps.longitude, 1.0e-6), "longitude round trip, west negative");
  require(parsed->altitude && near(*parsed->altitude, 657.25, 1.0e-3), "altitude round trip");
  require(parsed->speed_mps && near(*parsed->speed_mps, 5.5, 1.0e-3), "speed round trip");
  require(parsed->utc == std::optional<std::string>("2026-01-01T12:34:56Z"), "GPS date and time");
  require(exif.date_time_original == std::optional<std::string>("2026:01:01 12:34:56"),
          "DateTimeOriginal from UTC");
  require(!exif.make && !exif.model && !exif.software, "no identifying fields");
  require(exif.byte_order == "big-endian", "byte order");

  gps = {};
  gps.latitude = -33.8568;
  gps.longitude = 151.2153;
  gps.altitude = -12.5;
  const auto minimal = gb::image::parse_exif(gb::image::build_gps_exif(gps), parsed);
  require(parsed && near(parsed->latitude, -33.8568, 1.0e-6), "southern latitude");
  require(parsed->altitude && near(*parsed->altitude, -12.5, 1.0e-3), "below sea level");
  require(!parsed->utc && !parsed->speed_mps, "absent fields stay absent");
  require(!minimal.date_time_original, "no DateTimeOriginal without UTC");

  // Seconds that round up to 60 carry into the minutes.
  gps = {};
  gps.latitude = 10.0 + 59.0 / 60.0 + 59.99999 / 3600.0;
  gps.longitude = 0.0;
  (void)gb::image::parse_exif(gb::image::build_gps_exif(gps), parsed);
  require(parsed && near(parsed->latitude, 11.0, 1.0e-6), "carry seconds into minutes");

  bool rejected = false;
  try {
    gps.latitude = 91.0;
    (void)gb::image::build_gps_exif(gps);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  require(rejected, "out-of-range latitude is rejected");
}

void test_parse_utc() {
  require(gb::image::parse_utc("2026-10-31T08:01:02Z").has_value(), "whole seconds");
  require(gb::image::parse_utc("2026-10-31T08:01:02.250Z")->second == 2.25, "fractional seconds");
  require(!gb::image::parse_utc("2026-10-31 08:01:02Z"), "separator");
  require(!gb::image::parse_utc("2026-13-31T08:01:02Z"), "month range");
  require(!gb::image::parse_utc("2026-10-31T08:01:02"), "Z required");
}

Bytes minimal_jpeg() {
  // SOI, JFIF APP0, an old EXIF APP1, a COM segment, SOS with two data bytes, EOI.
  return {0xff, 0xd8, 0xff, 0xe0, 0x00, 0x07, 'J',  'F',  'I',  'F',  0x00, 0xff,
          0xe1, 0x00, 0x08, 'E',  'x',  'i',  'f',  0x00, 0x00, 0xff, 0xfe, 0x00,
          0x04, 'h',  'i',  0xff, 0xda, 0x00, 0x02, 0x12, 0x34, 0xff, 0xd9};
}

void test_insert() {
  gb::image::PhotoGps gps;
  gps.latitude = 1.0;
  gps.longitude = 2.0;
  const auto app1 = gb::image::build_gps_exif_segment(gps);
  const auto output = gb::image::insert_exif_segment(minimal_jpeg(), app1);
  require(output.size() == 2U + app1.size() + 6U + 8U, "JFIF and old EXIF removed");
  require(Bytes(output.begin() + 2,
                output.begin() + 2 + static_cast<std::ptrdiff_t>(app1.size())) == app1,
          "new APP1 straight after SOI");
  require(output[2 + app1.size()] == 0xffU && output[3 + app1.size()] == 0xfeU, "COM kept");
  require(output[output.size() - 1] == 0xd9U && output[output.size() - 3] == 0x34U,
          "scan data kept");

  const auto path = std::filesystem::temp_directory_path() / "gb_exif_writer_test.jpg";
  {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(output.data()),
               static_cast<std::streamsize>(output.size()));
  }
  const auto inspection = gb::image::inspect_jpeg(path);
  std::filesystem::remove(path);
  require(inspection.has_exif && !inspection.has_jfif, "inspector sees EXIF, no JFIF");

  bool rejected = false;
  try {
    (void)gb::image::insert_exif_segment({0x00, 0x01, 0x02, 0x03}, app1);
  } catch (const gb::image::JpegError &) {
    rejected = true;
  }
  require(rejected, "not a JPEG");
  rejected = false;
  try {
    (void)gb::image::insert_exif_segment({0xff, 0xd8, 0xff, 0xe0, 0x40, 0x00, 0x00}, app1);
  } catch (const gb::image::JpegError &) {
    rejected = true;
  }
  require(rejected, "segment longer than the file");
}

} // namespace

int main() {
  try {
    test_round_trip();
    test_parse_utc();
    test_insert();
  } catch (const std::exception &error) {
    std::cerr << "gb_exif_writer_tests: " << error.what() << '\n';
    return 1;
  }
  std::cout << "gb_exif_writer_tests: all tests passed\n";
  return 0;
}
