#include "gb/media/playback.hpp"

#include "ffmpeg_support.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <set>
#include <utility>

namespace gb::media {

using namespace detail;

namespace {

__extension__ typedef __int128 Int128;

constexpr std::int64_t kMaximumPixels = 128LL * 1024LL * 1024LL;
// Bounds memory when one stream stops producing frames that match the others.
constexpr std::size_t kMaximumQueuedFrames = 16;

struct Decoder {
  AVStream *stream{};
  CodecPtr codec;
  std::deque<FramePtr> frames;
  std::int64_t start{};
  ScalePtr converter;
};

bool planar_420(const int format) {
  return format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P;
}

} // namespace

std::string source_layout_name(const SourceLayout layout) {
  switch (layout) {
  case SourceLayout::flat:
    return "flat";
  case SourceLayout::max_dual_eac:
    return "max_dual_eac";
  case SourceLayout::equirectangular:
    return "equirectangular";
  }
  return "unknown";
}

PlaybackLayout select_playback_layout(const std::vector<VideoStream> &streams) {
  if (streams.empty())
    throw MediaError(ErrorKind::unsupported, "no video stream found");
  const auto &first = streams.front();
  if (streams.size() == 2U) {
    const auto &second = streams[1];
    const auto overlap = static_cast<std::int64_t>(first.width) - 3LL * first.height;
    if (first.width == second.width && first.height == second.height && first.height > 0 &&
        first.time_base.numerator == second.time_base.numerator &&
        first.time_base.denominator == second.time_base.denominator && overlap > 0 &&
        overlap <= 256 && overlap % 2 == 0) {
      return {SourceLayout::max_dual_eac,
              {first.index, second.index},
              static_cast<std::int32_t>(overlap),
              "MAX dual-stream EAC: streams " + std::to_string(first.index) +
                  " (LEFT/FRONT/RIGHT) and " + std::to_string(second.index) + " (DOWN/BACK/TOP), " +
                  std::to_string(overlap) + " px overlap"};
    }
    return {SourceLayout::flat,
            {first.index},
            0,
            "two video streams without the verified MAX EAC geometry; showing stream " +
                std::to_string(first.index) + " only"};
  }
  if (streams.size() > 2U) {
    return {SourceLayout::flat,
            {first.index},
            0,
            std::to_string(streams.size()) +
                " video streams with an unverified layout; showing stream " +
                std::to_string(first.index) + " only"};
  }
  return {SourceLayout::flat,
          {first.index},
          0,
          "single video stream " + std::to_string(first.index) +
              " shown flat; no projection claimed"};
}

std::int64_t rescale_ticks(const std::int64_t ticks, const Rational from, const Rational to) {
  if (from.numerator <= 0 || from.denominator <= 0 || to.numerator <= 0 || to.denominator <= 0) {
    throw MediaError(ErrorKind::invalid_argument, "time bases must be positive");
  }
  const Int128 numerator = static_cast<Int128>(ticks) * from.numerator * to.denominator;
  const Int128 denominator = static_cast<Int128>(from.denominator) * to.numerator;
  auto quotient = numerator / denominator;
  if (numerator % denominator != 0 && numerator < 0)
    --quotient;
  if (quotient > std::numeric_limits<std::int64_t>::max() ||
      quotient < std::numeric_limits<std::int64_t>::min()) {
    throw MediaError(ErrorKind::invalid_argument, "rescaled time is out of range");
  }
  return static_cast<std::int64_t>(quotient);
}

struct SynchronizedReader::Impl {
  FormatPtr format;
  std::vector<Decoder> decoders;
  std::vector<int> decoder_for_stream;
  std::vector<VideoStream> streams;
  PacketPtr packet{av_packet_alloc()};
  bool input_finished{};
  std::optional<std::int64_t> seek_target;
  std::optional<FrameSet> pending;
  std::uint64_t unpaired{};

  [[nodiscard]] std::int64_t relative_pts(const Decoder &decoder, const AVFrame &frame) const {
    return frame.best_effort_timestamp - decoder.start;
  }

  [[nodiscard]] int compare_heads(const std::size_t left, const std::size_t right) const {
    const auto &a = decoders[left];
    const auto &b = decoders[right];
    return av_compare_ts(relative_pts(a, *a.frames.front()), a.stream->time_base,
                         relative_pts(b, *b.frames.front()), b.stream->time_base);
  }

