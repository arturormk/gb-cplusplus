#include "gb/image/photo.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
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

bool near(const double value, const double expected, const double tolerance = 1e-6) {
  return std::abs(value - expected) <= tolerance;
}

// TIFF writer for one byte order. Values that do not fit in four bytes go to a data area after
// the IFDs; offsets are relative to the TIFF header, as in EXIF.
class Tiff {
public:
  explicit Tiff(const bool big_endian) : big_endian_(big_endian) {}

  struct Entry {
    std::uint16_t tag;
    std::uint16_t type;
    std::uint32_t count;
    Bytes value; // already in file byte order
  };

  Bytes u16(const std::uint16_t value) const {
    return big_endian_
               ? Bytes{static_cast<std::uint8_t>(value >> 8U), static_cast<std::uint8_t>(value)}
               : Bytes{static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8U)};
  }
  Bytes u32(const std::uint32_t value) const {
    auto high = u16(static_cast<std::uint16_t>(value >> 16U));
    auto low = u16(static_cast<std::uint16_t>(value));
    if (!big_endian_)
      std::swap(high, low);
    high.insert(high.end(), low.begin(), low.end());
    return high;
  }
  Entry ascii(const std::uint16_t tag, const std::string &text) const {
    Bytes value(text.begin(), text.end());
    value.push_back(0U);
    return {tag, 2U, static_cast<std::uint32_t>(value.size()), value};
  }
  Entry shorts(const std::uint16_t tag, const std::uint16_t number) const {
    return {tag, 3U, 1U, u16(number)};
  }
  Entry longs(const std::uint16_t tag, const std::uint32_t number) const {
    return {tag, 4U, 1U, u32(number)};
  }
  Entry byte(const std::uint16_t tag, const std::uint8_t number) const {
    return {tag, 1U, 1U, {number}};
  }
  Entry rationals(const std::uint16_t tag,
                  const std::vector<std::pair<std::uint32_t, std::uint32_t>> &values,
                  const std::uint16_t type = 5U) const {
    Bytes value;
    for (const auto &[numerator, denominator] : values) {
      const auto a = u32(numerator);
      const auto b = u32(denominator);
      value.insert(value.end(), a.begin(), a.end());
      value.insert(value.end(), b.begin(), b.end());
    }
    return {tag, type, static_cast<std::uint32_t>(values.size()), value};
  }

  // Lays out IFD0, then the EXIF and GPS IFDs; pointer entries are added automatically.
  Bytes build(std::vector<Entry> ifd0, const std::vector<Entry> &exif,
              const std::vector<Entry> &gps) const {
    const auto ifd_size = [](const std::size_t count) { return 2U + count * 12U + 4U; };
    const auto ifd0_count = ifd0.size() + (exif.empty() ? 0U : 1U) + (gps.empty() ? 0U : 1U);
    const auto exif_offset = static_cast<std::uint32_t>(8U + ifd_size(ifd0_count));
    const auto gps_offset =
        static_cast<std::uint32_t>(exif_offset + (exif.empty() ? 0U : ifd_size(exif.size())));
    auto data_offset =
        static_cast<std::uint32_t>(gps_offset + (gps.empty() ? 0U : ifd_size(gps.size())));
    if (!exif.empty())
      ifd0.push_back(longs(0x8769U, exif_offset));
    if (!gps.empty())
      ifd0.push_back(longs(0x8825U, gps_offset));

    Bytes header = big_endian_ ? Bytes{'M', 'M'} : Bytes{'I', 'I'};
    append(header, u16(42U));
    append(header, u32(8U));
    Bytes data;
    const auto write_ifd = [&](const std::vector<Entry> &entries) {
      append(header, u16(static_cast<std::uint16_t>(entries.size())));
      for (const auto &entry : entries) {
        append(header, u16(entry.tag));
        append(header, u16(entry.type));
        append(header, u32(entry.count));
        if (entry.value.size() <= 4U) {
          auto inline_value = entry.value;
          inline_value.resize(4U, 0U);
          append(header, inline_value);
        } else {
          append(header, u32(data_offset + static_cast<std::uint32_t>(data.size())));
          append(data, entry.value);
        }
      }
      append(header, u32(0U));
    };
    write_ifd(ifd0);
    if (!exif.empty())
      write_ifd(exif);
    if (!gps.empty())
      write_ifd(gps);
    append(header, data);
    return header;
  }

  static void append(Bytes &target, const Bytes &bytes) {
    target.insert(target.end(), bytes.begin(), bytes.end());
  }

