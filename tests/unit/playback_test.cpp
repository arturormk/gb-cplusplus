#include "gb/media/audio.hpp"
#include "gb/media/playback.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

gb::media::VideoStream stream(const int index, const std::int32_t width,
                              const std::int32_t height) {
  gb::media::VideoStream result;
  result.index = index;
  result.width = width;
  result.height = height;
  result.time_base = {1, 90000};
  return result;
}

template <typename Function> bool rejects(Function function, const gb::media::ErrorKind kind) {
  try {
    function();
  } catch (const gb::media::MediaError &error) {
    return error.kind() == kind;
  }
  return false;
}

int mean_plane(const gb::media::PlanarPicture &picture, const std::size_t plane) {
  const auto width = plane == 0U ? picture.width : (picture.width + 1) / 2;
  const auto height = plane == 0U ? picture.height : (picture.height + 1) / 2;
  long total{};
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      total += picture.planes[plane][static_cast<std::size_t>(y) *
                                         static_cast<std::size_t>(picture.strides[plane]) +
                                     static_cast<std::size_t>(x)];
    }
  }
  return static_cast<int>(total / (static_cast<long>(width) * height));
}

void layout_tests() {
  using gb::media::SourceLayout;
  const auto max =
      gb::media::select_playback_layout({stream(0, 4096, 1344), stream(5, 4096, 1344)});
  require(max.layout == SourceLayout::max_dual_eac && max.streams == std::vector<int>{0, 5} &&
              max.overlap == 64,
          "verified MAX geometry was not selected as dual EAC");
  const auto single = gb::media::select_playback_layout({stream(0, 1920, 1080)});
  require(single.layout == SourceLayout::flat && single.streams == std::vector<int>{0},
          "single stream is not flat");
  const auto odd =
      gb::media::select_playback_layout({stream(0, 4097, 1344), stream(1, 4097, 1344)});
  require(odd.layout == SourceLayout::flat && odd.streams == std::vector<int>{0},
          "odd overlap must not be claimed as MAX EAC");
  const auto mismatch =
      gb::media::select_playback_layout({stream(0, 4096, 1344), stream(1, 2048, 1344)});
  require(mismatch.layout == SourceLayout::flat, "mismatched streams must not be paired");
  require(rejects([] { static_cast<void>(gb::media::select_playback_layout({})); },
                  gb::media::ErrorKind::unsupported),
          "no-video input was not rejected");
  require(gb::media::source_layout_name(SourceLayout::max_dual_eac) == "max_dual_eac",
          "layout name changed");

  require(gb::media::rescale_ticks(3600, {1, 90000}, {1, 1000}) == 40, "rescale is wrong");
  require(gb::media::rescale_ticks(3599, {1, 90000}, {1, 1000}) == 39, "rescale does not floor");
  require(gb::media::rescale_ticks(-1, {1, 90000}, {1, 1000}) == -1,
          "negative rescale does not floor");
  require(gb::media::rescale_ticks(482560, {1, 1000}, {1, 90000}) == 43430400, "upscale is wrong");
  require(rejects(
              [] {
                static_cast<void>(gb::media::rescale_ticks(1, {0, 1}, {1, 1}));
              },
              gb::media::ErrorKind::invalid_argument),
          "invalid time base was not rejected");
  require(rejects(
              [] {
                static_cast<void>(gb::media::rescale_ticks(INT64_MAX, {1, 1}, {1, 90000}));
              },
              gb::media::ErrorKind::invalid_argument),
          "overflow was not rejected");
}

void reader_tests(const char *path) {
  const auto streams = gb::media::video_streams(path);
  const auto layout = gb::media::select_playback_layout(streams);
  require(layout.layout == gb::media::SourceLayout::max_dual_eac && layout.overlap == 8,
          "fixture layout was not recognized");
  gb::media::SynchronizedReader reader(path, layout.streams, 2);
  require(reader.streams().size() == 2U && reader.duration_ticks() > 0, "reader metadata missing");
  const auto base = reader.time_base();
  const auto tenth = gb::media::rescale_ticks(1, {1, 10}, base);

  std::vector<std::int64_t> sequence;
  while (auto set = reader.next()) {
    require(set->pictures.size() == 2U, "frame set does not contain both streams");
    for (const auto &picture : set->pictures) {
      require(picture.width == 104 && picture.height == 32, "unexpected decoded geometry");
      require(picture.strides[0] >= 104 && picture.strides[1] >= 52 && picture.strides[2] >= 52,
              "unexpected plane strides");
      require(picture.pts == set->pts && picture.storage != nullptr, "pictures are not paired");
    }
    require(set->duration == tenth, "frame duration is not one frame");
    sequence.push_back(set->pts);
    if (sequence.size() == 1U) {
      // Red then blue: verifies stream order and limited-range BT.601 planes.
      const auto &red = set->pictures[0];
      const auto &blue = set->pictures[1];
      require(std::abs(mean_plane(red, 0) - 81) <= 4 && mean_plane(red, 2) > 220,
              "first stream is not red");
      require(std::abs(mean_plane(blue, 0) - 41) <= 4 && mean_plane(blue, 1) > 220,
              "second stream is not blue");
      require(!red.full_range, "fixture should be limited range");
    }
  }
  require(sequence.size() == 20U, "expected 20 synchronized frame sets");
  for (std::size_t index = 0; index < sequence.size(); ++index) {
    require(sequence[index] == static_cast<std::int64_t>(index) * tenth,
            "PTS sequence is not exact");
  }
  require(reader.unpaired_frames() == 0U, "unexpected unpaired frames");

  // Mid-frame target inside a GOP: latest frame not after the target.
  reader.seek(13 * tenth + tenth / 2);
  auto selected = reader.next();
  require(selected.has_value() && selected->pts == 13 * tenth,
          "accurate seek selected wrong frame");
  auto following = reader.next();
  require(following.has_value() && following->pts == 14 * tenth, "seek lost the following frame");

  reader.seek(7 * tenth);
  selected = reader.next();
  require(selected.has_value() && selected->pts == 7 * tenth, "exact seek selected wrong frame");

  reader.seek(0);
  selected = reader.next();
  require(selected.has_value() && selected->pts == 0, "seek to start failed");

  reader.seek(1000 * tenth);
  selected = reader.next();
  require(selected.has_value() && selected->pts == 19 * tenth,
          "seek beyond end must hold last frame");
  require(!reader.next().has_value(), "reader did not end after the last frame");

  require(rejects([&reader] { reader.seek(-1); }, gb::media::ErrorKind::invalid_argument),
          "negative seek was not rejected");
  require(rejects(
              [path] {
                gb::media::SynchronizedReader invalid(path, {0, 0});
              },
              gb::media::ErrorKind::invalid_argument),
          "duplicate stream selection was not rejected");
  require(rejects([path] { gb::media::SynchronizedReader invalid(path, {7}); },
                  gb::media::ErrorKind::invalid_argument),
          "missing stream was not rejected");
}

