#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace gb::image {

struct JpegSegment {
  std::uint8_t marker{};
  std::string name;
  std::string content;
  std::uint64_t offset{};
  std::uint64_t size{};
};

struct JpegCodestream {
  std::uint64_t offset{};
  std::uint64_t size{};
  std::uint16_t width{};
  std::uint16_t height{};
  std::uint8_t precision{};
  std::uint8_t components{};
  bool complete{};
  std::vector<JpegSegment> segments;
};

struct JpegInspection {
  std::filesystem::path path;
  std::uint64_t file_size{};
  bool has_jfif{};
  bool has_exif{};
  bool has_xmp{};
  bool has_mpf{};
  bool has_app6{};
  std::uint64_t trailing_bytes{};
  std::vector<JpegCodestream> images;
};

enum class JpegErrorKind { io, corrupt };

class JpegError final : public std::runtime_error {
public:
  JpegError(std::uint64_t offset, const std::string &message);
  JpegError(JpegErrorKind kind, std::uint64_t offset, const std::string &message);
  [[nodiscard]] JpegErrorKind kind() const noexcept;
  [[nodiscard]] std::uint64_t offset() const noexcept;

private:
  JpegErrorKind kind_;
  std::uint64_t offset_;
};

[[nodiscard]] JpegInspection inspect_jpeg(const std::filesystem::path &path);
[[nodiscard]] std::string jpeg_marker_name(std::uint8_t marker);

} // namespace gb::image
