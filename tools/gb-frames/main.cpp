#include "gb/media/media.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
  bool list{};
  bool json{};
  bool reconstruct{};
  bool force{};
  std::vector<int> streams;
  std::optional<long double> seconds;
  std::optional<std::int64_t> pts;
  std::filesystem::path output;
  std::string format;
  std::int32_t width{4096};
  std::int32_t height{2048};
  std::int32_t overlap{64};
  std::filesystem::path input;
};

void help() {
  std::cout <<
      "Usage: gb-frames --list [--json] <file>\n"
      "       gb-frames [options] --output <image> <file>\n\n"
      "Decode an exact source stream frame or reconstruct two MAX EAC streams.\n\n"
      "Options:\n"
      "  --list             list video streams without decoding\n"
      "  --stream INDEX     select a video stream (repeat twice for reconstruction)\n"
      "  --time TIME        media time in seconds or HH:MM:SS[.fraction] (default 0)\n"
      "  --pts TICKS        exact requested ticks (single-stream mode only)\n"
      "  --reconstruct      produce a CPU reference equirectangular panorama\n"
      "  --width PIXELS     reconstructed width (default 4096)\n"
      "  --height PIXELS    reconstructed height (default 2048)\n"
      "  --overlap PIXELS   source overlap (default 64)\n"
      "  --output PATH      destination image or raw buffer\n"
      "  --format FORMAT    png, ppm, or rgb24 (otherwise inferred from extension)\n"
      "  --json             emit a gb.frames/v1 report to stdout\n"
      "  --force            replace an existing output file\n"
      "  --help             show this help and exit\n"
      "  --version          show the program version and exit\n";
}
std::string json_string(const std::string_view input) {
  std::ostringstream result;
  result << '"';
  for (const auto byte : input) {
    if (byte == '"') result << "\\\"";
    else if (byte == '\\') result << "\\\\";
    else if (byte == '\n') result << "\\n";
    else result << byte;
  }
  result << '"';
  return result.str();
}

template <typename Integer>
std::optional<Integer> integer(const std::string_view text) {
  try {
    std::size_t consumed{};
    const auto parsed = std::stoll(std::string(text), &consumed);
    if (consumed != text.size() || parsed < std::numeric_limits<Integer>::min() ||
        parsed > std::numeric_limits<Integer>::max()) return std::nullopt;
    return static_cast<Integer>(parsed);
  } catch (const std::exception &) { return std::nullopt; }
}

std::optional<long double> time_value(const std::string_view text) {
  try {
    long double result{};
    std::size_t start{};
    int fields{};
    while (true) {
      const auto separator = text.find(':', start);
      const auto part = text.substr(start, separator == std::string_view::npos
                                               ? text.size() - start
                                               : separator - start);
      std::size_t consumed{};
      const auto value = std::stold(std::string(part), &consumed);
      if (consumed != part.size() || value < 0.0L) return std::nullopt;
      result = result * 60.0L + value;
      ++fields;
      if (separator == std::string_view::npos) break;
      start = separator + 1U;
    }
    if (fields > 3 || !std::isfinite(result)) return std::nullopt;
    return result;
  } catch (const std::exception &) { return std::nullopt; }
}

std::int64_t ticks_at(const long double seconds, const gb::media::Rational base) {
  if (base.numerator <= 0 || base.denominator <= 0) throw std::runtime_error("invalid time base");
  const auto value = seconds * base.denominator / base.numerator;
  if (value > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
    throw std::runtime_error("requested time is out of range");
  }
  return static_cast<std::int64_t>(std::llround(value));
}

const gb::media::VideoStream *find_stream(const std::vector<gb::media::VideoStream> &streams,
                                          const int index) {
  const auto found = std::find_if(streams.begin(), streams.end(),
                                  [index](const auto &stream) { return stream.index == index; });
  return found == streams.end() ? nullptr : &*found;
}

void print_color_json(const gb::media::ColorMetadata &color) {
  std::cout << "{\"pixel_format\": " << json_string(color.pixel_format)
            << ", \"range\": " << json_string(color.range)
            << ", \"space\": " << json_string(color.space)
            << ", \"transfer\": " << json_string(color.transfer)
            << ", \"primaries\": " << json_string(color.primaries)
            << ", \"chroma_location\": " << json_string(color.chroma_location) << '}';
}

