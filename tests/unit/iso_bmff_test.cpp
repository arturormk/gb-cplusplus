#include "gb/container/iso_bmff.hpp"

#include <array>
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

void append_u32(Bytes &bytes, const std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_u64(Bytes &bytes, const std::uint64_t value) {
  append_u32(bytes, static_cast<std::uint32_t>(value >> 32U));
  append_u32(bytes, static_cast<std::uint32_t>(value));
}

void append_fourcc(Bytes &bytes, const std::string &value) {
  if (value.size() != 4U) {
    throw std::logic_error("test FourCC must contain four bytes");
  }
  bytes.insert(bytes.end(), value.begin(), value.end());
}

Bytes box(const std::string &type, const Bytes &payload) {
  Bytes result;
  append_u32(result, static_cast<std::uint32_t>(8U + payload.size()));
  append_fourcc(result, type);
  result.insert(result.end(), payload.begin(), payload.end());
  return result;
}

void append(Bytes &destination, const Bytes &source) {
  destination.insert(destination.end(), source.begin(), source.end());
}

class FixtureDirectory {
public:
  FixtureDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ =
        std::filesystem::temp_directory_path() / ("gb-container-tests-" + std::to_string(stamp));
    std::filesystem::create_directory(path_);
  }
  ~FixtureDirectory() { std::filesystem::remove_all(path_); }

  std::filesystem::path write(const std::string &name, const Bytes &bytes) const {
    const auto path = path_ / name;
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
      throw std::runtime_error("could not write test fixture");
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

template <typename Function>
void require_error(Function function, const gb::container::ErrorKind expected,
                   const std::string &message) {
  try {
    function();
  } catch (const gb::container::InspectionError &error) {
    require(error.kind() == expected, message + " (wrong error kind)");
    return;
  }
  throw std::runtime_error(message + " (no error)");
}

void test_valid_nested_fixture(const FixtureDirectory &fixtures) {
  Bytes ftyp_payload;
  append_fourcc(ftyp_payload, "isom");
  append_u32(ftyp_payload, 512U);
  append_fourcc(ftyp_payload, "isom");
  append_fourcc(ftyp_payload, "mp42");

  const auto free_box = box("free", {});
  const auto trak_box = box("trak", free_box);
  const auto moov_box = box("moov", trak_box);
  Bytes file = box("ftyp", ftyp_payload);
  append(file, moov_box);

  const auto result = gb::container::inspect_iso_bmff(fixtures.write("valid.mp4", file));
  require(result.file_size == file.size(), "valid fixture size mismatch");
  require(result.identity.major_brand == "isom", "major brand mismatch");
  require(result.identity.minor_version == 512U, "minor version mismatch");
  require(result.identity.compatible_brands.size() == 2U, "compatible brands mismatch");
  require(result.boxes.size() == 2U, "top-level box count mismatch");
  require(result.boxes[1].type == "moov", "moov box missing");
  require(result.boxes[1].children.at(0).type == "trak", "trak box missing");
  require(result.boxes[1].children.at(0).children.at(0).offset == 40U,
          "nested checked offset mismatch");
}

void test_truncated_fixture(const FixtureDirectory &fixtures) {
  Bytes file;
  append_u32(file, 20U);
  append_fourcc(file, "ftyp");
  append_fourcc(file, "isom");
  require_error(
      [&] { (void)gb::container::inspect_iso_bmff(fixtures.write("truncated.mp4", file)); },
      gb::container::ErrorKind::corrupt, "truncated box was accepted");
}

void test_malicious_small_extended_size(const FixtureDirectory &fixtures) {
  Bytes file;
  append_u32(file, 1U);
  append_fourcc(file, "free");
  append_u64(file, 8U);
  require_error(
      [&] { (void)gb::container::inspect_iso_bmff(fixtures.write("small-extended.mp4", file)); },
      gb::container::ErrorKind::corrupt, "extended box smaller than header was accepted");
}

void test_malicious_oversized_box(const FixtureDirectory &fixtures) {
  Bytes file;
  append_u32(file, 0xffffffffU);
  append_fourcc(file, "free");
  require_error(
      [&] { (void)gb::container::inspect_iso_bmff(fixtures.write("oversized.mp4", file)); },
      gb::container::ErrorKind::corrupt, "oversized box was accepted");
}

void test_size_zero_extends_to_parent(const FixtureDirectory &fixtures) {
  Bytes ftyp_payload;
  append_fourcc(ftyp_payload, "isom");
  append_u32(ftyp_payload, 0U);
  Bytes file = box("ftyp", ftyp_payload);
  append_u32(file, 0U);
  append_fourcc(file, "mdat");
  file.push_back(0xaaU);
  file.push_back(0xbbU);
  const auto result = gb::container::inspect_iso_bmff(fixtures.write("to-end.mp4", file));
  require(result.boxes.at(1).size == 10U, "size-zero box did not extend to end");
}

} // namespace

int main() {
  try {
    const FixtureDirectory fixtures;
    test_valid_nested_fixture(fixtures);
    test_truncated_fixture(fixtures);
    test_malicious_small_extended_size(fixtures);
    test_malicious_oversized_box(fixtures);
    test_size_zero_extends_to_parent(fixtures);
    std::cout << "all ISO BMFF tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
