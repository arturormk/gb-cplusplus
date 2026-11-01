#include "gb/media/tempo.hpp"

#include "ffmpeg_support.hpp"

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
}

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <string>

namespace gb::media {
namespace {

void check(const int code, const std::string &what) {
  if (code < 0)
    throw MediaError(ErrorKind::io, "audio tempo: " + what + ": " + detail::ffmpeg_error(code));
}

struct GraphCloser {
  void operator()(AVFilterGraph *graph) const { avfilter_graph_free(&graph); }
};

} // namespace

struct TempoStretcher::Impl {
  int rate{};
  std::unique_ptr<AVFilterGraph, GraphCloser> graph;
  AVFilterContext *source{}; // owned by the graph
  AVFilterContext *sink{};
  detail::FramePtr input{av_frame_alloc()};
  detail::FramePtr output{av_frame_alloc()};
  std::int64_t pts{};
  bool finished{};

  AVFilterContext *add(const char *filter, const char *name, const std::string &arguments) {
    AVFilterContext *context = nullptr;
    check(avfilter_graph_create_filter(&context, avfilter_get_by_name(filter), name,
                                       arguments.c_str(), nullptr, graph.get()),
          std::string("cannot create ") + filter);
    return context;
  }

  void drain(std::vector<float> &out) {
    while (true) {
      const auto result = av_buffersink_get_frame(sink, output.get());
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
        return;
      check(result, "filtering failed");
      const auto *data = reinterpret_cast<const float *>(output->data[0]);
      out.insert(out.end(), data, data + static_cast<std::ptrdiff_t>(output->nb_samples) * 2);
      av_frame_unref(output.get());
    }
  }
};

TempoStretcher::TempoStretcher(const int sample_rate, const double tempo)
    : impl_(std::make_unique<Impl>()) {
  auto &m = *impl_;
  if (sample_rate <= 0 || !(tempo >= 0.5 && tempo <= 100.0))
    throw MediaError(ErrorKind::invalid_argument, "audio tempo: unsupported rate or tempo");
  if (!m.input || !m.output)
    throw MediaError(ErrorKind::io, "audio tempo: out of memory");
  m.rate = sample_rate;
  m.graph.reset(avfilter_graph_alloc());
  if (!m.graph)
    throw MediaError(ErrorKind::io, "audio tempo: out of memory");

  const auto rate = std::to_string(sample_rate);
  m.source =
      m.add("abuffer", "in",
            "sample_rate=" + rate + ":sample_fmt=flt:channel_layout=stereo:time_base=1/" + rate);
  std::array<char, 32> text{}; // not std::to_string: Qt may have set a decimal-comma locale
  const auto written =
      std::to_chars(text.data(), text.data() + text.size(), tempo, std::chars_format::fixed, 6);
  auto *stretch = m.add("atempo", "tempo", "tempo=" + std::string(text.data(), written.ptr));
  auto *format = m.add("aformat", "format",
                       "sample_fmts=flt:sample_rates=" + rate + ":channel_layouts=stereo");
  m.sink = m.add("abuffersink", "out", "");
  check(avfilter_link(m.source, 0, stretch, 0), "cannot link the filters");
  check(avfilter_link(stretch, 0, format, 0), "cannot link the filters");
  check(avfilter_link(format, 0, m.sink, 0), "cannot link the filters");
  check(avfilter_graph_config(m.graph.get(), nullptr), "cannot configure the filters");
}

TempoStretcher::~TempoStretcher() = default;

void TempoStretcher::push(const float *samples, std::int64_t frames, std::vector<float> &output) {
  auto &m = *impl_;
  constexpr std::int64_t kChunk = 4096;
  while (frames > 0 && !m.finished) {
    const auto count = static_cast<int>(std::min(frames, kChunk));
    auto *frame = m.input.get();
    frame->format = AV_SAMPLE_FMT_FLT;
    frame->sample_rate = m.rate;
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
    av_channel_layout_default(&frame->ch_layout, 2);
#else
    frame->channel_layout = AV_CH_LAYOUT_STEREO;
    frame->channels = 2;
#endif
    frame->nb_samples = count;
    check(av_frame_get_buffer(frame, 0), "cannot allocate a frame");
    std::memcpy(frame->data[0], samples, static_cast<std::size_t>(count) * 2U * sizeof(float));
    frame->pts = m.pts;
    m.pts += count;
    check(av_buffersrc_add_frame(m.source, frame), "cannot add samples"); // takes the buffers
    av_frame_unref(frame);
    m.drain(output);
    samples += static_cast<std::ptrdiff_t>(count) * 2;
    frames -= count;
  }
}

void TempoStretcher::finish(std::vector<float> &output) {
  auto &m = *impl_;
  if (m.finished)
    return;
  m.finished = true;
  check(av_buffersrc_add_frame(m.source, nullptr), "cannot end the input");
  m.drain(output);
}

} // namespace gb::media