private:
  bool big_endian_;
};

void append_u16(Bytes &bytes, const std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_segment(Bytes &bytes, const std::uint8_t marker, const Bytes &payload) {
  bytes.push_back(0xffU);
  bytes.push_back(marker);
  append_u16(bytes, static_cast<std::uint16_t>(payload.size() + 2U));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
}

Bytes klv(const std::string &key, const char type, const std::uint8_t size,
          const std::uint16_t repeat, const Bytes &payload) {
  Bytes bytes(key.begin(), key.end());
  bytes.push_back(static_cast<std::uint8_t>(type));
  bytes.push_back(size);
  append_u16(bytes, repeat);
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  while (bytes.size() % 4U != 0U)
    bytes.push_back(0U);
  return bytes;
}

Bytes gopro_app6(const std::string &projection, const std::string &resolution,
                 const float field_of_view) {
  std::uint32_t bits{};
  std::memcpy(&bits, &field_of_view, sizeof(bits));
  Bytes children;
  for (const auto &item :
       {klv("PRJT", 'F', 4U, 1U, Bytes(projection.begin(), projection.end())),
        klv("PRES", 'c', 1U, static_cast<std::uint16_t>(resolution.size()),
            Bytes(resolution.begin(), resolution.end())),
        klv("ZFOV", 'f', 4U, 1U,
            {static_cast<std::uint8_t>(bits >> 24U), static_cast<std::uint8_t>(bits >> 16U),
             static_cast<std::uint8_t>(bits >> 8U), static_cast<std::uint8_t>(bits)})}) {
    children.insert(children.end(), item.begin(), item.end());
  }
  Bytes payload{'G', 'o', 'P', 'r', 'o', 0U};
  const auto device = klv("DEVC", '\0', 1U, static_cast<std::uint16_t>(children.size()), children);
  payload.insert(payload.end(), device.begin(), device.end());
  return payload;
}

Bytes make_photo(const Bytes &tiff, const std::string &xmp, const Bytes &app6) {
  Bytes bytes{0xffU, 0xd8U};
  if (!tiff.empty()) {
    Bytes exif{'E', 'x', 'i', 'f', 0U, 0U};
    exif.insert(exif.end(), tiff.begin(), tiff.end());
    append_segment(bytes, 0xe1U, exif);
  }
  if (!xmp.empty()) {
    const std::string signature("http://ns.adobe.com/xap/1.0/\0", 29);
    Bytes payload(signature.begin(), signature.end());
    payload.insert(payload.end(), xmp.begin(), xmp.end());
    append_segment(bytes, 0xe1U, payload);
  }
  if (!app6.empty())
    append_segment(bytes, 0xe6U, app6);
  append_segment(bytes, 0xc0U,
                 {8U, 0U, 16U, 0U, 32U, 3U, 1U, 0x22U, 0U, 2U, 0x11U, 1U, 3U, 0x11U, 1U});
  append_segment(bytes, 0xdaU, {3U, 1U, 0U, 2U, 0x11U, 3U, 0x11U, 0U, 63U, 0U});
  bytes.insert(bytes.end(), {0x11U, 0x22U, 0xffU, 0xd9U});
  return bytes;
}

class FixtureDirectory {
public:
  FixtureDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("gb-photo-tests-" + std::to_string(stamp));
    std::filesystem::create_directory(path_);
  }
  ~FixtureDirectory() { std::filesystem::remove_all(path_); }

  std::filesystem::path write(const std::string &name, const Bytes &bytes) const {
    const auto path = path_ / name;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output)
      throw std::runtime_error("could not write photo test fixture");
    return path;
  }

private:
  std::filesystem::path path_;
};

const std::string kSphericalXmp =
    R"(<x:xmpmeta><rdf:RDF><rdf:Description GPano:ProjectionType="equirectangular" )"
    R"(GPano:CroppedAreaLeftPixels="0" GPano:CroppedAreaTopPixels="0" )"
    R"(GPano:CroppedAreaImageWidthPixels="5760" GPano:CroppedAreaImageHeightPixels="2880" )"
    R"(GPano:FullPanoWidthPixels="5760" GPano:FullPanoHeightPixels="2880" GPano:SourcePhotosCount="2"/>)"
    R"(</rdf:RDF></x:xmpmeta>)";

