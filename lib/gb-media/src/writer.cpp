#include "gb/media/writer.hpp"

#include "ffmpeg_support.hpp"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cstring>
#include <vector>

namespace gb::media {
namespace {

using detail::ffmpeg_error;

[[noreturn]] void fail(const ErrorKind kind, const std::string &message) {
  throw MediaError(kind, message);
}

void check(const int code, const std::string &what) {
  if (code < 0)
    fail(ErrorKind::io, what + ": " + ffmpeg_error(code));
}

AVRational av(const Rational value) { return {value.numerator, value.denominator}; }

struct OutputCloser {
  void operator()(AVFormatContext *context) const {
    if (context == nullptr)
      return;
    if (context->pb != nullptr && (context->oformat->flags & AVFMT_NOFILE) == 0)
      avio_closep(&context->pb);
    avformat_free_context(context);
  }
};

} // namespace

struct VideoWriter::Impl {
  VideoWriterOptions options;
  std::unique_ptr<AVFormatContext, OutputCloser> format;
  detail::CodecPtr video;
  detail::CodecPtr audio;
  AVStream *video_stream{};
  AVStream *audio_stream{};
  detail::FramePtr picture;
  detail::FramePtr sound;
  detail::PacketPtr packet;
  detail::ScalePtr scale;
  std::vector<float> pending; // interleaved stereo not yet encoded
  std::int64_t audio_next{};  // samples sent to the encoder
  std::int64_t audio_written{};
  bool header{};
  bool finished{};
  std::string description;

  void drain(AVCodecContext *codec, AVStream *stream) {
    while (true) {
      const auto result = avcodec_receive_packet(codec, packet.get());
      if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
        return;
      check(result, "encoding failed");
      av_packet_rescale_ts(packet.get(), codec->time_base, stream->time_base);
      packet->stream_index = stream->index;
      check(av_interleaved_write_frame(format.get(), packet.get()), "writing failed");
    }
  }

  void encode_audio_frames(const bool flush) {
    if (!audio)
      return;
    const auto size = audio->frame_size > 0 ? audio->frame_size : 1024;
    while (pending.size() >= static_cast<std::size_t>(size) * 2U || (flush && !pending.empty())) {
      const auto count = std::min<std::size_t>(static_cast<std::size_t>(size), pending.size() / 2U);
      check(av_frame_make_writable(sound.get()), "audio frame");
      sound->nb_samples = static_cast<int>(count);
      auto *left = reinterpret_cast<float *>(sound->data[0]);
      auto *right = reinterpret_cast<float *>(sound->data[1]);
      for (std::size_t index = 0; index < count; ++index) {
        left[index] = pending[2U * index];
        right[index] = pending[2U * index + 1U];
      }
      pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(count * 2U));
      sound->pts = audio_next;
      audio_next += static_cast<std::int64_t>(count);
      check(avcodec_send_frame(audio.get(), sound.get()), "audio encoding failed");
      drain(audio.get(), audio_stream);
    }
  }
};

