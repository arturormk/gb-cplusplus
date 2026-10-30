#pragma once

#include "gb/media/media.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include <filesystem>
#include <memory>
#include <string>

namespace gb::media::detail {

struct FormatCloser {
  void operator()(AVFormatContext *context) const { avformat_close_input(&context); }
};
struct CodecCloser {
  void operator()(AVCodecContext *context) const { avcodec_free_context(&context); }
};
struct FrameCloser {
  void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};
struct PacketCloser {
  void operator()(AVPacket *packet) const { av_packet_free(&packet); }
};
struct ScaleCloser {
  void operator()(SwsContext *context) const { sws_freeContext(context); }
};

using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecCloser>;
using FramePtr = std::unique_ptr<AVFrame, FrameCloser>;
using PacketPtr = std::unique_ptr<AVPacket, PacketCloser>;
using ScalePtr = std::unique_ptr<SwsContext, ScaleCloser>;

[[nodiscard]] std::string ffmpeg_error(int code);
[[nodiscard]] FormatPtr open_input(const std::filesystem::path &path);
[[nodiscard]] ColorMetadata color_metadata(const AVCodecParameters &parameters);
[[nodiscard]] ColorMetadata color_metadata(const AVFrame &frame);
[[nodiscard]] VideoStream describe_video_stream(const AVStream &stream, int index);
[[nodiscard]] CodecPtr open_decoder(const AVStream &stream, int threads);

} // namespace gb::media::detail
