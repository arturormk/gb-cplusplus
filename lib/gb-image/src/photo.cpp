#include "gb/image/photo.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace gb::image {
namespace {

constexpr std::size_t kMaximumIfdEntries = 512;
constexpr std::string_view kExifSignature("Exif\0\0", 6);
constexpr std::string_view kXmpSignature("http://ns.adobe.com/xap/1.0/\0", 29);
constexpr std::string_view kGoProSignature("GoPro\0", 6);

using Bytes = std::vector<std::uint8_t>;

bool starts_with(const Bytes &bytes, const std::string_view signature) {
  return bytes.size() >= signature.size() &&
         std::equal(signature.begin(), signature.end(), bytes.begin(),
                    [](const char left, const std::uint8_t right) {
                      return static_cast<std::uint8_t>(left) == right;
                    });
}

Bytes read_payload(std::ifstream &input, const JpegSegment &segment) {
  // Marker (2) and length (2) precede the payload; JPEG lengths bound it to 64 KiB.
  if (segment.size < 4U) return {};
  Bytes bytes(static_cast<std::size_t>(segment.size - 4U));
  input.clear();
  input.seekg(static_cast<std::streamoff>(segment.offset + 4U), std::ios::beg);
  input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input) throw JpegError(segment.offset, "truncated metadata segment");
  return bytes;
}

class TiffReader {
public:
  explicit TiffReader(const Bytes &bytes) : bytes_(bytes) {
    if (bytes_.size() < 8U) throw std::runtime_error("EXIF TIFF header is truncated");
    if (bytes_[0] == 'M' && bytes_[1] == 'M') big_endian_ = true;
    else if (bytes_[0] == 'I' && bytes_[1] == 'I') big_endian_ = false;
    else throw std::runtime_error("EXIF byte order mark is invalid");
    if (u16(2) != 42U) throw std::runtime_error("EXIF TIFF magic number is invalid");
  }

  [[nodiscard]] bool big_endian() const { return big_endian_; }
  [[nodiscard]] std::uint32_t first_ifd() const { return u32(4); }

  [[nodiscard]] std::uint16_t u16(const std::size_t offset) const {
    check(offset, 2U);
    return big_endian_ ? static_cast<std::uint16_t>((bytes_[offset] << 8U) | bytes_[offset + 1U])
                       : static_cast<std::uint16_t>((bytes_[offset + 1U] << 8U) | bytes_[offset]);
  }

  [[nodiscard]] std::uint32_t u32(const std::size_t offset) const {
    check(offset, 4U);
    const auto high = static_cast<std::uint32_t>(u16(big_endian_ ? offset : offset + 2U));
    const auto low = static_cast<std::uint32_t>(u16(big_endian_ ? offset + 2U : offset));
    return (high << 16U) | low;
  }

  [[nodiscard]] std::uint8_t u8(const std::size_t offset) const {
    check(offset, 1U);
    return bytes_[offset];
  }

  void check(const std::size_t offset, const std::size_t size) const {
    if (offset > bytes_.size() || size > bytes_.size() - offset) {
      throw std::runtime_error("EXIF value extends beyond its segment");
    }
  }

private:
  const Bytes &bytes_;
  bool big_endian_{true};
};

struct IfdEntry {
  std::uint16_t tag{};
  std::uint16_t type{};
  std::uint32_t count{};
  std::size_t value_offset{};  // where the value bytes start, inline or not
};

std::size_t type_size(const std::uint16_t type) {
  switch (type) {
  case 1U: case 2U: case 6U: case 7U: return 1U;
  case 3U: case 8U: return 2U;
  case 4U: case 9U: case 11U: return 4U;
  case 5U: case 10U: case 12U: return 8U;
  default: return 0U;
  }
}

std::vector<IfdEntry> read_ifd(const TiffReader &reader, const std::uint32_t offset) {
  const auto count = reader.u16(offset);
  if (count > kMaximumIfdEntries) throw std::runtime_error("EXIF IFD has too many entries");
  std::vector<IfdEntry> entries;
  for (std::size_t index = 0; index < count; ++index) {
    const auto base = static_cast<std::size_t>(offset) + 2U + index * 12U;
    IfdEntry entry{reader.u16(base), reader.u16(base + 2U), reader.u32(base + 4U), base + 8U};
    const auto size = type_size(entry.type);
    if (size == 0U) continue;
    const auto total = static_cast<std::uint64_t>(size) * entry.count;
    if (total > 4U) entry.value_offset = reader.u32(base + 8U);
    if (total > std::numeric_limits<std::uint32_t>::max()) continue;
    try {
      reader.check(entry.value_offset, static_cast<std::size_t>(total));
    } catch (const std::exception &) {
      continue;  // One damaged entry does not hide the others.
    }
    entries.push_back(entry);
  }
  return entries;
}