VideoWriter::VideoWriter(const std::filesystem::path &path, const VideoWriterOptions &options)
    : impl_(std::make_unique<Impl>()) {
  auto &m = *impl_;
  m.options = options;
  // The encoders report statistics at the info level; only warnings and errors are of interest.
  if (av_log_get_level() > AV_LOG_WARNING)
    av_log_set_level(AV_LOG_WARNING);
  if (options.width < 2 || options.height < 2 || options.width % 2 != 0 ||
      options.height % 2 != 0) {
    fail(ErrorKind::invalid_argument, "the output size must be even");
  }
  if (options.time_base.numerator <= 0 || options.time_base.denominator <= 0 ||
      options.frame_rate.numerator <= 0 || options.frame_rate.denominator <= 0) {
    fail(ErrorKind::invalid_argument, "invalid time base or frame rate");
  }

  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const char *container = extension == ".mp4" ? "mp4" : extension == ".mkv" ? "matroska" : nullptr;
  if (container == nullptr)
    fail(ErrorKind::invalid_argument, "the output must end in .mp4 or .mkv");
  AVFormatContext *raw{};
  check(avformat_alloc_output_context2(&raw, nullptr, container, path.string().c_str()),
        "cannot create the output");
  m.format.reset(raw);

  // Video.
  const auto *encoder_name = options.codec == VideoCodec::hevc ? "libx265" : "libx264";
  const auto *encoder = avcodec_find_encoder_by_name(encoder_name);
  if (encoder == nullptr)
    fail(ErrorKind::unsupported,
         std::string("the encoder ") + encoder_name + " is not available in this FFmpeg build");
  m.video.reset(avcodec_alloc_context3(encoder));
  auto *video = m.video.get();
  video->width = options.width;
  video->height = options.height;
  video->pix_fmt = AV_PIX_FMT_YUV420P;
  video->time_base = av(options.time_base);
  video->framerate = av(options.frame_rate);
  video->color_primaries = AVCOL_PRI_BT709;
  video->color_trc = AVCOL_TRC_BT709;
  video->colorspace = AVCOL_SPC_BT709;
  video->color_range = AVCOL_RANGE_MPEG;
  video->sample_aspect_ratio = {1, 1};
  if ((m.format->oformat->flags & AVFMT_GLOBALHEADER) != 0)
    video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  av_opt_set(video->priv_data, "preset", options.preset.c_str(), 0);
  av_opt_set(video->priv_data, "crf", std::to_string(options.crf).c_str(), 0);
  if (options.codec == VideoCodec::hevc)
    av_opt_set(video->priv_data, "x265-params", "log-level=error", 0);
  check(avcodec_open2(video, encoder, nullptr), std::string("cannot open ") + encoder_name);
  m.video_stream = avformat_new_stream(m.format.get(), nullptr);
  if (m.video_stream == nullptr)
    fail(ErrorKind::io, "cannot add the video stream");
  check(avcodec_parameters_from_context(m.video_stream->codecpar, video), "video parameters");
  m.video_stream->time_base = video->time_base;
  m.video_stream->avg_frame_rate = video->framerate;
  if (options.codec == VideoCodec::hevc && std::strcmp(container, "mp4") == 0)
    m.video_stream->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
  m.description = std::string(encoder_name) + " " + std::to_string(options.width) + "×" +
                  std::to_string(options.height);

  // Audio.
  if (options.audio_sample_rate) {
    const auto *aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (aac == nullptr)
      fail(ErrorKind::unsupported, "the AAC encoder is not available in this FFmpeg build");
    m.audio.reset(avcodec_alloc_context3(aac));
    auto *audio = m.audio.get();
    audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
    audio->sample_rate = *options.audio_sample_rate;
    audio->bit_rate = options.audio_bitrate;
    audio->time_base = {1, *options.audio_sample_rate};
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
    av_channel_layout_default(&audio->ch_layout, 2);
#else
    audio->channel_layout = AV_CH_LAYOUT_STEREO;
    audio->channels = 2;
#endif
    if ((m.format->oformat->flags & AVFMT_GLOBALHEADER) != 0)
      audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    check(avcodec_open2(audio, aac, nullptr), "cannot open the AAC encoder");
    m.audio_stream = avformat_new_stream(m.format.get(), nullptr);
    if (m.audio_stream == nullptr)
      fail(ErrorKind::io, "cannot add the audio stream");
    check(avcodec_parameters_from_context(m.audio_stream->codecpar, audio), "audio parameters");
    m.audio_stream->time_base = audio->time_base;
    m.sound.reset(av_frame_alloc());
    m.sound->format = AV_SAMPLE_FMT_FLTP;
    m.sound->sample_rate = audio->sample_rate;
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
    check(av_channel_layout_copy(&m.sound->ch_layout, &audio->ch_layout), "audio layout");
#else
    m.sound->channel_layout = audio->channel_layout;
    m.sound->channels = 2;
#endif
    m.sound->nb_samples = audio->frame_size > 0 ? audio->frame_size : 1024;
    check(av_frame_get_buffer(m.sound.get(), 0), "audio frame");
    m.description += ", aac " + std::to_string(audio->sample_rate) + " Hz";
  }
  m.description += std::strcmp(container, "mp4") == 0 ? ", mp4" : ", mkv";

  m.picture.reset(av_frame_alloc());
  m.picture->format = AV_PIX_FMT_YUV420P;
  m.picture->width = options.width;
  m.picture->height = options.height;
  check(av_frame_get_buffer(m.picture.get(), 0), "video frame");
  m.packet.reset(av_packet_alloc());
  m.scale.reset(sws_getContext(options.width, options.height, AV_PIX_FMT_BGRA, options.width,
                               options.height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr,
                               nullptr));
  if (!m.scale)
    fail(ErrorKind::unsupported, "cannot convert pictures to yuv420p");
  const auto *coefficients = sws_getCoefficients(SWS_CS_ITU709);
  sws_setColorspaceDetails(m.scale.get(), coefficients, 1, coefficients, 0, 0, 1 << 16, 1 << 16);

  // No metadata from any source; the muxer adds only its encoder tag.
  if ((m.format->oformat->flags & AVFMT_NOFILE) == 0)
    check(avio_open(&m.format->pb, path.string().c_str(), AVIO_FLAG_WRITE),
          "cannot write " + path.string());
  AVDictionary *muxer_options{};
  if (std::strcmp(container, "mp4") == 0)
    av_dict_set(&muxer_options, "movflags", "+faststart", 0);
  const auto written = avformat_write_header(m.format.get(), &muxer_options);
  av_dict_free(&muxer_options);
  check(written, "cannot write the header");
  m.header = true;
}

