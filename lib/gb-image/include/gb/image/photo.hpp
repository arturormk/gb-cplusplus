#pragma once

#include "gb/image/jpeg.hpp"
#include "gb/metadata/gpmf.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gb::image {

// EXIF fields of a camera JPEG. Every field is optional: a value is present only when the file
// records it with the expected type. Rationals keep their recorded terms.
struct ExifRational {
  std::uint32_t numerator{};
  std::uint32_t denominator{};
};

struct ExifData {
  std::string byte_order; // "big-endian" or "little-endian"
  std::optional<std::string> make;
  std::optional<std::string> model;
  std::optional<std::string> software;
  std::optional<std::string> date_time_original; // "YYYY:MM:DD HH:MM:SS", camera local clock
  std::optional<std::uint16_t> orientation;
  std::optional<ExifRational> exposure_time; // seconds
  std::optional<ExifRational> f_number;
  std::optional<std::uint32_t> iso;
  std::optional<double> exposure_bias_ev;
  std::optional<std::uint16_t> exposure_program;
  std::optional<std::uint16_t> metering_mode;
  std::optional<std::uint16_t> white_balance;
  std::optional<std::uint16_t> sharpness;
  std::optional<double> focal_length_mm;
  std::optional<std::uint16_t> focal_length_35mm;
  std::optional<double> digital_zoom_ratio;
};

// The EXIF GPS IFD. A position needs both coordinates with their references.
struct PhotoGps {
  double latitude{};              // degrees, north positive
  double longitude{};             // degrees, east positive
  std::optional<double> altitude; // metres; below sea level negative
  std::optional<std::string> utc; // "YYYY-MM-DDTHH:MM:SSZ" when date and time stamps exist
  std::optional<double> dop;
  std::optional<double> speed_mps;
};

// XMP GPano (Google Photo Sphere) geometry: the image is the crop of a full equirectangular canvas.
struct Panorama {
  std::string projection;
  std::uint32_t full_width{};
  std::uint32_t full_height{};
  std::uint32_t crop_left{};
  std::uint32_t crop_top{};
  std::uint32_t crop_width{};
  std::uint32_t crop_height{};
  std::optional<std::uint32_t> source_photos;
};

// GoPro APP6 segment: "GoPro\0" followed by GPMF. Selected settings are copied out; the parsed
// tree keeps everything, including keys this project does not interpret.
struct GoProPhotoMetadata {
  std::optional<std::string> projection;    // PRJT, e.g. GPRO, PANO, ERP0
  std::optional<std::string> resolution;    // PRES, e.g. 5.5MP_W
  std::optional<double> field_of_view;      // ZFOV, degrees
  std::optional<std::string> stabilization; // EISA
  std::optional<std::string> firmware;      // FMWR
  std::optional<std::string> model;         // MINF
  std::vector<gb::metadata::GpmfElement> elements;
  std::string parse_error;
};

enum class PhotoFieldOfView { spherical, superwide, wide, linear, narrow, unknown };

struct PhotoClassification {
  PhotoFieldOfView field_of_view{PhotoFieldOfView::unknown};
  // Horizontal angle covered by an equirectangular image, from its GPano crop; else absent.
  std::optional<double> horizontal_degrees;
  std::string evidence;
};

struct PhotoInspection {
  JpegInspection jpeg;
  std::uint16_t width{};
  std::uint16_t height{};
  std::optional<ExifData> exif;
  std::string exif_error;
  std::optional<PhotoGps> gps;
  std::optional<Panorama> panorama;
  std::optional<GoProPhotoMetadata> gopro;
  PhotoClassification classification;
};

// True when the file starts with a JPEG start-of-image marker.
[[nodiscard]] bool is_jpeg_file(const std::filesystem::path &path);

// Reads the metadata of the primary codestream. Malformed optional metadata is reported in the
// result (exif_error, gopro->parse_error) rather than thrown; a malformed JPEG throws JpegError.
[[nodiscard]] PhotoInspection inspect_photo(const std::filesystem::path &path);

[[nodiscard]] PhotoClassification classify_photo(const std::optional<Panorama> &panorama,
                                                 const std::optional<GoProPhotoMetadata> &gopro);
[[nodiscard]] std::string field_of_view_name(PhotoFieldOfView value);

// Bounded parsers over segment payloads, exposed for tests.
[[nodiscard]] ExifData parse_exif(const std::vector<std::uint8_t> &tiff,
                                  std::optional<PhotoGps> &gps);
[[nodiscard]] std::optional<Panorama> parse_gpano(const std::string &xmp);

} // namespace gb::image
