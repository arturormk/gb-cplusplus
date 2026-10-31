#include "gb/media/timeline.hpp"

#include "gb/media/chapters.hpp"

#include <algorithm>
#include <cmath>

namespace gb::media {
namespace {

long double seconds(const std::int64_t ticks, const Rational base) {
  return static_cast<long double>(ticks) * base.numerator / base.denominator;
}

std::int64_t ticks_for_milliseconds(const std::int64_t milliseconds, const Rational base) {
  return rescale_ticks(milliseconds, {1, 1000}, base);
}

} // namespace

std::size_t RecordingTimeline::chapter_at(const std::int64_t ticks) const {
  for (std::size_t index = chapters.size(); index > 0U; --index) {
    if (ticks >= chapters[index - 1U].offset_ticks)
      return index - 1U;
  }
  return 0;
}

RecordingTimeline open_timeline(const std::filesystem::path &path) {
  RecordingTimeline timeline;
  int number = 0;
  for (const auto &chapter_path : chapter_sequence(path)) {
    ++number;
    TimelineChapter chapter;
    chapter.path = chapter_path;
    chapter.number = number;
    chapter.streams = video_streams(chapter_path);
    if (chapter.streams.empty())
      throw MediaError(ErrorKind::unsupported, chapter_path.filename().string() + " has no video");
    chapter.layout = select_playback_layout(chapter.streams);
    const auto &first = chapter.streams[static_cast<std::size_t>(chapter.layout.streams.front())];
    if (timeline.chapters.empty()) {
      timeline.time_base = first.time_base;
      timeline.frame_rate = first.average_frame_rate;
    } else {
      const auto &reference = timeline.chapters.front();
      const auto &reference_stream =
          reference.streams[static_cast<std::size_t>(reference.layout.streams.front())];
      if (chapter.layout.layout != reference.layout.layout ||
          chapter.layout.streams != reference.layout.streams ||
          first.width != reference_stream.width || first.height != reference_stream.height ||
          first.time_base.numerator != timeline.time_base.numerator ||
          first.time_base.denominator != timeline.time_base.denominator) {
        throw MediaError(ErrorKind::unsupported,
                         chapter_path.filename().string() +
                             " differs from the first chapter in layout, size or time base; the "
                             "chapters cannot be joined");
      }
    }
    chapter.duration_ticks = first.duration;
    if (chapter.duration_ticks <= 0) {
      SynchronizedReader reader(chapter_path, chapter.layout.streams, 1);
      chapter.duration_ticks = reader.duration_ticks();
    }
    if (chapter.duration_ticks <= 0)
      throw MediaError(ErrorKind::unsupported,
                       chapter_path.filename().string() + " has no known duration");
    chapter.offset_ticks = timeline.duration_ticks;
    timeline.duration_ticks += chapter.duration_ticks;
    timeline.chapters.push_back(std::move(chapter));
  }
  if (timeline.frame_rate.numerator <= 0 || timeline.frame_rate.denominator <= 0)
    throw MediaError(ErrorKind::unsupported, "the video has no known frame rate");
  return timeline;
}

std::int64_t resolve_position(const RecordingTimeline &timeline, const Position &position) {
  const auto base = timeline.time_base;
  const auto ticks = ticks_for_milliseconds(position.milliseconds, base);
  std::optional<std::size_t> index;
  if (position.chapter) {
    if (*position.chapter < 1 ||
        static_cast<std::size_t>(*position.chapter) > timeline.chapters.size()) {
      throw MediaError(ErrorKind::invalid_argument,
                       "chapter " + std::to_string(*position.chapter) +
                           " does not exist; the recording has " +
                           std::to_string(timeline.chapters.size()) +
                           (timeline.chapters.size() == 1U ? " chapter" : " chapters"));
    }
    index = static_cast<std::size_t>(*position.chapter - 1);
  } else if (position.file) {
    for (std::size_t candidate = 0; candidate < timeline.chapters.size(); ++candidate) {
      if (timeline.chapters[candidate].path.filename().string() == *position.file)
        index = candidate;
    }
    if (!index)
      throw MediaError(ErrorKind::invalid_argument,
                       *position.file + " is not a chapter of this recording");
  }
  if (index) {
    const auto &chapter = timeline.chapters[*index];
    if (ticks > chapter.duration_ticks) {
      throw MediaError(ErrorKind::invalid_argument,
                       format_position(chapter.number, position.milliseconds) +
                           " is past the end of chapter " + std::to_string(chapter.number) +
                           ", which is " + format_clock(seconds(chapter.duration_ticks, base)) +
                           " long");
    }
    return chapter.offset_ticks + ticks;
  }
  if (ticks > timeline.duration_ticks) {
    throw MediaError(ErrorKind::invalid_argument,
                     format_clock_milliseconds(position.milliseconds) +
                         " is past the end of the recording, which is " +
                         format_clock(seconds(timeline.duration_ticks, base)) + " long");
  }
  return ticks;
}

std::string format_recording_position(const RecordingTimeline &timeline, const std::int64_t ticks) {
  const auto index = timeline.chapter_at(ticks);
  const auto &chapter = timeline.chapters[index];
  const auto local = seconds(ticks - chapter.offset_ticks, timeline.time_base);
  const auto milliseconds = static_cast<std::int64_t>(std::floor(std::max(0.0L, local) * 1000.0L));
  return format_position(timeline.chaptered() ? std::optional<int>(chapter.number) : std::nullopt,
                         milliseconds);
}

} // namespace gb::media