VideoWriter::~VideoWriter() = default;

void VideoWriter::write_video(const std::uint8_t *pixels, const int stride,
                              const std::int64_t pts) {
  auto &m = *impl_;
  if (m.finished)
    fail(ErrorKind::invalid_argument, "the writer is finished");
  check(av_frame_make_writable(m.picture.get()), "video frame");
  const std::uint8_t *source[1] = {pixels};
  const int strides[1] = {stride};
  sws_scale(m.scale.get(), source, strides, 0, m.options.height, m.picture->data,
            m.picture->linesize);
  m.picture->pts = pts;
  check(avcodec_send_frame(m.video.get(), m.picture.get()), "video encoding failed");
  m.drain(m.video.get(), m.video_stream);
}

void VideoWriter::write_audio(const float *samples, const std::int64_t frames) {
  auto &m = *impl_;
  if (!m.audio || frames <= 0)
    return;
  if (m.finished)
    fail(ErrorKind::invalid_argument, "the writer is finished");
  m.pending.insert(m.pending.end(), samples, samples + frames * 2);
  m.audio_written += frames;
  m.encode_audio_frames(false);
}

std::int64_t VideoWriter::audio_frames_written() const noexcept { return impl_->audio_written; }

void VideoWriter::finish() {
  auto &m = *impl_;
  if (m.finished)
    return;
  m.finished = true;
  check(avcodec_send_frame(m.video.get(), nullptr), "flushing the video encoder");
  m.drain(m.video.get(), m.video_stream);
  if (m.audio) {
    m.encode_audio_frames(true);
    check(avcodec_send_frame(m.audio.get(), nullptr), "flushing the audio encoder");
    m.drain(m.audio.get(), m.audio_stream);
  }
  check(av_write_trailer(m.format.get()), "cannot finish the file");
  check(avio_closep(&m.format->pb), "cannot close the file");
}

std::string VideoWriter::description() const { return impl_->description; }

bool encoder_available(const std::string &name) {
  return avcodec_find_encoder_by_name(name.c_str()) != nullptr;
}

} // namespace gb::media
