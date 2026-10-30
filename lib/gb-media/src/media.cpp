#include "gb/media/media.hpp"

#include "ffmpeg_support.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>

namespace gb::media {

using namespace detail;

namespace {

constexpr std::int64_t kMaximumPixels = 128LL * 1024LL * 1024LL;

void validate_dimensions(const int width, const int height) {
  if (width <= 0 || height <= 0 || static_cast<std::int64_t>(width) * height > kMaximumPixels) {
    throw MediaError(ErrorKind::unsupported, "decoded frame dimensions exceed safety limits");
  }
}

RgbFrame convert_frame(const AVFrame &source, const int stream_index,
                       const std::int64_t requested_pts, const AVRational time_base) {
  validate_dimensions(source.width, source.height);
  RgbFrame result;
  result.width = source.width;
  result.height = source.height;
  result.pixels.resize(static_cast<std::size_t>(source.width) *
                       static_cast<std::size_t>(source.height) * 3U);
  result.stream_index = stream_index;
  result.requested_pts = requested_pts;
  result.pts = source.best_effort_timestamp;
  result.duration = source.pkt_duration;
  result.time_base = {time_base.num, time_base.den};
  result.source_color = color_metadata(source);

  auto source_format = static_cast<AVPixelFormat>(source.format);
  const auto source_is_full_range = source.color_range == AVCOL_RANGE_JPEG ||
                                    source_format == AV_PIX_FMT_YUVJ420P ||
                                    source_format == AV_PIX_FMT_YUVJ422P ||
                                    source_format == AV_PIX_FMT_YUVJ444P;
  if (source_format == AV_PIX_FMT_YUVJ420P) source_format = AV_PIX_FMT_YUV420P;
  else if (source_format == AV_PIX_FMT_YUVJ422P) source_format = AV_PIX_FMT_YUV422P;
  else if (source_format == AV_PIX_FMT_YUVJ444P) source_format = AV_PIX_FMT_YUV444P;
  ScalePtr scaler(sws_getContext(source.width, source.height, source_format, source.width,
                                 source.height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr,
                                 nullptr));
  if (!scaler) throw MediaError(ErrorKind::unsupported, "cannot create RGB conversion context");
  const auto coefficient = source.colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_DEFAULT;
  const auto *coefficients = sws_getCoefficients(coefficient);
  if (sws_setColorspaceDetails(scaler.get(), coefficients, source_is_full_range ? 1 : 0,
                               coefficients, 1, 0, 1 << 16, 1 << 16) < 0) {
    throw MediaError(ErrorKind::unsupported, "cannot configure RGB color conversion");
  }
  const auto packed_stride = source.width * 3;
  const auto aligned_stride = (packed_stride + 31) & ~31;
  std::vector<std::uint8_t> converted(static_cast<std::size_t>(aligned_stride) *
                                          static_cast<std::size_t>(source.height) +
                                      AV_INPUT_BUFFER_PADDING_SIZE);
  std::uint8_t *destination[] = {converted.data(), nullptr, nullptr, nullptr};
  const int strides[] = {aligned_stride, 0, 0, 0};
  const auto rows = sws_scale(scaler.get(), source.data, source.linesize, 0, source.height,
                              destination, strides);
  if (rows != source.height) throw MediaError(ErrorKind::corrupt, "incomplete RGB conversion");
  for (int row = 0; row < source.height; ++row) {
    const auto source_offset = static_cast<std::size_t>(row) *
                               static_cast<std::size_t>(aligned_stride);
    const auto destination_offset = static_cast<std::size_t>(row) *
                                    static_cast<std::size_t>(packed_stride);
    std::copy_n(converted.begin() + static_cast<std::ptrdiff_t>(source_offset), packed_stride,
                result.pixels.begin() + static_cast<std::ptrdiff_t>(destination_offset));
  }
  return result;
}

void write_bytes(const std::filesystem::path &path, const std::uint8_t *data,
                 const std::size_t size) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw MediaError(ErrorKind::io, "cannot create output: " + path.string());
  output.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
  if (!output) throw MediaError(ErrorKind::io, "cannot write output: " + path.string());
}

} // namespace

MediaError::MediaError(const ErrorKind kind, const std::string &message)
    : std::runtime_error(message), kind_(kind) {}

ErrorKind MediaError::kind() const noexcept { return kind_; }

std::string error_kind_name(const ErrorKind kind) {
  switch (kind) {
  case ErrorKind::missing: return "missing";
  case ErrorKind::io: return "io";
  case ErrorKind::unsupported: return "unsupported";
  case ErrorKind::corrupt: return "corrupt";
  case ErrorKind::invalid_argument: return "invalid_argument";
  }
  return "unexpected";
}

std::vector<VideoStream> video_streams(const std::filesystem::path &path) {
  const auto context = open_input(path);
  std::vector<VideoStream> result;
  for (unsigned index = 0; index < context->nb_streams; ++index) {
    const auto *stream = context->streams[index];
    const auto *parameters = stream->codecpar;
    if (parameters->codec_type != AVMEDIA_TYPE_VIDEO) continue;
    result.push_back(describe_video_stream(*stream, static_cast<int>(index)));
  }
  return result;
}

