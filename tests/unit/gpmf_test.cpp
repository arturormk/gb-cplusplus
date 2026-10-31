#include "gb/metadata/gpmf.hpp"

#include <cstdint>
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

void append_u32(Bytes &bytes, const std::uint32_t value) {
  append_u16(bytes, static_cast<std::uint16_t>(value >> 16U));
  append_u16(bytes, static_cast<std::uint16_t>(value));
}

void append(Bytes &destination, const Bytes &source) {
  destination.insert(destination.end(), source.begin(), source.end());
}

Bytes element(const std::string &key, const std::uint8_t type, const std::uint8_t structure_size,
              const std::uint16_t repeat, const Bytes &payload) {
  Bytes bytes(key.begin(), key.end());
  bytes.push_back(type);
  bytes.push_back(structure_size);
  append_u16(bytes, repeat);
  append(bytes, payload);
  while ((bytes.size() % 4U) != 0U) {
    bytes.push_back(0U);
  }
  return bytes;
}

void require(const bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_nested_scaling_and_unknown_preservation() {
  Bytes scale;
  append_u32(scale, 10U);
  append_u32(scale, 100U);
  Bytes data;
  append_u16(data, 20U);
  append_u16(data, 300U);

  Bytes stream;
  append(stream, element("SCAL", 'l', 4U, 2U, scale));
  append(stream, element("SIUN", 'c', 3U, 2U, {'m', '/', 's', 'r', 'a', 'd'}));
  append(stream, element("ZZZZ", 's', 4U, 1U, data));
  const auto bytes = element("STRM", 0U, 1U, static_cast<std::uint16_t>(stream.size()), stream);

  const auto result = gb::metadata::parse_gpmf(bytes);
  require(result.size() == 1U && result[0].children.size() == 3U, "nested records missing");
  const auto &unknown = result[0].children[2];
  require(!gb::metadata::is_known_key(unknown.key), "unknown key was classified as known");
  require(unknown.raw == data, "unknown raw bytes were not preserved");
  require(unknown.values.size() == 2U && unknown.values[0] == "20" && unknown.values[1] == "300",
          "raw values decoded incorrectly");
  require(unknown.normalized_values.size() == 2U && unknown.normalized_values[0] == "2" &&
              unknown.normalized_values[1] == "3",
          "scaled values decoded incorrectly");
  require(unknown.unit == "m/s,rad", "component units decoded incorrectly");
}

void test_truncated_record_rejected() {
  const Bytes bytes{'B', 'A', 'D', '!', 'L', 4U, 0U, 2U, 0U};
  try {
    (void)gb::metadata::parse_gpmf(bytes);
  } catch (const gb::metadata::GpmfError &) {
    return;
  }
  throw std::runtime_error("truncated GPMF record was accepted");
}

} // namespace

int main() {
  try {
    test_nested_scaling_and_unknown_preservation();
    test_truncated_record_rejected();
    std::cout << "all GPMF tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
