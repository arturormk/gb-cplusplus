#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace gb::metadata {

struct GpmfElement {
  std::string key;
  std::uint8_t type{};
  std::uint8_t structure_size{};
  std::uint16_t repeat{};
  std::uint64_t offset{};
  std::vector<std::uint8_t> raw;
  std::vector<std::string> values;
  std::vector<std::string> normalized_values;
  std::string unit;
  std::vector<GpmfElement> children;
};

class GpmfError final : public std::runtime_error {
public:
  GpmfError(std::uint64_t offset, const std::string &message);
  [[nodiscard]] std::uint64_t offset() const noexcept;

private:
  std::uint64_t offset_;
};

[[nodiscard]] std::vector<GpmfElement> parse_gpmf(const std::vector<std::uint8_t> &bytes);
[[nodiscard]] std::string type_name(std::uint8_t type);
[[nodiscard]] bool is_known_key(const std::string &key);

} // namespace gb::metadata
