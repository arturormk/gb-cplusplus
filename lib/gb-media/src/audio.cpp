#include "gb/media/audio.hpp"

#include "ffmpeg_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <utility>

namespace gb::media {

using namespace detail;

namespace {

// Decoded ahead of a seek target so lossy decoders (AAC needs one frame of overlap) converge
// before the first kept sample. PCM simply decodes and discards it.
constexpr std::int64_t kSeekPrerollSamples = 4096;

int channel_count(const AVCodecParameters &parameters) {
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
  return parameters.ch_layout.nb_channels;
#else
  return parameters.channels;
#endif
}

int channel_count(const AVFrame &frame) {
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
  return frame.ch_layout.nb_channels;
#else
  return frame.channels;
#endif
}

std::string channel_layout_name(const AVCodecParameters &parameters) {
  char buffer[128]{};
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 24, 100)
  if (parameters.ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) return "unknown";
  if (av_channel_layout_describe(&parameters.ch_layout, buffer, sizeof buffer) < 0) return "unknown";
#else
  if (parameters.channel_layout == 0U) return "unknown";
  av_get_channel_layout_string(buffer, sizeof buffer, parameters.channels, parameters.channel_layout);
#endif
  return buffer;
}

AudioStream describe_audio_stream(const AVStream &stream, const int index) {
  const auto *parameters = stream.codecpar;
  const auto *descriptor = avcodec_descriptor_get(parameters->codec_id);
  return {index,
          descriptor == nullptr ? "unknown" : descriptor->name,
          parameters->sample_rate,
          channel_count(*parameters),
          channel_layout_name(*parameters),
          {stream.time_base.num, stream.time_base.den},
          stream.start_time == AV_NOPTS_VALUE ? 0 : stream.start_time,
          stream.duration};
}

// One channel of one sample as float in [-1, 1].
float sample_value(const AVFrame &frame, const int channel, const int index) {
  const auto format = static_cast<AVSampleFormat>(frame.format);
  const auto planar = av_sample_fmt_is_planar(format) != 0;
  const auto channels = channel_count(frame);
  const auto position = static_cast<std::size_t>(planar ? index : index * channels + channel);
  const auto *data = frame.extended_data[planar ? channel : 0];
  switch (av_get_packed_sample_fmt(format)) {
  case AV_SAMPLE_FMT_U8: return (static_cast<float>(data[position]) - 128.0F) / 128.0F;
  case AV_SAMPLE_FMT_S16: {
    std::int16_t value{};
    std::memcpy(&value, data + position * sizeof value, sizeof value);
    return static_cast<float>(value) / 32768.0F;
  }
  case AV_SAMPLE_FMT_S32: {
    std::int32_t value{};
    std::memcpy(&value, data + position * sizeof value, sizeof value);
    return static_cast<float>(static_cast<double>(value) / 2147483648.0);
  }
  case AV_SAMPLE_FMT_FLT: {
    float value{};
    std::memcpy(&value, data + position * sizeof value, sizeof value);
    return value;
  }
  case AV_SAMPLE_FMT_DBL: {
    double value{};
    std::memcpy(&value, data + position * sizeof value, sizeof value);
    return static_cast<float>(value);
  }
  default: throw MediaError(ErrorKind::unsupported, "unsupported decoded audio sample format");
  }
}

} // namespace

std::vector<AudioStream> audio_streams(const std::filesystem::path &path) {
  const auto format = open_input(path);
  std::vector<AudioStream> result;
  for (unsigned index = 0; index < format->nb_streams; ++index) {
    const auto *stream = format->streams[index];
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
      result.push_back(describe_audio_stream(*stream, static_cast<int>(index)));
    }
  }
  return result;
}

AudioSelection select_playback_audio(const std::vector<AudioStream> &streams) {
  if (streams.empty()) return {std::nullopt, "no audio stream"};
  for (const auto &stream : streams) {
    if ((stream.channels == 1 || stream.channels == 2) && stream.sample_rate > 0) {
      auto description = "audio stream " + std::to_string(stream.index) + ": " + stream.codec + " " +
                         std::to_string(stream.sample_rate) + " Hz " +
                         (stream.channels == 1 ? "mono (played on both channels)" : "stereo");
      std::string skipped;
      for (const auto &other : streams) {
        if (other.index != stream.index) {
          skipped += (skipped.empty() ? "" : ", ") + std::to_string(other.index) + " (" + other.codec + ", " +
                     std::to_string(other.channels) + " ch)";
        }
      }
      if (!skipped.empty()) description += "; not played: stream " + skipped;
      return {stream.index, description};
    }
  }
  std::string found;
  for (const auto &stream : streams) {
    found += (found.empty() ? "" : ", ") + std::to_string(stream.index) + " (" + stream.codec + ", " +
             std::to_string(stream.channels) + " ch)";
  }
  return {std::nullopt, "no mono or stereo audio stream; multichannel/spatial audio is not rendered: stream " + found};
}

struct AudioReader::Impl {
  FormatPtr format;
  AVStream *av_stream{};
  CodecPtr codec;
  AudioStream stream;
  PacketPtr packet{av_packet_alloc()};
  FramePtr frame{av_frame_alloc()};
  std::deque<AudioBlock> ready;
  std::optional<std::int64_t> next_sample;
  std::int64_t discard_before{};
  bool input_finished{};
  bool decoder_drained{};

  [[nodiscard]] std::int64_t frame_position(const AVFrame &decoded) const {
    const auto pts = decoded.best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) return next_sample.value_or(0);
    return av_rescale_q_rnd(pts - stream.start_time, av_stream->time_base, {1, stream.sample_rate},
                            static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
  }