  void receive(Decoder &decoder) {
    while (true) {
      FramePtr frame(av_frame_alloc());
      if (!frame)
        throw MediaError(ErrorKind::io, "cannot allocate decode frame");
      const auto result = avcodec_receive_frame(decoder.codec.get(), frame.get());
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
        return;
      if (result < 0)
        throw MediaError(ErrorKind::corrupt, "decode failed: " + ffmpeg_error(result));
      if (frame->best_effort_timestamp == AV_NOPTS_VALUE)
        continue;
      if (frame->width <= 0 || frame->height <= 0 ||
          static_cast<std::int64_t>(frame->width) * frame->height > kMaximumPixels) {
        throw MediaError(ErrorKind::unsupported, "decoded frame dimensions exceed safety limits");
      }
      decoder.frames.push_back(std::move(frame));
      if (decoder.frames.size() > kMaximumQueuedFrames) {
        decoder.frames.pop_front();
        ++unpaired;
      }
    }
  }

  void send(Decoder &decoder, const AVPacket *data) {
    auto result = avcodec_send_packet(decoder.codec.get(), data);
    if (result == AVERROR(EAGAIN)) {
      receive(decoder);
      result = avcodec_send_packet(decoder.codec.get(), data);
    }
    if (result < 0 && result != AVERROR_EOF) {
      throw MediaError(ErrorKind::corrupt, "cannot submit packet: " + ffmpeg_error(result));
    }
    receive(decoder);
  }

  PlanarPicture picture(Decoder &decoder, FramePtr frame) {
    if (!planar_420(frame->format)) {
      // Deprecated YUVJ formats are plain YUV at full range; naming the range explicitly keeps
      // swscale quiet and the conversion range-preserving.
      auto source_format = static_cast<AVPixelFormat>(frame->format);
      const auto full_range =
          frame->color_range == AVCOL_RANGE_JPEG || source_format == AV_PIX_FMT_YUVJ422P ||
          source_format == AV_PIX_FMT_YUVJ444P || source_format == AV_PIX_FMT_YUVJ440P ||
          source_format == AV_PIX_FMT_YUVJ411P;
      if (source_format == AV_PIX_FMT_YUVJ422P)
        source_format = AV_PIX_FMT_YUV422P;
      else if (source_format == AV_PIX_FMT_YUVJ444P)
        source_format = AV_PIX_FMT_YUV444P;
      else if (source_format == AV_PIX_FMT_YUVJ440P)
        source_format = AV_PIX_FMT_YUV440P;
      else if (source_format == AV_PIX_FMT_YUVJ411P)
        source_format = AV_PIX_FMT_YUV411P;
      decoder.converter.reset(sws_getCachedContext(
          decoder.converter.release(), frame->width, frame->height, source_format, frame->width,
          frame->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr));
      if (!decoder.converter) {
        throw MediaError(ErrorKind::unsupported, "cannot convert decoded pixel format");
      }
      // FFmpeg reports failure for YUV-to-YUV here although it records both ranges; ignore it.
      const auto *coefficients = sws_getCoefficients(SWS_CS_DEFAULT);
      static_cast<void>(sws_setColorspaceDetails(decoder.converter.get(), coefficients,
                                                 full_range ? 1 : 0, coefficients,
                                                 full_range ? 1 : 0, 0, 1 << 16, 1 << 16));
      FramePtr converted(av_frame_alloc());
      if (!converted)
        throw MediaError(ErrorKind::io, "cannot allocate conversion frame");
      converted->format = AV_PIX_FMT_YUV420P;
      converted->width = frame->width;
      converted->height = frame->height;
      if (av_frame_get_buffer(converted.get(), 32) < 0 ||
          av_frame_copy_props(converted.get(), frame.get()) < 0) {
        throw MediaError(ErrorKind::io, "cannot allocate conversion buffer");
      }
      const auto rows = sws_scale(decoder.converter.get(), frame->data, frame->linesize, 0,
                                  frame->height, converted->data, converted->linesize);
      if (rows != frame->height)
        throw MediaError(ErrorKind::corrupt, "incomplete pixel conversion");
      converted->color_range = full_range ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
      // Keep the decoder's own pixel-format description for diagnostics.
      auto color = color_metadata(*frame);
      auto result = picture(decoder, std::move(converted));
      result.source_color = std::move(color);
      return result;
    }
    for (int plane = 0; plane < 3; ++plane) {
      if (frame->linesize[plane] <= 0 || frame->data[plane] == nullptr) {
        throw MediaError(ErrorKind::unsupported, "decoded frame has an unsupported plane layout");
      }
    }
    PlanarPicture result;
    result.width = frame->width;
    result.height = frame->height;
    for (std::size_t plane = 0; plane < 3U; ++plane) {
      result.planes[plane] = frame->data[plane];
      result.strides[plane] = frame->linesize[plane];
    }
    result.stream_index = decoder.stream->index;
    result.pts = relative_pts(decoder, *frame);
    result.duration = detail::frame_duration(*frame);
    result.time_base = {decoder.stream->time_base.num, decoder.stream->time_base.den};
    result.source_color = color_metadata(*frame);
    // Mirror the CPU reference conversion: BT.709 when declared, otherwise BT.601.
    if (frame->colorspace != AVCOL_SPC_BT709) {
      result.luma_red = 0.299L;
      result.luma_blue = 0.114L;
    }
    result.full_range =
        frame->color_range == AVCOL_RANGE_JPEG || frame->format == AV_PIX_FMT_YUVJ420P;
    std::shared_ptr<AVFrame> owner(frame.release(), [](AVFrame *value) { av_frame_free(&value); });
    result.storage = std::move(owner);
    return result;
  }

