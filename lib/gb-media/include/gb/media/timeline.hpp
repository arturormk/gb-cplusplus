#pragma once

#include "gb/media/playback.hpp"
#include "gb/media/position.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gb::media {

// The chapters of one recording on one timeline (docs/plans/export/pipeline.md#chapter-sequences).
// Chapter n+1 starts where chapter n's video ends: its offset is the sum of the video durations of
// the chapters before it, as measured on a three-chapter MP4 recording
// (research/findings/2026-10-31-gopro-chapter-boundaries.md). Times are in the first chapter's
// video time base ("recording ticks").
struct TimelineChapter {
  std::filesystem::path path;
  int number{}; // 1-based, in chapter_sequence order
  std::int64_t offset_ticks{};
  std::int64_t duration_ticks{};
  PlaybackLayout layout;
  std::vector<VideoStream> streams;
};

struct RecordingTimeline {
  std::vector<TimelineChapter> chapters;
  Rational time_base;
  Rational frame_rate;
  std::int64_t duration_ticks{};

  [[nodiscard]] bool chaptered() const noexcept { return chapters.size() > 1U; }
  // The chapter holding a recording time; the last one for times at or past the end.
  [[nodiscard]] std::size_t chapter_at(std::int64_t ticks) const;
};

// Opens every chapter of the recording `path` belongs to. Throws MediaError when a chapter cannot
// be read or differs from the first in layout, picture size or time base, which would make one
// timeline across them a guess.
[[nodiscard]] RecordingTimeline open_timeline(const std::filesystem::path &path);

// A parsed position as recording ticks. Throws MediaError (invalid_argument) for a chapter that
// does not exist, a file name that is not one of the chapters, or a time past the end of its
// chapter or of the recording, naming the length.
[[nodiscard]] std::int64_t resolve_position(const RecordingTimeline &timeline,
                                            const Position &position);

// "c2:00:05:31.400" for a chaptered recording, the clock alone otherwise.
[[nodiscard]] std::string format_recording_position(const RecordingTimeline &timeline,
                                                    std::int64_t ticks);

} // namespace gb::media