// Element form, as other writers produce it, for a centred 270x90 degree crop.
const std::string kSuperwideXmp =
    "<rdf:Description><GPano:ProjectionType>equirectangular</GPano:ProjectionType>"
    "<GPano:CroppedAreaLeftPixels>720</GPano:CroppedAreaLeftPixels>"
    "<GPano:CroppedAreaTopPixels>720</GPano:CroppedAreaTopPixels>"
    "<GPano:CroppedAreaImageWidthPixels>4320</GPano:CroppedAreaImageWidthPixels>"
    "<GPano:CroppedAreaImageHeightPixels>1440</GPano:CroppedAreaImageHeightPixels>"
    "<GPano:FullPanoWidthPixels>5760</GPano:FullPanoWidthPixels>"
    "<GPano:FullPanoHeightPixels>2880</GPano:FullPanoHeightPixels></rdf:Description>";

void test_spherical_photo(const FixtureDirectory &fixtures) {
  const Tiff tiff(true);
  const auto exif = tiff.build(
      {tiff.ascii(0x010fU, "GoPro"), tiff.ascii(0x0110U, "GoPro Max"), tiff.shorts(0x0112U, 1U)},
      {tiff.rationals(0x829aU, {{1U, 100U}}), tiff.rationals(0x829dU, {{28U, 10U}}),
       tiff.shorts(0x8827U, 645U), tiff.ascii(0x9003U, "2026:01:02 13:14:15"),
       tiff.rationals(0x9204U, {{static_cast<std::uint32_t>(-1), 3U}}, 10U),
       tiff.rationals(0x920aU, {{3U, 1U}}), tiff.shorts(0xa405U, 15U)},
      {tiff.ascii(1U, "S"), tiff.rationals(2U, {{12U, 1U}, {30U, 1U}, {0U, 1U}}),
       tiff.ascii(3U, "W"), tiff.rationals(4U, {{4U, 1U}, {15U, 1U}, {36U, 1U}}), tiff.byte(5U, 1U),
       tiff.rationals(6U, {{12500U, 1000U}}), tiff.rationals(7U, {{11U, 1U}, {14U, 1U}, {15U, 1U}}),
       tiff.ascii(0x1dU, "2026:01:02"), tiff.ascii(0x0cU, "K"),
       tiff.rationals(0x0dU, {{36U, 1U}})});
  const auto path = fixtures.write(
      "spherical.jpg", make_photo(exif, kSphericalXmp, gopro_app6("ERP0", "16.6MP_S", 360.0F)));
  require(gb::image::is_jpeg_file(path), "photo fixture is not recognised as JPEG");

  const auto photo = gb::image::inspect_photo(path);
  require(photo.width == 32U && photo.height == 16U, "primary dimensions are wrong");
  require(photo.exif.has_value() && photo.exif_error.empty(), "EXIF was not parsed");
  const auto &values = *photo.exif;
  require(values.byte_order == "big-endian", "byte order is wrong");
  require(values.make == "GoPro" && values.model == "GoPro Max" && values.orientation == 1U,
          "IFD0 fields are wrong");
  require(values.exposure_time && values.exposure_time->numerator == 1U &&
              values.exposure_time->denominator == 100U,
          "exposure time is wrong");
  require(values.f_number && values.f_number->numerator == 28U, "f-number is wrong");
  require(values.iso == 645U, "ISO is wrong");
  require(values.date_time_original == "2026:01:02 13:14:15", "capture time is wrong");
  require(values.exposure_bias_ev && near(*values.exposure_bias_ev, -1.0 / 3.0),
          "signed exposure bias is wrong");
  require(values.focal_length_mm && near(*values.focal_length_mm, 3.0) &&
              values.focal_length_35mm == 15U,
          "focal length is wrong");

  require(photo.gps.has_value(), "GPS was not parsed");
  require(near(photo.gps->latitude, -12.5), "southern latitude is wrong");
  require(near(photo.gps->longitude, -(4.0 + 15.0 / 60.0 + 36.0 / 3600.0)),
          "western longitude is wrong");
  require(photo.gps->altitude && near(*photo.gps->altitude, -12.5),
          "below-sea-level altitude is wrong");
  require(photo.gps->utc == "2026-01-02T11:14:15Z", "GPS UTC is wrong");
  require(photo.gps->speed_mps && near(*photo.gps->speed_mps, 10.0),
          "km/h speed conversion is wrong");

  require(photo.panorama.has_value() && photo.panorama->full_width == 5760U &&
              photo.panorama->source_photos == 2U,
          "GPano attributes were not parsed");
  require(photo.gopro.has_value() && photo.gopro->parse_error.empty(), "GoPro APP6 was not parsed");
  require(photo.gopro->projection == "ERP0" && photo.gopro->resolution == "16.6MP_S",
          "GoPro strings are wrong");
  require(photo.gopro->field_of_view && near(*photo.gopro->field_of_view, 360.0),
          "GoPro ZFOV is wrong");
  require(photo.classification.field_of_view == gb::image::PhotoFieldOfView::spherical &&
              photo.classification.horizontal_degrees &&
              near(*photo.classification.horizontal_degrees, 360.0),
          "spherical photo was misclassified");
}