  std::optional<FrameSet> pair() {
    while (true) {
      for (const auto &decoder : decoders) {
        if (decoder.frames.empty())
          return std::nullopt;
      }
      std::size_t latest = 0;
      for (std::size_t index = 1; index < decoders.size(); ++index) {
        if (compare_heads(index, latest) > 0)
          latest = index;
      }
      bool aligned = true;
      for (std::size_t index = 0; index < decoders.size(); ++index) {
        if (compare_heads(index, latest) < 0) {
          decoders[index].frames.pop_front();
          ++unpaired;
          aligned = false;
        }
      }
      if (!aligned)
        continue;
      FrameSet set;
      for (auto &decoder : decoders) {
        auto frame = std::move(decoder.frames.front());
        decoder.frames.pop_front();
        set.pictures.push_back(picture(decoder, std::move(frame)));
      }
      const auto &first = set.pictures.front();
      set.pts = first.pts;
      set.time_base = first.time_base;
      set.duration = first.duration;
      const auto rate = decoders.front().stream->avg_frame_rate;
      if (set.duration <= 0 && rate.num > 0 && rate.den > 0) {
        set.duration = std::max<std::int64_t>(
            1, av_rescale_q(1, {rate.den, rate.num}, decoders.front().stream->time_base));
      }
      return set;
    }
  }

