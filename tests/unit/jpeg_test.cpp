#include "gb/image/jpeg.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

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

Bytes make_jpeg(const std::uint16_t width, const std::uint16_t height,
                const bool include_metadata) {
  Bytes bytes{0xffU, 0xd8U};
  if (include_metadata) {
    append_segment(bytes, 0xe0U, {'J', 'F', 'I', 'F', 0U});
    append_segment(bytes, 0xe1U, {'E', 'x', 'i', 'f', 0U, 0U});
    append_segment(bytes, 0xe1U,
                   {'h', 't', 't', 'p', ':', '/', '/', 'n', 's', '.', 'a', 'd', 'o', 'b', 'e',
                    '.', 'c', 'o', 'm', '/', 'x', 'a', 'p', '/', '1', '.', '0', '/', 0U});
    append_segment(bytes, 0xe2U, {'M', 'P', 'F', 0U});
    append_segment(bytes, 0xe6U, {1U, 2U, 3U});
  }

  Bytes sof{8U, static_cast<std::uint8_t>(height >> 8U), static_cast<std::uint8_t>(height),
            static_cast<std::uint8_t>(width >> 8U), static_cast<std::uint8_t>(width), 3U,
            1U, 0x22U, 0U, 2U, 0x11U, 1U, 3U, 0x11U, 1U};
  append_segment(bytes, 0xc0U, sof);
  append_segment(bytes, 0xdaU, {3U, 1U, 0U, 2U, 0x11U, 3U, 0x11U, 0U, 63U, 0U});
  bytes.insert(bytes.end(), {0x11U, 0xffU, 0x00U, 0x22U, 0xffU, 0xd0U, 0x33U});
  bytes.insert(bytes.end(), {0xffU, 0xd9U});
  return bytes;
}

class FixtureDirectory {
public:
  FixtureDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("gb-image-tests-" + std::to_string(stamp));
    std::filesystem::create_directory(path_);
  }
  ~FixtureDirectory() { std::filesystem::remove_all(path_); }

  std::filesystem::path write(const std::string &name, const Bytes &bytes) const {
    const auto path = path_ / name;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
      throw std::runtime_error("could not write JPEG test fixture");
    }
    return path;
  }

private:
  std::filesystem::path path_;
};

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Function> void require_corrupt(Function function, const std::string &message) {
  try {
    function();
  } catch (const gb::image::JpegError &) {
    return;
  }
  throw std::runtime_error(message);
}

void test_multiple_images_and_metadata(const FixtureDirectory &fixtures) {
  auto primary = make_jpeg(5760U, 2880U, true);
  const auto primary_size = primary.size();
  const auto preview = make_jpeg(1440U, 720U, false);
  primary.insert(primary.end(), preview.begin(), preview.end());

  const auto result = gb::image::inspect_jpeg(fixtures.write("multi.jpg", primary));
  require(result.images.size() == 2U, "appended JPEG preview was not detected");
  require(result.images[0].width == 5760U && result.images[0].height == 2880U,
          "primary geometry mismatch");
  require(result.images[1].offset == primary_size, "preview offset mismatch");
  require(result.images[1].width == 1440U && result.images[1].height == 720U,
          "preview geometry mismatch");
  require(result.has_jfif && result.has_exif && result.has_xmp && result.has_mpf &&
              result.has_app6,
          "metadata signatures were not preserved");
  require(result.trailing_bytes == 0U, "valid JPEG reported trailing bytes");
}

void test_truncated_segment(const FixtureDirectory &fixtures) {
  const Bytes bytes{0xffU, 0xd8U, 0xffU, 0xe1U, 0x00U, 0x20U, 1U};
  require_corrupt([&] { (void)gb::image::inspect_jpeg(fixtures.write("truncated.jpg", bytes)); },
                  "truncated JPEG segment was accepted");
}

void test_malicious_short_length(const FixtureDirectory &fixtures) {
  const Bytes bytes{0xffU, 0xd8U, 0xffU, 0xe1U, 0x00U, 0x01U};
  require_corrupt([&] { (void)gb::image::inspect_jpeg(fixtures.write("short.jpg", bytes)); },
                  "JPEG segment smaller than its header was accepted");
}

void test_missing_eoi(const FixtureDirectory &fixtures) {
  auto bytes = make_jpeg(16U, 8U, false);
  bytes.resize(bytes.size() - 2U);
  require_corrupt([&] { (void)gb::image::inspect_jpeg(fixtures.write("no-eoi.jpg", bytes)); },
                  "JPEG without EOI was accepted");
}

} // namespace

int main() {
  try {
    const FixtureDirectory fixtures;
    test_multiple_images_and_metadata(fixtures);
    test_truncated_segment(fixtures);
    test_malicious_short_length(fixtures);
    test_missing_eoi(fixtures);
    std::cout << "all JPEG tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
