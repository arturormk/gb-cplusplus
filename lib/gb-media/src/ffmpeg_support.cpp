#include "ffmpeg_support.hpp"

extern "C" {
#include <libavutil/pixdesc.h>
}

#include <cerrno>
#include <cstring>

namespace gb::media::detail {
namespace {

std::string name_or_unknown(const char *name) { return name == nullptr ? "unknown" : name; }

} // namespace

std::string ffmpeg_error(const int code) {
  std::string result(AV_ERROR_MAX_STRING_SIZE, '\0');
  av_strerror(code, result.data(), result.size());
  result.resize(std::strlen(result.c_str()));
  return result;
}

FormatPtr open_input(const std::filesystem::path &path) {
  std::error_code filesystem_error;
  if (!std::filesystem::exists(path, filesystem_error)) {
    if (filesystem_error) {
      throw MediaError(ErrorKind::io, "cannot inspect input: " + filesystem_error.message());
    }
    throw MediaError(ErrorKind::missing, "input does not exist: " + path.string());
  }
  AVFormatContext *raw = nullptr;
  const auto result = avformat_open_input(&raw, path.c_str(), nullptr, nullptr);
  if (result < 0) {
    auto kind = ErrorKind::unsupported;
    if (result == AVERROR_INVALIDDATA)
      kind = ErrorKind::corrupt;
    else if (result == AVERROR(EIO) || result == AVERROR(EACCES))
      kind = ErrorKind::io;
    throw MediaError(kind, "cannot open media: " + ffmpeg_error(result));
  }
  FormatPtr context(raw);
  const auto info_result = avformat_find_stream_info(context.get(), nullptr);
  if (info_result < 0) {
    throw MediaError(ErrorKind::corrupt,
                     "cannot read stream information: " + ffmpeg_error(info_result));
  }
  return context;
}

ColorMetadata color_metadata(const AVCodecParameters &parameters) {
  return {name_or_unknown(av_get_pix_fmt_name(static_cast<AVPixelFormat>(parameters.format))),
          name_or_unknown(av_color_range_name(parameters.color_range)),
          name_or_unknown(av_color_space_name(parameters.color_space)),
          name_or_unknown(av_color_transfer_name(parameters.color_trc)),
          name_or_unknown(av_color_primaries_name(parameters.color_primaries)),
          name_or_unknown(av_chroma_location_name(parameters.chroma_location))};
}

ColorMetadata color_metadata(const AVFrame &frame) {
  return {name_or_unknown(av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame.format))),
          name_or_unknown(av_color_range_name(frame.color_range)),
          name_or_unknown(av_color_space_name(frame.colorspace)),
          name_or_unknown(av_color_transfer_name(frame.color_trc)),
          name_or_unknown(av_color_primaries_name(frame.color_primaries)),
          name_or_unknown(av_chroma_location_name(frame.chroma_location))};
}

VideoStream describe_video_stream(const AVStream &stream, const int index) {
  const auto *parameters = stream.codecpar;
  const auto *descriptor = avcodec_descriptor_get(parameters->codec_id);
  return {index,
          descriptor == nullptr ? "unknown" : descriptor->name,
          parameters->width,
          parameters->height,
          {stream.time_base.num, stream.time_base.den},
          stream.start_time == AV_NOPTS_VALUE ? 0 : stream.start_time,
          stream.duration,
          {stream.avg_frame_rate.num, stream.avg_frame_rate.den},
          color_metadata(*parameters)};
}

CodecPtr open_decoder(const AVStream &stream, const int threads) {
  const auto *decoder = avcodec_find_decoder(stream.codecpar->codec_id);
  if (decoder == nullptr)
    throw MediaError(ErrorKind::unsupported, "no decoder for selected stream");
  CodecPtr codec(avcodec_alloc_context3(decoder));
  if (!codec)
    throw MediaError(ErrorKind::io, "cannot allocate decoder context");
  auto result = avcodec_parameters_to_context(codec.get(), stream.codecpar);
  if (result < 0)
    throw MediaError(ErrorKind::corrupt, "invalid codec parameters");
  codec->thread_count = threads;
  if (threads != 1)
    codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
  result = avcodec_open2(codec.get(), decoder, nullptr);
  if (result < 0)
    throw MediaError(ErrorKind::unsupported, "cannot open decoder: " + ffmpeg_error(result));
  return codec;
}

} // namespace gb::media::detail
