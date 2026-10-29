#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void append_u32(Bytes &bytes, const std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_u16(Bytes &bytes, const std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_fourcc(Bytes &bytes, const std::string &value) {
  bytes.insert(bytes.end(), value.begin(), value.end());
}

void append(Bytes &destination, const Bytes &source) {
  destination.insert(destination.end(), source.begin(), source.end());
}

Bytes box(const std::string &type, const Bytes &payload) {
  Bytes bytes;
  append_u32(bytes, static_cast<std::uint32_t>(payload.size() + 8U));
  append_fourcc(bytes, type);
  append(bytes, payload);
  return bytes;
}

Bytes gpmf(const std::string &key, const std::uint8_t type, const std::uint8_t structure_size,
           const std::uint16_t repeat, const Bytes &payload) {
  Bytes bytes;
  append_fourcc(bytes, key);
  bytes.push_back(type);
  bytes.push_back(structure_size);
  append_u16(bytes, repeat);
  append(bytes, payload);
  while ((bytes.size() % 4U) != 0U) {
    bytes.push_back(0U);
  }
  return bytes;
}

Bytes metadata_payload(const bool include_gps = true, const std::uint32_t gps_fix_value = 3U,
                       const std::uint16_t gps_records = 1U) {
  Bytes stream;
  append(stream, gpmf("STNM", 'c', 1U, 5U, {'A', 'c', 'c', 'e', 'l'}));
  Bytes scales;
  append_u32(scales, 100U);
  append_u32(scales, 100U);
  append_u32(scales, 100U);
  append(stream, gpmf("SCAL", 'l', 4U, 3U, scales));
  append(stream, gpmf("SIUN", 'c', 3U, 3U, {'m', '/', 's', 'm', '/', 's', 'm', '/', 's'}));
  Bytes acceleration;
  for (const std::uint16_t value :
       std::vector<std::uint16_t>{100U, 200U, 300U, 400U, 500U, 600U}) {
    append_u16(acceleration, value);
  }
  append(stream, gpmf("ACCL", 's', 6U, 2U, acceleration));
  append(stream, gpmf("ZZZZ", 'B', 1U, 2U, {0xaaU, 0xbbU}));

  Bytes gps_stream;
  append(gps_stream, gpmf("STNM", 'c', 1U, 3U, {'G', 'P', 'S'}));
  Bytes gps_scales;
  for (const std::uint32_t value :
       std::vector<std::uint32_t>{10'000'000U, 10'000'000U, 1000U, 1000U, 100U}) {
    append_u32(gps_scales, value);
  }
  append(gps_stream, gpmf("SCAL", 'l', 4U, 5U, gps_scales));
  append(gps_stream,
         gpmf("UNIT", 'c', 3U, 5U,
              {'d', 'e', 'g', 'd', 'e', 'g', 'm', 0U, 0U, 'm', '/', 's', 'm', '/', 's'}));
  append(gps_stream,
         gpmf("GPSU", 'U', 16U, 1U,
              {'2', '6', '0', '1', '0', '1', '1', '2', '0', '0', '0', '0', '.', '0', '0', '0'}));
  Bytes gps_fix;
  append_u32(gps_fix, gps_fix_value);
  append(gps_stream, gpmf("GPSF", 'L', 4U, 1U, gps_fix));
  Bytes gps_precision;
  append_u16(gps_precision, 189U);
  append(gps_stream, gpmf("GPSP", 'S', 2U, 1U, gps_precision));
  Bytes gps;
  for (std::uint16_t record = 0; record < gps_records; ++record) {
    append_u32(gps, 401'234'567U + record);
    append_u32(gps, static_cast<std::uint32_t>(-31'234'567 + record));
    append_u32(gps, 721'304U);
    append_u32(gps, 2'184U);
    append_u32(gps, 230U);
  }
  append(gps_stream, gpmf("GPS5", 'l', 20U, gps_records, gps));

  Bytes device;
  Bytes id;
  append_u32(id, 1U);
  append(device, gpmf("DVID", 'L', 4U, 1U, id));
  append(device, gpmf("STRM", 0U, 1U, static_cast<std::uint16_t>(stream.size()), stream));
  if (include_gps) {
    append(device,
           gpmf("STRM", 0U, 1U, static_cast<std::uint16_t>(gps_stream.size()), gps_stream));
  }
  return gpmf("DEVC", 0U, 1U, static_cast<std::uint16_t>(device.size()), device);
}

Bytes metadata_mp4(const bool include_gps = true, const std::uint32_t gps_fix = 3U,
                   const std::uint16_t gps_records = 1U,
                   const std::int32_t composition_offset = 0) {
  Bytes file;
  Bytes ftyp;
  append_fourcc(ftyp, "isom");
  append_u32(ftyp, 0U);
  append_fourcc(ftyp, "isom");
  append(file, box("ftyp", ftyp));

  const auto payload = metadata_payload(include_gps, gps_fix, gps_records);
  append(file, box("mdat", payload));
  const auto sample_offset = 28U;

  Bytes tkhd(84U, 0U);
  tkhd[15] = 1U;

  Bytes mdhd(24U, 0U);
  mdhd[14] = 3U;
  mdhd[15] = 0xe8U;
  mdhd[18] = 3U;
  mdhd[19] = 0xe8U;

  Bytes hdlr(24U, 0U);
  hdlr[8] = 'm';
  hdlr[9] = 'e';
  hdlr[10] = 't';
  hdlr[11] = 'a';

  Bytes entry;
  append_u32(entry, 16U);
  append_fourcc(entry, "gpmd");
  entry.resize(16U, 0U);
  Bytes stsd(4U, 0U);
  append_u32(stsd, 1U);
  append(stsd, entry);

  Bytes stts(4U, 0U);
  append_u32(stts, 1U);
  append_u32(stts, 1U);
  append_u32(stts, 1000U);

  Bytes ctts(4U, 0U);
  append_u32(ctts, 1U);
  append_u32(ctts, 1U);
  append_u32(ctts, static_cast<std::uint32_t>(composition_offset));

  Bytes stsc(4U, 0U);
  append_u32(stsc, 1U);
  append_u32(stsc, 1U);
  append_u32(stsc, 1U);
  append_u32(stsc, 1U);

  Bytes stsz(4U, 0U);
  append_u32(stsz, 0U);
  append_u32(stsz, 1U);
  append_u32(stsz, static_cast<std::uint32_t>(payload.size()));

  Bytes stco(4U, 0U);
  append_u32(stco, 1U);
  append_u32(stco, sample_offset);

  Bytes stbl;
  append(stbl, box("stsd", stsd));
  append(stbl, box("stts", stts));
  if (composition_offset != 0) {
    append(stbl, box("ctts", ctts));
  }
  append(stbl, box("stsc", stsc));
  append(stbl, box("stsz", stsz));
  append(stbl, box("stco", stco));
  Bytes minf = box("stbl", stbl);
  Bytes mdia;
  append(mdia, box("mdhd", mdhd));
  append(mdia, box("hdlr", hdlr));
  append(mdia, box("minf", minf));
  Bytes trak;
  append(trak, box("tkhd", tkhd));
  append(trak, box("mdia", mdia));
  append(file, box("moov", box("trak", trak)));
  return file;
}

void write(const std::filesystem::path &path, const Bytes &bytes) {
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    throw std::runtime_error("cannot write fixture: " + path.string());
  }
}

void append_jpeg_segment(Bytes &bytes, const std::uint8_t marker, const Bytes &payload) {
  bytes.push_back(0xffU);
  bytes.push_back(marker);
  append_u16(bytes, static_cast<std::uint16_t>(payload.size() + 2U));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
}

Bytes minimal_jpeg() {
  Bytes bytes{0xffU, 0xd8U};
  const Bytes sof{8U, 0U, 1U, 0U, 1U, 1U, 1U, 0x11U, 0U};
  append_jpeg_segment(bytes, 0xc0U, sof);
  const Bytes sos{1U, 1U, 0U, 0U, 63U, 0U};
  append_jpeg_segment(bytes, 0xdaU, sos);
  bytes.push_back(0x2aU);
  bytes.push_back(0xffU);
  bytes.push_back(0xd9U);
  return bytes;
}

} // namespace

int main(const int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: gb_test_fixture_writer <directory>\n";
    return 2;
  }
  try {
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);

    Bytes valid;
    append_u32(valid, 20U);
    append_fourcc(valid, "ftyp");
    append_fourcc(valid, "isom");
    append_u32(valid, 0U);
    append_fourcc(valid, "isom");
    write(directory / "valid.mp4", valid);

    Bytes corrupt;
    append_u32(corrupt, 64U);
    append_fourcc(corrupt, "ftyp");
    write(directory / "corrupt.mp4", corrupt);

    write(directory / "valid-image.bin", minimal_jpeg());
    write(directory / "corrupt-image.bin", {0xffU, 0xd8U, 0xffU, 0xe1U, 0xffU, 0xffU});
    write(directory / "metadata.mp4", metadata_mp4());
    write(directory / "metadata-no-gps.mp4", metadata_mp4(false));
    write(directory / "metadata-invalid-gps.mp4", metadata_mp4(true, 0U));
    write(directory / "metadata-jitter.mp4", metadata_mp4(true, 3U, 3U));
    write(directory / "metadata-ctts.mp4", metadata_mp4(true, 3U, 1U, 100));
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