  std::optional<FrameSet> decode_set() {
    while (true) {
      if (auto set = pair(); set.has_value())
        return set;
      if (input_finished) {
        for (auto &decoder : decoders) {
          unpaired += decoder.frames.size();
          decoder.frames.clear();
        }
        return std::nullopt;
      }
      const auto result = av_read_frame(format.get(), packet.get());
      if (result == AVERROR_EOF) {
        input_finished = true;
        for (auto &decoder : decoders)
          send(decoder, nullptr);
        continue;
      }
      if (result < 0)
        throw MediaError(ErrorKind::corrupt, "packet read failed: " + ffmpeg_error(result));
      const auto stream = packet->stream_index;
      if (stream >= 0 && static_cast<std::size_t>(stream) < decoder_for_stream.size() &&
          decoder_for_stream[static_cast<std::size_t>(stream)] >= 0) {
        try {
          send(decoders[static_cast<std::size_t>(
                   decoder_for_stream[static_cast<std::size_t>(stream)])],
               packet.get());
        } catch (...) {
          av_packet_unref(packet.get());
          throw;
        }
      }
      av_packet_unref(packet.get());
    }
  }
};

SynchronizedReader::SynchronizedReader(const std::filesystem::path &path,
                                       std::vector<int> stream_indices, const int decoder_threads)
    : impl_(std::make_unique<Impl>()) {
  if (stream_indices.empty()) {
    throw MediaError(ErrorKind::invalid_argument, "select at least one video stream");
  }
  if (decoder_threads < 0 || decoder_threads > 64) {
    throw MediaError(ErrorKind::invalid_argument, "decoder thread count must be 0..64");
  }
  if (!impl_->packet)
    throw MediaError(ErrorKind::io, "cannot allocate packet");
  impl_->format = open_input(path);
  auto &format = *impl_->format;
  impl_->decoder_for_stream.assign(format.nb_streams, -1);
  std::set<int> unique;
  for (const auto index : stream_indices) {
    if (index < 0 || static_cast<unsigned>(index) >= format.nb_streams ||
        format.streams[index]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO ||
        !unique.insert(index).second) {
      throw MediaError(ErrorKind::invalid_argument,
                       "selected streams must be distinct video streams");
    }
  }
  for (unsigned index = 0; index < format.nb_streams; ++index) {
    if (unique.count(static_cast<int>(index)) == 0U)
      format.streams[index]->discard = AVDISCARD_ALL;
  }
  for (const auto index : stream_indices) {
    auto *stream = format.streams[index];
    Decoder decoder;
    decoder.stream = stream;
    decoder.codec = open_decoder(*stream, decoder_threads);
    decoder.start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    impl_->decoder_for_stream[static_cast<std::size_t>(index)] =
        static_cast<int>(impl_->decoders.size());
    impl_->decoders.push_back(std::move(decoder));
    impl_->streams.push_back(describe_video_stream(*stream, index));
  }
}

SynchronizedReader::~SynchronizedReader() = default;

const std::vector<VideoStream> &SynchronizedReader::streams() const noexcept {
  return impl_->streams;
}

Rational SynchronizedReader::time_base() const noexcept { return impl_->streams.front().time_base; }

std::int64_t SynchronizedReader::duration_ticks() const noexcept {
  const auto *stream = impl_->decoders.front().stream;
  if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
    return stream->duration;
  if (impl_->format->duration != AV_NOPTS_VALUE && impl_->format->duration > 0) {
    return av_rescale_q(impl_->format->duration, AV_TIME_BASE_Q, stream->time_base);
  }
  return 0;
}

std::uint64_t SynchronizedReader::unpaired_frames() const noexcept { return impl_->unpaired; }

void SynchronizedReader::seek(const std::int64_t presentation_ticks) {
  if (presentation_ticks < 0) {
    throw MediaError(ErrorKind::invalid_argument, "presentation timestamp cannot be negative");
  }
  const auto &first = impl_->decoders.front();
  if (presentation_ticks > std::numeric_limits<std::int64_t>::max() - first.start) {
    throw MediaError(ErrorKind::invalid_argument, "presentation timestamp is out of range");
  }
  const auto result = av_seek_frame(impl_->format.get(), first.stream->index,
                                    presentation_ticks + first.start, AVSEEK_FLAG_BACKWARD);
  if (result < 0 && presentation_ticks != 0) {
    throw MediaError(ErrorKind::unsupported, "stream is not seekable: " + ffmpeg_error(result));
  }
  for (auto &decoder : impl_->decoders) {
    avcodec_flush_buffers(decoder.codec.get());
    decoder.frames.clear();
  }
  impl_->input_finished = false;
  impl_->pending.reset();
  impl_->seek_target = presentation_ticks;
}

FrameSet decode_still(const std::filesystem::path &path) {
  const auto streams = video_streams(path);
  if (streams.empty())
    throw MediaError(ErrorKind::unsupported, "no image stream found");
  SynchronizedReader reader(path, {streams.front().index}, 1);
  auto set = reader.next();
  if (!set.has_value())
    throw MediaError(ErrorKind::corrupt, "the image could not be decoded");
  return std::move(*set);
}

std::optional<FrameSet> SynchronizedReader::next() {
  if (impl_->pending.has_value()) {
    auto result = std::move(impl_->pending);
    impl_->pending.reset();
    return result;
  }
  if (!impl_->seek_target.has_value())
    return impl_->decode_set();
  const auto target = *impl_->seek_target;
  impl_->seek_target.reset();
  std::optional<FrameSet> candidate;
  while (auto set = impl_->decode_set()) {
    if (set->pts > target) {
      if (!candidate.has_value())
        return set;
      impl_->pending = std::move(set);
      return candidate;
    }
    candidate = std::move(set);
  }
  return candidate;
}

} // namespace gb::media