const IfdEntry *find(const std::vector<IfdEntry> &entries, const std::uint16_t tag) {
  const auto found = std::find_if(entries.begin(), entries.end(),
                                  [tag](const IfdEntry &entry) { return entry.tag == tag; });
  return found == entries.end() ? nullptr : &*found;
}

std::optional<std::string> ascii(const TiffReader &reader, const std::vector<IfdEntry> &entries,
                                 const std::uint16_t tag) {
  const auto *entry = find(entries, tag);
  if (entry == nullptr || entry->type != 2U) return std::nullopt;
  std::string text;
  for (std::uint32_t index = 0; index < entry->count; ++index) {
    const auto value = reader.u8(entry->value_offset + index);
    if (value == 0U) break;
    text.push_back(static_cast<char>(value));
  }
  while (!text.empty() && text.back() == ' ') text.pop_back();
  if (text.empty()) return std::nullopt;
  return text;
}

std::optional<std::uint32_t> unsigned_value(const TiffReader &reader, const std::vector<IfdEntry> &entries,
                                            const std::uint16_t tag, const std::uint32_t index = 0) {
  const auto *entry = find(entries, tag);
  if (entry == nullptr || index >= entry->count) return std::nullopt;
  switch (entry->type) {
  case 1U: case 7U: return reader.u8(entry->value_offset + index);
  case 3U: return reader.u16(entry->value_offset + index * 2U);
  case 4U: return reader.u32(entry->value_offset + index * 4U);
  default: return std::nullopt;
  }
}

std::optional<std::uint16_t> short_value(const TiffReader &reader, const std::vector<IfdEntry> &entries,
                                         const std::uint16_t tag) {
  const auto value = unsigned_value(reader, entries, tag);
  if (!value.has_value() || *value > std::numeric_limits<std::uint16_t>::max()) return std::nullopt;
  return static_cast<std::uint16_t>(*value);
}

std::optional<ExifRational> rational(const TiffReader &reader, const std::vector<IfdEntry> &entries,
                                     const std::uint16_t tag, const std::uint32_t index = 0) {
  const auto *entry = find(entries, tag);
  if (entry == nullptr || entry->type != 5U || index >= entry->count) return std::nullopt;
  const auto offset = entry->value_offset + index * 8U;
  ExifRational value{reader.u32(offset), reader.u32(offset + 4U)};
  if (value.denominator == 0U) return std::nullopt;
  return value;
}

std::optional<double> real(const TiffReader &reader, const std::vector<IfdEntry> &entries,
                           const std::uint16_t tag, const std::uint32_t index = 0) {
  const auto *entry = find(entries, tag);
  if (entry == nullptr) return std::nullopt;
  if (entry->type == 10U && index < entry->count) {
    const auto offset = entry->value_offset + index * 8U;
    const auto numerator = static_cast<std::int32_t>(reader.u32(offset));
    const auto denominator = static_cast<std::int32_t>(reader.u32(offset + 4U));
    if (denominator == 0) return std::nullopt;
    return static_cast<double>(numerator) / denominator;
  }
  const auto value = rational(reader, entries, tag, index);
  if (!value.has_value()) return std::nullopt;
  return static_cast<double>(value->numerator) / value->denominator;
}

std::optional<double> coordinate(const TiffReader &reader, const std::vector<IfdEntry> &entries,
                                 const std::uint16_t reference_tag, const std::uint16_t value_tag,
                                 const char negative) {
  const auto reference = ascii(reader, entries, reference_tag);
  const auto degrees = real(reader, entries, value_tag, 0);
  const auto minutes = real(reader, entries, value_tag, 1);
  const auto secs = real(reader, entries, value_tag, 2);
  if (!reference.has_value() || !degrees || !minutes || !secs) return std::nullopt;
  const auto value = *degrees + *minutes / 60.0 + *secs / 3600.0;
  return reference->front() == negative ? -value : value;
}