gb::media::AudioStream audio(const int index, const std::int32_t channels) {
  gb::media::AudioStream result;
  result.index = index;
  result.codec = "aac";
  result.sample_rate = 48000;
  result.channels = channels;
  return result;
}

void audio_selection_tests() {
  // MAX: stereo AAC is played; the four-channel ambisonic PCM track is reported, not downmixed.
  const auto max = gb::media::select_playback_audio({audio(1, 2), audio(6, 4)});
  require(max.stream == 1 && max.description.find("not played: stream 6") != std::string::npos,
          "MAX stereo track was not selected");
  require(!gb::media::select_playback_audio({audio(6, 4)}).stream.has_value(),
          "multichannel audio must not be selected");
  require(gb::media::select_playback_audio({audio(3, 1)}).stream == 3,
          "mono stream was not selected");
  require(!gb::media::select_playback_audio({}).stream.has_value(),
          "no-audio input selected a stream");
}

void audio_aac_tests(const char *path) {
  const auto selection = gb::media::select_playback_audio(gb::media::audio_streams(path));
  require(selection.stream == 2, "fixture AAC stream was not selected");
  gb::media::AudioReader reader(path, *selection.stream);
  require(reader.sample_rate() == 48000, "unexpected AAC sample rate");
  std::int64_t expected{};
  std::int64_t total{};
  float peak{};
  while (auto block = reader.next()) {
    require(block->first_sample == expected, "AAC blocks are not contiguous");
    expected += block->frames();
    total += block->frames();
    for (std::size_t index = 0; index < block->samples.size(); index += 2U) {
      require(block->samples[index] == block->samples[index + 1U], "mono fixture channels differ");
      peak = std::max(peak, std::abs(block->samples[index]));
    }
  }
  require(std::llabs(total - 96000) <= 1024, "AAC duration is not two seconds");
  require(peak > 0.05F && peak <= 1.0F, "AAC signal level is implausible");

  reader.seek(30000);
  const auto block = reader.next();
  require(block.has_value() && block->first_sample == 30000, "AAC accurate seek is not exact");
}

void audio_pcm_tests(const char *path) {
  const auto streams = gb::media::audio_streams(path);
  require(streams.size() == 1U && streams.front().channels == 1, "ramp fixture is not mono");
  gb::media::AudioReader reader(path, streams.front().index);
  const auto value_at = [](const std::int64_t sample) {
    return static_cast<float>((sample % 1000) - 500) / 1000.0F;
  };
  const auto check = [&](const gb::media::AudioBlock &block) {
    for (std::int64_t frame = 0; frame < block.frames(); ++frame) {
      const auto left = block.samples[static_cast<std::size_t>(frame) * 2U];
      require(std::abs(left - value_at(block.first_sample + frame)) < 1.0e-3F,
              "PCM sample does not match its position");
      require(left == block.samples[static_cast<std::size_t>(frame) * 2U + 1U],
              "mono was not duplicated");
    }
  };
  std::int64_t total{};
  while (auto block = reader.next()) {
    require(block->first_sample == total, "PCM blocks are not contiguous");
    check(*block);
    total += block->frames();
  }
  require(total == 96000, "PCM fixture length is wrong");

  for (const std::int64_t target : {12345LL, 0LL, 95999LL, 47999LL}) {
    reader.seek(target);
    const auto block = reader.next();
    require(block.has_value() && block->first_sample == target, "PCM accurate seek is not exact");
    check(*block);
  }
  reader.seek(200000);
  require(!reader.next().has_value(), "seek beyond the end must end the stream");
  require(rejects([&reader] { reader.seek(-1); }, gb::media::ErrorKind::invalid_argument),
          "negative audio seek was not rejected");
  require(rejects([path] { gb::media::AudioReader invalid(path, 5); },
                  gb::media::ErrorKind::invalid_argument),
          "missing audio stream was not rejected");
}

} // namespace

int main(const int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: gb_playback_tests <two-stream fixture with audio> <mono PCM ramp>\n";
    return 2;
  }
  try {
    layout_tests();
    reader_tests(argv[1]);
    audio_selection_tests();
    audio_aac_tests(argv[1]);
    audio_pcm_tests(argv[2]);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "playback test failure: " << error.what() << '\n';
    return 1;
  }
}