void print_streams(const Options &options, const std::vector<gb::media::VideoStream> &streams) {
  if (!options.json) {
    std::cout << "file: " << options.input.string() << "\nvideo streams: " << streams.size() << '\n';
    for (const auto &stream : streams) {
      std::cout << "  index=" << stream.index << " codec=" << stream.codec << " geometry="
                << stream.width << 'x' << stream.height << " time_base="
                << stream.time_base.numerator << '/' << stream.time_base.denominator
                << " duration_ticks=" << stream.duration << " pixel_format="
                << stream.color.pixel_format << " range=" << stream.color.range << '\n';
    }
    return;
  }
  std::cout << "{\n  \"schema\": \"gb.frames.streams/v1\",\n  \"path\": "
            << json_string(options.input.string()) << ",\n  \"video_streams\": [";
  for (std::size_t index = 0; index < streams.size(); ++index) {
    const auto &stream = streams[index];
    std::cout << (index == 0U ? "\n" : ",\n") << "    {\"index\": " << stream.index
              << ", \"codec\": " << json_string(stream.codec) << ", \"width\": "
              << stream.width << ", \"height\": " << stream.height
              << ", \"time_base\": {\"numerator\": " << stream.time_base.numerator
              << ", \"denominator\": " << stream.time_base.denominator
              << "}, \"start_time_ticks\": " << stream.start_time
              << ", \"duration_ticks\": " << stream.duration
              << ", \"average_frame_rate\": {\"numerator\": "
              << stream.average_frame_rate.numerator << ", \"denominator\": "
              << stream.average_frame_rate.denominator << "}, \"color\": ";
    print_color_json(stream.color);
    std::cout << '}';
  }
  std::cout << (streams.empty() ? "" : "\n  ") << "]\n}\n";
}

void print_result(const Options &options, const gb::media::RgbFrame &frame,
                  const std::vector<gb::media::RgbFrame> &sources) {
  if (!options.json) {
    std::cout << "output: " << options.output.string() << '\n'
              << "mode: " << (options.reconstruct ? "equirectangular reconstruction" : "decoded stream") << '\n'
              << "geometry: " << frame.width << 'x' << frame.height << " rgb24\n"
              << "selection: nearest presentation timestamp; earlier frame wins ties\n";
    for (const auto &source : sources) {
      std::cout << "source stream=" << source.stream_index << " requested_pts="
                << source.requested_pts << " selected_pts=" << source.pts << " duration_ticks="
                << source.duration << " time_base=" << source.time_base.numerator << '/'
                << source.time_base.denominator << " pixel_format="
                << source.source_color.pixel_format << '\n';
    }
    return;
  }
  std::cout << "{\n  \"schema\": \"gb.frames/v1\",\n  \"path\": "
            << json_string(options.input.string()) << ",\n  \"mode\": "
            << json_string(options.reconstruct ? "max_eac_to_equirectangular_cpu_reference" : "decoded_stream")
            << ",\n  \"selection_policy\": \"nearest presentation timestamp; earlier frame wins ties\",\n"
            << "  \"sources\": [";
  for (std::size_t index = 0; index < sources.size(); ++index) {
    const auto &source = sources[index];
    std::cout << (index == 0U ? "" : ", ") << "{\"stream_index\": " << source.stream_index
              << ", \"requested_pts\": " << source.requested_pts << ", \"selected_pts\": "
              << source.pts << ", \"duration_ticks\": " << source.duration
              << ", \"time_base\": {\"numerator\": " << source.time_base.numerator
              << ", \"denominator\": " << source.time_base.denominator << "}, \"color\": ";
    print_color_json(source.source_color);
    std::cout << '}';
  }
  std::cout << "],\n  \"output\": {\"path\": " << json_string(options.output.string())
            << ", \"format\": " << json_string(options.format) << ", \"width\": "
            << frame.width << ", \"height\": " << frame.height
            << ", \"pixel_format\": \"rgb24\", \"range\": \"pc\"}";
  if (options.reconstruct) {
    std::cout << ",\n  \"reconstruction\": {\"projection\": \"equirectangular\", \"overlap_pixels\": "
              << options.overlap << ", \"calibration\": \"layout transform and linear overlap blend only\"}";
  }
  std::cout << "\n}\n";
}

Options parse(const int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value = [&](const std::string_view option) -> std::string_view {
      if (++index >= argc) throw std::runtime_error(std::string(option) + " requires a value");
      return argv[index];
    };
    if (argument == "--list") options.list = true;
    else if (argument == "--json") options.json = true;
    else if (argument == "--reconstruct") options.reconstruct = true;
    else if (argument == "--force") options.force = true;
    else if (argument == "--stream") {
      const auto parsed = integer<int>(value(argument));
      if (!parsed.has_value() || *parsed < 0) throw std::runtime_error("invalid stream index");
      options.streams.push_back(*parsed);
    } else if (argument == "--time") {
      options.seconds = time_value(value(argument));
      if (!options.seconds.has_value()) throw std::runtime_error("invalid media time");
    } else if (argument == "--pts") {
      options.pts = integer<std::int64_t>(value(argument));
      if (!options.pts.has_value() || *options.pts < 0) throw std::runtime_error("invalid PTS");
    } else if (argument == "--output") options.output = value(argument);
    else if (argument == "--format") options.format = value(argument);
    else if (argument == "--width") {
      const auto parsed = integer<std::int32_t>(value(argument));
      if (!parsed.has_value()) throw std::runtime_error("invalid width");
      options.width = *parsed;
    } else if (argument == "--height") {
      const auto parsed = integer<std::int32_t>(value(argument));
      if (!parsed.has_value()) throw std::runtime_error("invalid height");
      options.height = *parsed;
    } else if (argument == "--overlap") {
      const auto parsed = integer<std::int32_t>(value(argument));
      if (!parsed.has_value()) throw std::runtime_error("invalid overlap");
      options.overlap = *parsed;
    } else if (argument == "--help" || argument == "--version") {
      continue;
    } else if (!argument.empty() && argument.front() == '-') {
      throw std::runtime_error("unknown option: " + std::string(argument));
    } else if (options.input.empty()) options.input = argument;
    else throw std::runtime_error("only one input file is accepted");
  }
  return options;
}

