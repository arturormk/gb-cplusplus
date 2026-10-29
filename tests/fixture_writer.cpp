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
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
