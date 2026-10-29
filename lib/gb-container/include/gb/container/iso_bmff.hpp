#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace gb::container {

struct Box {
  std::string type;
  std::uint64_t offset{};
  std::uint64_t size{};
  std::uint64_t header_size{};
  std::vector<Box> children;
};

struct FileIdentity {
  bool is_iso_bmff{};
  std::optional<std::string> major_brand;
  std::optional<std::uint32_t> minor_version;
  std::vector<std::string> compatible_brands;
};

struct Inspection {
  std::filesystem::path path;
  std::uint64_t file_size{};
  FileIdentity identity;
  std::vector<Box> boxes;
};

enum class ErrorKind { missing, io, unsupported, corrupt };

class InspectionError final : public std::runtime_error {
public:
  InspectionError(ErrorKind kind, std::uint64_t offset, const std::string &message);

  [[nodiscard]] ErrorKind kind() const noexcept;
  [[nodiscard]] std::uint64_t offset() const noexcept;

private:
  ErrorKind kind_;
  std::uint64_t offset_;
};

[[nodiscard]] Inspection inspect_iso_bmff(const std::filesystem::path &path);
[[nodiscard]] std::string error_kind_name(ErrorKind kind);

} // namespace gb::container