std::string infer_format(const std::filesystem::path &path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const unsigned char byte) { return static_cast<char>(std::tolower(byte)); });
  if (extension == ".png") return "png";
  if (extension == ".ppm") return "ppm";
  if (extension == ".rgb" || extension == ".rgb24" || extension == ".raw") return "rgb24";
  return {};
}

int media_exit(const gb::media::ErrorKind kind) {
  switch (kind) {
  case gb::media::ErrorKind::missing: return 3;
  case gb::media::ErrorKind::unsupported: return 4;
  case gb::media::ErrorKind::corrupt: return 5;
  case gb::media::ErrorKind::io: return 6;
  case gb::media::ErrorKind::invalid_argument: return 2;
  }
  return 1;
}

} // namespace

int main(const int argc, char **argv) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") { help(); return 0; }
    if (argument == "--version") { std::cout << "gb-frames " << GB_FRAMES_VERSION << '\n'; return 0; }
  }
  try {
    auto options = parse(argc, argv);
    if (options.input.empty()) throw std::runtime_error("an input file is required");
    const auto streams = gb::media::video_streams(options.input);
    if (options.list) {
      if (!options.output.empty() || options.reconstruct || options.pts.has_value() ||
          options.seconds.has_value() || !options.streams.empty() || !options.format.empty() ||
          options.force) {
        throw std::runtime_error("--list cannot be combined with extraction options");
      }
      print_streams(options, streams);
      return streams.empty() ? 4 : 0;
    }
    if (options.output.empty()) throw std::runtime_error("--output is required for extraction");
    std::error_code path_error;
    if (std::filesystem::exists(options.output, path_error) &&
        std::filesystem::equivalent(options.input, options.output, path_error)) {
      throw std::runtime_error("output must not replace the source media");
    }
    if (path_error) throw std::runtime_error("cannot validate output path: " + path_error.message());
    if (std::filesystem::exists(options.output) && !options.force) {
      throw std::runtime_error("output exists; use --force to replace it");
    }
    if (options.format.empty()) options.format = infer_format(options.output);
    if (options.format != "png" && options.format != "ppm" && options.format != "rgb24") {
      throw std::runtime_error("output format must be png, ppm, or rgb24");
    }
    if (options.seconds.has_value() && options.pts.has_value()) {
      throw std::runtime_error("--time and --pts are mutually exclusive");
    }
    if (options.reconstruct && options.pts.has_value()) {
      throw std::runtime_error("--pts is ambiguous in reconstruction mode; use --time");
    }
    if (options.reconstruct) {
      if (options.streams.empty()) {
        if (streams.size() != 2U) throw std::runtime_error("select exactly two video streams");
        options.streams = {streams[0].index, streams[1].index};
      }
      if (options.streams.size() != 2U || options.streams[0] == options.streams[1]) {
        throw std::runtime_error("--reconstruct requires two distinct --stream values");
      }
    } else {
      if (options.streams.empty()) {
        if (streams.empty()) throw gb::media::MediaError(gb::media::ErrorKind::unsupported, "no video streams");
        options.streams.push_back(streams.front().index);
      }
      if (options.streams.size() != 1U) throw std::runtime_error("select one stream or use --reconstruct");
    }

    std::vector<gb::media::RgbFrame> decoded;
    for (const auto stream_index : options.streams) {
      const auto *stream = find_stream(streams, stream_index);
      if (stream == nullptr) throw std::runtime_error("selected index is not a video stream");
      const auto requested = options.pts.value_or(ticks_at(options.seconds.value_or(0.0L), stream->time_base));
      decoded.push_back(gb::media::decode_frame(options.input, stream_index, requested));
    }
    auto result = options.reconstruct
                      ? gb::media::reconstruct_equirectangular(decoded[0], decoded[1],
                                                              options.width, options.height,
                                                              options.overlap)
                      : decoded[0];
    if (options.format == "png") gb::media::write_png(options.output, result);
    else if (options.format == "ppm") gb::media::write_ppm(options.output, result);
    else gb::media::write_rgb24(options.output, result);
    print_result(options, result, decoded);
    return 0;
  } catch (const gb::media::MediaError &error) {
    std::cerr << "gb-frames: " << gb::media::error_kind_name(error.kind()) << ": "
              << error.what() << '\n';
    return media_exit(error.kind());
  } catch (const std::exception &error) {
    std::cerr << "gb-frames: invalid_argument: " << error.what() << '\n';
    return 2;
  }
}