void test_little_endian_without_gps(const FixtureDirectory &fixtures) {
  const Tiff tiff(false);
  const auto exif = tiff.build({tiff.ascii(0x010fU, "GoPro")}, {tiff.longs(0x8827U, 3200U)}, {});
  const auto path =
      fixtures.write("wide.jpg", make_photo(exif, {}, gopro_app6("GPRO", "5.5MP_W", 148.4F)));
  const auto photo = gb::image::inspect_photo(path);
  require(photo.exif && photo.exif->byte_order == "little-endian",
          "little-endian EXIF was not parsed");
  require(photo.exif->make == "GoPro" && photo.exif->iso == 3200U,
          "little-endian values are wrong");
  require(!photo.gps.has_value() && !photo.panorama.has_value(),
          "absent GPS or GPano was invented");
  require(photo.classification.field_of_view == gb::image::PhotoFieldOfView::wide &&
              !photo.classification.horizontal_degrees,
          "wide photo was misclassified");
}

void test_damaged_exif_is_reported(const FixtureDirectory &fixtures) {
  Bytes tiff{'M', 'M', 0U, 42U, 0U, 0U, 0x10U, 0U}; // IFD0 offset far beyond the segment
  const auto path = fixtures.write("damaged.jpg", make_photo(tiff, kSuperwideXmp, {}));
  const auto photo = gb::image::inspect_photo(path);
  require(!photo.exif.has_value() && !photo.exif_error.empty(), "damaged EXIF was not reported");
  require(!photo.gopro.has_value(), "missing APP6 was invented");
  require(photo.panorama && photo.panorama->crop_left == 720U &&
              photo.panorama->crop_width == 4320U,
          "GPano elements were not parsed");
  require(photo.classification.field_of_view == gb::image::PhotoFieldOfView::superwide &&
              near(*photo.classification.horizontal_degrees, 270.0),
          "superwide photo was misclassified");
}

void test_classification_without_gpano() {
  using gb::image::PhotoFieldOfView;
  const auto classify = [](const std::string &projection, const std::string &resolution) {
    gb::image::GoProPhotoMetadata gopro;
    if (!projection.empty())
      gopro.projection = projection;
    if (!resolution.empty())
      gopro.resolution = resolution;
    return gb::image::classify_photo(std::nullopt, gopro).field_of_view;
  };
  require(classify("ERP0", "") == PhotoFieldOfView::spherical, "ERP0 is not spherical");
  require(classify("PANO", "") == PhotoFieldOfView::superwide, "PANO is not superwide");
  require(classify("GPRO", "5.5MP_L") == PhotoFieldOfView::linear, "_L is not linear");
  require(classify("GPRO", "5.5MP_N") == PhotoFieldOfView::narrow, "_N is not narrow");
  require(classify("", "") == PhotoFieldOfView::unknown, "no evidence is not unknown");
  require(gb::image::classify_photo(std::nullopt, std::nullopt).field_of_view ==
              PhotoFieldOfView::unknown,
          "no metadata is not unknown");
  // A crop that does not fit its canvas is rejected rather than trusted.
  require(!gb::image::parse_gpano(
              R"(GPano:ProjectionType="equirectangular" GPano:FullPanoWidthPixels="100" )"
              R"(GPano:FullPanoHeightPixels="50" GPano:CroppedAreaImageWidthPixels="80" )"
              R"(GPano:CroppedAreaImageHeightPixels="50" GPano:CroppedAreaLeftPixels="40")"),
          "out-of-canvas GPano crop was accepted");
}

} // namespace

int main() {
  try {
    const FixtureDirectory fixtures;
    test_spherical_photo(fixtures);
    test_little_endian_without_gps(fixtures);
    test_damaged_exif_is_reported(fixtures);
    test_classification_without_gpano();
  } catch (const std::exception &error) {
    std::cerr << "gb_photo_tests: " << error.what() << '\n';
    return 1;
  }
  std::cout << "gb_photo_tests: all tests passed\n";
  return 0;
}