std::optional<PhotoGps> read_gps(const TiffReader &reader, const std::vector<IfdEntry> &entries) {
  const auto latitude = coordinate(reader, entries, 1U, 2U, 'S');
  const auto longitude = coordinate(reader, entries, 3U, 4U, 'W');
  if (!latitude.has_value() || !longitude.has_value() || std::abs(*latitude) > 90.0 ||
      std::abs(*longitude) > 180.0) {
    return std::nullopt;
  }
  PhotoGps gps;
  gps.latitude = *latitude;
  gps.longitude = *longitude;
  if (const auto altitude = real(reader, entries, 6U); altitude.has_value()) {
    gps.altitude = unsigned_value(reader, entries, 5U).value_or(0U) == 1U ? -*altitude : *altitude;
  }
  const auto date = ascii(reader, entries, 0x1dU);
  const auto hours = real(reader, entries, 7U, 0);
  const auto minutes = real(reader, entries, 7U, 1);
  const auto secs = real(reader, entries, 7U, 2);
  if (date.has_value() && date->size() == 10U && hours && minutes && secs) {
    std::array<char, 32> time{};
    std::snprintf(time.data(), time.size(), "T%02d:%02d:%02dZ", static_cast<int>(*hours),
                  static_cast<int>(*minutes), static_cast<int>(*secs));
    auto text = *date;
    std::replace(text.begin(), text.end(), ':', '-');
    gps.utc = text + time.data();
  }
  gps.dop = real(reader, entries, 0x0bU);
  if (const auto speed = real(reader, entries, 0x0dU); speed.has_value()) {
    const auto unit = ascii(reader, entries, 0x0cU).value_or("K");
    const auto factor = unit == "M" ? 1609.344 / 3600.0 : unit == "N" ? 1852.0 / 3600.0 : 1000.0 / 3600.0;
    gps.speed_mps = *speed * factor;
  }
  return gps;
}

std::optional<std::string> xmp_value(const std::string &xmp, const std::string &name) {
  const auto qualified = "GPano:" + name;
  // Attribute form: GPano:Name="value".
  for (auto position = xmp.find(qualified); position != std::string::npos;
       position = xmp.find(qualified, position + 1U)) {
    auto cursor = position + qualified.size();
    while (cursor < xmp.size() && (xmp[cursor] == ' ' || xmp[cursor] == '\t')) ++cursor;
    if (cursor < xmp.size() && xmp[cursor] == '=') {
      ++cursor;
      while (cursor < xmp.size() && (xmp[cursor] == ' ' || xmp[cursor] == '\t')) ++cursor;
      if (cursor >= xmp.size() || (xmp[cursor] != '"' && xmp[cursor] != '\'')) continue;
      const auto quote = xmp[cursor];
      const auto end = xmp.find(quote, cursor + 1U);
      if (end == std::string::npos) return std::nullopt;
      return xmp.substr(cursor + 1U, end - cursor - 1U);
    }
    // Element form: <GPano:Name>value</GPano:Name>.
    if (position > 0U && xmp[position - 1U] == '<' && cursor < xmp.size() && xmp[cursor] == '>') {
      const auto end = xmp.find("</" + qualified, cursor + 1U);
      if (end == std::string::npos) return std::nullopt;
      return xmp.substr(cursor + 1U, end - cursor - 1U);
    }
  }
  return std::nullopt;
}

std::optional<std::uint32_t> xmp_unsigned(const std::string &xmp, const std::string &name) {
  const auto text = xmp_value(xmp, name);
  if (!text.has_value() || text->empty() || text->size() > 9U) return std::nullopt;
  std::uint32_t value{};
  for (const auto character : *text) {
    if (character < '0' || character > '9') return std::nullopt;
    value = value * 10U + static_cast<std::uint32_t>(character - '0');
  }
  return value;
}

std::string trimmed(std::string text) {
  while (!text.empty() && (text.back() == '\0' || text.back() == ' ')) text.pop_back();
  return text;
}

const gb::metadata::GpmfElement *find_key(const std::vector<gb::metadata::GpmfElement> &elements,
                                          const std::string &key) {
  for (const auto &element : elements) {
    if (element.key == key) return &element;
    if (const auto *found = find_key(element.children, key); found != nullptr) return found;
  }
  return nullptr;
}

std::optional<std::string> gpmf_text(const std::vector<gb::metadata::GpmfElement> &elements,
                                     const std::string &key) {
  const auto *element = find_key(elements, key);
  if (element == nullptr || element->raw.empty()) return std::nullopt;
  // 'c' strings and 'F' FourCCs are raw characters; trailing NULs are padding.
  if (element->type != 'c' && element->type != 'F') return std::nullopt;
  auto text = trimmed(std::string(element->raw.begin(), element->raw.end()));
  if (const auto nul = text.find('\0'); nul != std::string::npos) text.resize(nul);
  if (text.empty()) return std::nullopt;
  return text;
}

