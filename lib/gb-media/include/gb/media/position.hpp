#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace gb::media {

// A position in a recording, as written on the command line, in settings files and on the
// player's clock (docs/plans/export/cli.md): "c2:00:05:31.400" is chapter 2 (1-based, in
// chapter_sequence order) at 5 min 31.4 s into that chapter's file. The chapter's file name works
// too ("GH020073.MP4:00:05:31.400"), and a time without either is recording time.
struct Position {
  std::optional<int> chapter;      // 1-based
  std::optional<std::string> file; // a chapter file name, as written
  std::int64_t milliseconds{};     // within the chapter, or recording time without one
};

// Parses a position. The time is HH:MM:SS.mmm or a shorter form (M:S.f or seconds alone); when a
// larger field is present, minutes and seconds must be below 60. Times are rounded to the
// millisecond. Returns nothing for anything else, including negative or non-finite times.
[[nodiscard]] std::optional<Position> parse_position(const std::string &text);

// "HH:MM:SS.mmm", floored to the millisecond, as gb-player's clock shows it. Negative or
// non-finite input gives "--:--:--.---".
[[nodiscard]] std::string format_clock(long double seconds);
[[nodiscard]] std::string format_clock_milliseconds(std::int64_t milliseconds);

// "c2:00:05:31.400" with a chapter, or the clock alone without one.
[[nodiscard]] std::string format_position(std::optional<int> chapter, std::int64_t milliseconds);

// The form used in file names: "c2-05m31s400", with "1h" in front when there are hours, and no
// "cN-" without a chapter.
[[nodiscard]] std::string format_position_for_file(std::optional<int> chapter,
                                                   std::int64_t milliseconds);

} // namespace gb::media
