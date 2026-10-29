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

struct Rational {
  std::uint64_t numerator{1};
  std::uint64_t denominator{1};
};

struct SampleReference {
  std::uint64_t offset{};
  std::uint32_t size{};
  std::uint64_t decode_time{};
  std::int64_t presentation_time{};
  std::uint32_t duration{};
};

struct Track {
  std::uint32_t id{};
  std::string handler_type;
  std::string handler_name;
  std::string codec;
  Rational time_base;
  std::uint64_t duration_ticks{};
  std::uint32_t sample_count{};
  std::uint16_t width{};
  std::uint16_t height{};
  std::uint16_t channel_count{};
  std::uint16_t bits_per_sample{};
  std::uint32_t sample_rate{};
  std::vector<SampleReference> samples;
};

struct Inspection {
  std::filesystem::path path;
  std::uint64_t file_size{};
  FileIdentity identity;
  std::vector<Box> boxes;
  std::vector<Track> tracks;
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
[[nodiscard]] std::vector<std::uint8_t> read_sample(const std::filesystem::path &path,
                                                    const SampleReference &sample,
                                                    std::uint32_t maximum_size = 16U * 1024U * 1024U);
[[nodiscard]] std::string error_kind_name(ErrorKind kind);

} // namespace gb::container