std::optional<double> gpmf_float(const std::vector<gb::metadata::GpmfElement> &elements, const std::string &key) {
  const auto *element = find_key(elements, key);
  if (element == nullptr || element->type != 'f' || element->raw.size() < 4U) return std::nullopt;
  const auto bits = (static_cast<std::uint32_t>(element->raw[0]) << 24U) |
                    (static_cast<std::uint32_t>(element->raw[1]) << 16U) |
                    (static_cast<std::uint32_t>(element->raw[2]) << 8U) |
                    static_cast<std::uint32_t>(element->raw[3]);
  float value{};
  static_assert(sizeof(value) == sizeof(bits));
  std::memcpy(&value, &bits, sizeof(value));
  if (!std::isfinite(value)) return std::nullopt;
  return static_cast<double>(value);
}

GoProPhotoMetadata parse_gopro(const Bytes &payload) {
  GoProPhotoMetadata result;
  try {
    result.elements = gb::metadata::parse_gpmf(Bytes(payload.begin() + kGoProSignature.size(), payload.end()));
  } catch (const std::exception &error) {
    result.parse_error = error.what();
    return result;
  }
  result.projection = gpmf_text(result.elements, "PRJT");
  result.resolution = gpmf_text(result.elements, "PRES");
  result.field_of_view = gpmf_float(result.elements, "ZFOV");
  result.stabilization = gpmf_text(result.elements, "EISA");
  result.firmware = gpmf_text(result.elements, "FMWR");
  result.model = gpmf_text(result.elements, "MINF");
  return result;
}

} // namespace

ExifData parse_exif(const Bytes &tiff, std::optional<PhotoGps> &gps) {
  const TiffReader reader(tiff);
  ExifData exif;
  exif.byte_order = reader.big_endian() ? "big-endian" : "little-endian";
  const auto ifd0 = read_ifd(reader, reader.first_ifd());
  exif.make = ascii(reader, ifd0, 0x010fU);
  exif.model = ascii(reader, ifd0, 0x0110U);
  exif.software = ascii(reader, ifd0, 0x0131U);
  exif.orientation = short_value(reader, ifd0, 0x0112U);

  std::set<std::uint32_t> visited{reader.first_ifd()};
  if (const auto offset = unsigned_value(reader, ifd0, 0x8769U); offset.has_value() && visited.insert(*offset).second) {
    const auto entries = read_ifd(reader, *offset);
    exif.exposure_time = rational(reader, entries, 0x829aU);
    exif.f_number = rational(reader, entries, 0x829dU);
    exif.exposure_program = short_value(reader, entries, 0x8822U);
    exif.iso = unsigned_value(reader, entries, 0x8827U);
    exif.date_time_original = ascii(reader, entries, 0x9003U);
    exif.exposure_bias_ev = real(reader, entries, 0x9204U);
    exif.metering_mode = short_value(reader, entries, 0x9207U);
    exif.focal_length_mm = real(reader, entries, 0x920aU);
    exif.white_balance = short_value(reader, entries, 0xa403U);
    exif.digital_zoom_ratio = real(reader, entries, 0xa404U);
    exif.focal_length_35mm = short_value(reader, entries, 0xa405U);
    exif.sharpness = short_value(reader, entries, 0xa40aU);
  }
  gps.reset();
  if (const auto offset = unsigned_value(reader, ifd0, 0x8825U); offset.has_value() && visited.insert(*offset).second) {
    gps = read_gps(reader, read_ifd(reader, *offset));
  }
  return exif;
}

std::optional<Panorama> parse_gpano(const std::string &xmp) {
  Panorama panorama;
  const auto projection = xmp_value(xmp, "ProjectionType");
  const auto full_width = xmp_unsigned(xmp, "FullPanoWidthPixels");
  const auto full_height = xmp_unsigned(xmp, "FullPanoHeightPixels");
  const auto crop_width = xmp_unsigned(xmp, "CroppedAreaImageWidthPixels");
  const auto crop_height = xmp_unsigned(xmp, "CroppedAreaImageHeightPixels");
  if (!projection || !full_width || !full_height || !crop_width || !crop_height || *full_width == 0U ||
      *full_height == 0U) {
    return std::nullopt;
  }
  panorama.projection = *projection;
  panorama.full_width = *full_width;
  panorama.full_height = *full_height;
  panorama.crop_width = *crop_width;
  panorama.crop_height = *crop_height;
  panorama.crop_left = xmp_unsigned(xmp, "CroppedAreaLeftPixels").value_or(0U);
  panorama.crop_top = xmp_unsigned(xmp, "CroppedAreaTopPixels").value_or(0U);
  if (static_cast<std::uint64_t>(panorama.crop_left) + panorama.crop_width > panorama.full_width ||
      static_cast<std::uint64_t>(panorama.crop_top) + panorama.crop_height > panorama.full_height) {
    return std::nullopt;
  }
  panorama.source_photos = xmp_unsigned(xmp, "SourcePhotosCount");
  return panorama;
}