  void convert(const AVFrame &decoded) {
    const auto channels = channel_count(decoded);
    if (channels != 1 && channels != 2) {
      throw MediaError(ErrorKind::unsupported, "audio channel count changed mid-stream");
    }
    if (decoded.nb_samples <= 0) return;
    // Trust the running count for contiguous audio, and re-anchor on real timestamp gaps.
    const auto position = frame_position(decoded);
    const auto tolerance = std::max<std::int64_t>(1, stream.sample_rate / 50);
    if (!next_sample.has_value() || std::llabs(position - *next_sample) > tolerance) next_sample = position;
    AudioBlock block;
    block.first_sample = *next_sample;
    *next_sample += decoded.nb_samples;

    auto skip = std::clamp<std::int64_t>(discard_before - block.first_sample, 0, decoded.nb_samples);
    if (skip == decoded.nb_samples) return;
    block.first_sample += skip;
    block.samples.resize(static_cast<std::size_t>(decoded.nb_samples - skip) * 2U);
    for (auto index = static_cast<int>(skip); index < decoded.nb_samples; ++index) {
      const auto left = sample_value(decoded, 0, index);
      const auto right = channels == 2 ? sample_value(decoded, 1, index) : left;
      const auto out = static_cast<std::size_t>(index - skip) * 2U;
      block.samples[out] = left;
      block.samples[out + 1U] = right;
    }
    ready.push_back(std::move(block));
  }

  void receive() {
    while (true) {
      const auto result = avcodec_receive_frame(codec.get(), frame.get());
      if (result == AVERROR(EAGAIN)) return;
      if (result == AVERROR_EOF) {
        decoder_drained = true;
        return;
      }
      if (result < 0) throw MediaError(ErrorKind::corrupt, "audio decode failed: " + ffmpeg_error(result));
      try {
        convert(*frame);
      } catch (...) {
        av_frame_unref(frame.get());
        throw;
      }
      av_frame_unref(frame.get());
    }
  }

  void send(const AVPacket *data) {
    auto result = avcodec_send_packet(codec.get(), data);
    if (result == AVERROR(EAGAIN)) {
      receive();
      result = avcodec_send_packet(codec.get(), data);
    }
    // A damaged audio packet is skipped rather than ending playback.
    if (result < 0 && result != AVERROR_EOF && result != AVERROR_INVALIDDATA) {
      throw MediaError(ErrorKind::corrupt, "cannot submit audio packet: " + ffmpeg_error(result));
    }
    receive();
  }
};

AudioReader::AudioReader(const std::filesystem::path &path, const int stream_index)
    : impl_(std::make_unique<Impl>()) {
  if (!impl_->packet || !impl_->frame) throw MediaError(ErrorKind::io, "cannot allocate audio decode buffers");
  impl_->format = open_input(path);
  auto &format = *impl_->format;
  if (stream_index < 0 || static_cast<unsigned>(stream_index) >= format.nb_streams ||
      format.streams[stream_index]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
    throw MediaError(ErrorKind::invalid_argument, "selected stream is not an audio stream");
  }
  for (unsigned index = 0; index < format.nb_streams; ++index) {
    if (static_cast<int>(index) != stream_index) format.streams[index]->discard = AVDISCARD_ALL;
  }
  impl_->av_stream = format.streams[stream_index];
  impl_->stream = describe_audio_stream(*impl_->av_stream, stream_index);
  if (impl_->stream.channels != 1 && impl_->stream.channels != 2) {
    throw MediaError(ErrorKind::unsupported, "only mono and stereo audio streams can be played");
  }
  if (impl_->stream.sample_rate <= 0) throw MediaError(ErrorKind::corrupt, "audio stream has no sample rate");
  impl_->codec = open_decoder(*impl_->av_stream, 1);
}

AudioReader::~AudioReader() = default;

const AudioStream &AudioReader::stream() const noexcept { return impl_->stream; }
std::int32_t AudioReader::sample_rate() const noexcept { return impl_->stream.sample_rate; }

void AudioReader::seek(const std::int64_t sample) {
  if (sample < 0) throw MediaError(ErrorKind::invalid_argument, "audio sample position cannot be negative");
  auto &impl = *impl_;
  const auto preroll = std::max<std::int64_t>(0, sample - kSeekPrerollSamples);
  const auto target = av_rescale_q(preroll, {1, impl.stream.sample_rate}, impl.av_stream->time_base) +
                      impl.stream.start_time;
  const auto result = av_seek_frame(impl.format.get(), impl.av_stream->index, target, AVSEEK_FLAG_BACKWARD);
  if (result < 0 && sample != 0) {
    throw MediaError(ErrorKind::unsupported, "audio stream is not seekable: " + ffmpeg_error(result));
  }
  avcodec_flush_buffers(impl.codec.get());
  impl.ready.clear();
  impl.next_sample.reset();
  impl.discard_before = sample;
  impl.input_finished = false;
  impl.decoder_drained = false;
}

std::optional<AudioBlock> AudioReader::next() {
  auto &impl = *impl_;
  while (impl.ready.empty()) {
    if (impl.decoder_drained) return std::nullopt;
    if (impl.input_finished) {
      impl.send(nullptr);
      impl.decoder_drained = true;
      continue;
    }
    const auto result = av_read_frame(impl.format.get(), impl.packet.get());
    if (result == AVERROR_EOF) {
      impl.input_finished = true;
      continue;
    }
    if (result < 0) throw MediaError(ErrorKind::corrupt, "audio packet read failed: " + ffmpeg_error(result));
    try {
      if (impl.packet->stream_index == impl.av_stream->index) impl.send(impl.packet.get());
    } catch (...) {
      av_packet_unref(impl.packet.get());
      throw;
    }
    av_packet_unref(impl.packet.get());
  }
  auto block = std::move(impl.ready.front());
  impl.ready.pop_front();
  return block;
}

} // namespace gb::media