RgbFrame decode_frame(const std::filesystem::path &path, const int stream_index,
                      const std::int64_t presentation_ticks) {
  if (presentation_ticks < 0) {
    throw MediaError(ErrorKind::invalid_argument, "presentation timestamp cannot be negative");
  }
  auto format = open_input(path);
  if (stream_index < 0 || static_cast<unsigned>(stream_index) >= format->nb_streams ||
      format->streams[stream_index]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
    throw MediaError(ErrorKind::invalid_argument, "selected stream is not a video stream");
  }
  auto *stream = format->streams[stream_index];
  auto codec = open_decoder(*stream, 1);

  const auto absolute_target = presentation_ticks +
      (stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time);
  auto result = av_seek_frame(format.get(), stream_index, absolute_target, AVSEEK_FLAG_BACKWARD);
  if (result < 0 && presentation_ticks != 0) {
    throw MediaError(ErrorKind::unsupported, "stream is not seekable: " + ffmpeg_error(result));
  }
  avcodec_flush_buffers(codec.get());

  PacketPtr packet(av_packet_alloc());
  FramePtr frame(av_frame_alloc());
  if (!packet || !frame) throw MediaError(ErrorKind::io, "cannot allocate decode buffers");
  FramePtr previous(av_frame_alloc());
  if (!previous) throw MediaError(ErrorKind::io, "cannot allocate frame selection buffer");
  bool has_previous{};

  const auto receive = [&]() -> std::optional<RgbFrame> {
    while (true) {
      const auto receive_result = avcodec_receive_frame(codec.get(), frame.get());
      if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) return std::nullopt;
      if (receive_result < 0) {
        throw MediaError(ErrorKind::corrupt, "decode failed: " + ffmpeg_error(receive_result));
      }
      const auto timestamp = frame->best_effort_timestamp;
      if (timestamp == AV_NOPTS_VALUE) {
        av_frame_unref(frame.get());
        continue;
      }
      if (timestamp >= absolute_target) {
        if (has_previous &&
            absolute_target - previous->best_effort_timestamp <= timestamp - absolute_target) {
          return convert_frame(*previous, stream_index, presentation_ticks, stream->time_base);
        }
        return convert_frame(*frame, stream_index, presentation_ticks, stream->time_base);
      }
      av_frame_unref(previous.get());
      const auto reference_result = av_frame_ref(previous.get(), frame.get());
      if (reference_result < 0) throw MediaError(ErrorKind::io, "cannot retain candidate frame");
      has_previous = true;
      av_frame_unref(frame.get());
    }
  };

  while ((result = av_read_frame(format.get(), packet.get())) >= 0) {
    if (packet->stream_index == stream_index) {
      const auto send_result = avcodec_send_packet(codec.get(), packet.get());
      if (send_result < 0 && send_result != AVERROR(EAGAIN)) {
        throw MediaError(ErrorKind::corrupt, "cannot submit packet: " + ffmpeg_error(send_result));
      }
      if (auto selected = receive(); selected.has_value()) return *selected;
    }
    av_packet_unref(packet.get());
  }
  if (result != AVERROR_EOF) throw MediaError(ErrorKind::corrupt, "packet read failed: " + ffmpeg_error(result));
  const auto flush_result = avcodec_send_packet(codec.get(), nullptr);
  if (flush_result >= 0) {
    if (auto selected = receive(); selected.has_value()) return *selected;
  }
  if (has_previous) return convert_frame(*previous, stream_index, presentation_ticks, stream->time_base);
  throw MediaError(ErrorKind::unsupported, "no decodable frame at the requested timestamp");
}

void write_png(const std::filesystem::path &path, const RgbFrame &frame) {
  const auto *encoder = avcodec_find_encoder(AV_CODEC_ID_PNG);
  if (encoder == nullptr) throw MediaError(ErrorKind::unsupported, "FFmpeg PNG encoder unavailable");
  CodecPtr codec(avcodec_alloc_context3(encoder));
  FramePtr source(av_frame_alloc());
  PacketPtr packet(av_packet_alloc());
  if (!codec || !source || !packet) throw MediaError(ErrorKind::io, "cannot allocate PNG encoder");
  codec->width = frame.width;
  codec->height = frame.height;
  codec->pix_fmt = AV_PIX_FMT_RGB24;
  codec->time_base = {1, 1};
  auto result = avcodec_open2(codec.get(), encoder, nullptr);
  if (result < 0) throw MediaError(ErrorKind::unsupported, "cannot open PNG encoder: " + ffmpeg_error(result));
  source->format = AV_PIX_FMT_RGB24;
  source->width = frame.width;
  source->height = frame.height;
  source->data[0] = const_cast<std::uint8_t *>(frame.pixels.data());
  source->linesize[0] = frame.width * 3;
  result = avcodec_send_frame(codec.get(), source.get());
  if (result < 0) throw MediaError(ErrorKind::io, "PNG encode failed: " + ffmpeg_error(result));
  result = avcodec_receive_packet(codec.get(), packet.get());
  if (result < 0) throw MediaError(ErrorKind::io, "PNG encode failed: " + ffmpeg_error(result));
  write_bytes(path, packet->data, static_cast<std::size_t>(packet->size));
}

void write_ppm(const std::filesystem::path &path, const RgbFrame &frame) {
  std::ostringstream header;
  header << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
  std::ofstream output(path, std::ios::binary);
  if (!output) throw MediaError(ErrorKind::io, "cannot create output: " + path.string());
  output << header.str();
  output.write(reinterpret_cast<const char *>(frame.pixels.data()),
               static_cast<std::streamsize>(frame.pixels.size()));
  if (!output) throw MediaError(ErrorKind::io, "cannot write output: " + path.string());
}

void write_rgb24(const std::filesystem::path &path, const RgbFrame &frame) {
  write_bytes(path, frame.pixels.data(), frame.pixels.size());
}

} // namespace gb::media