PhotoClassification classify_photo(const std::optional<Panorama> &panorama,
                                   const std::optional<GoProPhotoMetadata> &gopro) {
  PhotoClassification result;
  const auto projection = gopro && gopro->projection ? *gopro->projection : std::string();
  const auto resolution = gopro && gopro->resolution ? *gopro->resolution : std::string();
  const auto lens = resolution.size() > 2U && resolution[resolution.size() - 2U] == '_' ? resolution.back() : '\0';
  std::string gopro_evidence;
  if (!projection.empty()) gopro_evidence += "GoPro PRJT " + projection;
  if (!resolution.empty()) gopro_evidence += (gopro_evidence.empty() ? "GoPro " : ", ") + std::string("PRES ") + resolution;

  if (panorama.has_value() && panorama->projection == "equirectangular") {
    const auto horizontal = 360.0 * panorama->crop_width / panorama->full_width;
    result.horizontal_degrees = horizontal;
    const auto full = panorama->crop_width == panorama->full_width && panorama->crop_height == panorama->full_height;
    result.field_of_view = full ? PhotoFieldOfView::spherical : PhotoFieldOfView::superwide;
    result.evidence = "GPano equirectangular, crop " + std::to_string(panorama->crop_width) + "×" +
                      std::to_string(panorama->crop_height) + " of " + std::to_string(panorama->full_width) + "×" +
                      std::to_string(panorama->full_height);
    if (!gopro_evidence.empty()) result.evidence += "; " + gopro_evidence;
    return result;
  }
  if (projection == "ERP0" || lens == 'S') result.field_of_view = PhotoFieldOfView::spherical;
  else if (projection == "PANO" || lens == 'P') result.field_of_view = PhotoFieldOfView::superwide;
  else if (lens == 'W') result.field_of_view = PhotoFieldOfView::wide;
  else if (lens == 'L') result.field_of_view = PhotoFieldOfView::linear;
  else if (lens == 'N') result.field_of_view = PhotoFieldOfView::narrow;
  result.evidence = gopro_evidence.empty() ? "no projection metadata" : gopro_evidence;
  return result;
}

std::string field_of_view_name(const PhotoFieldOfView value) {
  switch (value) {
  case PhotoFieldOfView::spherical: return "360° spherical";
  case PhotoFieldOfView::superwide: return "superwide";
  case PhotoFieldOfView::wide: return "wide";
  case PhotoFieldOfView::linear: return "linear";
  case PhotoFieldOfView::narrow: return "narrow";
  case PhotoFieldOfView::unknown: return "unknown";
  }
  return "unknown";
}

bool is_jpeg_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  std::array<unsigned char, 3> signature{};
  input.read(reinterpret_cast<char *>(signature.data()), signature.size());
  return input && signature[0] == 0xffU && signature[1] == 0xd8U && signature[2] == 0xffU;
}

PhotoInspection inspect_photo(const std::filesystem::path &path) {
  PhotoInspection result;
  result.jpeg = inspect_jpeg(path);
  if (result.jpeg.images.empty()) throw JpegError(0, "no JPEG codestream");
  const auto &primary = result.jpeg.images.front();
  result.width = primary.width;
  result.height = primary.height;

  std::ifstream input(path, std::ios::binary);
  if (!input) throw JpegError(JpegErrorKind::io, 0, "cannot open input for reading");
  for (const auto &segment : primary.segments) {
    if (segment.content == "Exif" && !result.exif.has_value() && result.exif_error.empty()) {
      const auto payload = read_payload(input, segment);
      try {
        result.exif = parse_exif(Bytes(payload.begin() + kExifSignature.size(), payload.end()), result.gps);
      } catch (const std::exception &error) {
        result.exif_error = error.what();
      }
    } else if (segment.content == "XMP" && !result.panorama.has_value()) {
      const auto payload = read_payload(input, segment);
      result.panorama = parse_gpano(std::string(payload.begin() + kXmpSignature.size(), payload.end()));
    } else if (segment.marker == 0xe6U && !result.gopro.has_value()) {
      const auto payload = read_payload(input, segment);
      if (starts_with(payload, kGoProSignature)) result.gopro = parse_gopro(payload);
    }
  }
  result.classification = classify_photo(result.panorama, result.gopro);
  return result;
}

} // namespace gb::image
