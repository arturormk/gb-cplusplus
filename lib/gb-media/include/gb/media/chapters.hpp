#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gb::media {

// A GoPro chaptered file name: GSnnmmmm.360 (MAX), or GHnnmmmm.MP4 (AVC) and GXnnmmmm.MP4 (HEVC)
// as documented in GoPro's "GoPro Camera File Naming Convention" support article. nn is the
// chapter (01 onwards) and mmmm the recording number shared by all chapters of one recording.
struct ChapterName {
  std::string prefix;    // "GS", "GH" or "GX", as written
  int chapter{};         // nn
  std::string recording; // mmmm
  std::string extension; // ".360" or ".MP4", as written
};

// Parses a file name (not a path). Letters match case-insensitively; GS goes with .360, GH and GX
// with .MP4. Returns nothing for any other name.
[[nodiscard]] std::optional<ChapterName> parse_chapter_name(const std::string &file_name);

// The chapters of the recording `path` belongs to: the regular files in its directory with the
// same prefix, recording number and extension, ordered by chapter. Missing chapters leave no
// placeholder. A name that is not chaptered, or a directory that cannot be listed, yields just
// `path` itself.
[[nodiscard]] std::vector<std::filesystem::path>
chapter_sequence(const std::filesystem::path &path);

} // namespace gb::media
