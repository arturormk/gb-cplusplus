// gb-render: exports a framed video of a GoPro recording, with telemetry and map overlays over
// the picture or on a panel beside it (docs/plans/export/cli.md, settings.md).

#include "gb/export/export_settings.hpp"
#include "gb/export/framing.hpp"
#include "gb/export/settings.hpp"
#include "gb/geo/scale_curve.hpp"
#include "gb/geo/tile_cache.hpp"
#include "gb/geo/tiles.hpp"
#include "gb/media/audio.hpp"
#include "gb/media/playback.hpp"
#include "gb/media/position.hpp"
#include "gb/media/tempo.hpp"
#include "gb/media/timeline.hpp"
#include "gb/media/writer.hpp"
#include "gb/render/export_overlay.hpp"
#include "gb/render/frame_renderer.hpp"
#include "gb/render/still.hpp"
#include "gb/telemetry/telemetry.hpp"
#include "gb/tiles/tile_fetcher.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTimer>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <clocale>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

namespace ex = gb::exporting;
namespace fs = std::filesystem;

constexpr std::string_view kVersion = GB_RENDER_VERSION;
constexpr int kExitUsage = 2;
constexpr int kExitFailure = 1;
constexpr int kExitCancelled = 130;

std::atomic<bool> cancelled{false};

extern "C" void on_interrupt(int) { cancelled.store(true); }

class UsageError final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

struct Options {
  fs::path recording;
  std::vector<fs::path> settings_files;
  std::vector<std::string> assignments; // SECTION.KEY=VALUE from options and --set
  std::optional<double> duration_seconds;
  fs::path output;
  bool overwrite{};
  bool draft{};
  bool dry_run{};
  bool print_settings{};
  bool example_settings{};
  std::optional<fs::path> manifest;
  bool json{};
  std::string map_provider{"osm"};
  bool map_network{true};
  std::optional<fs::path> map_cache;
  int max_tiles{300};
  bool require_tiles{};
  int decoder_threads{0};
};

void help() {
  std::cout
      << "Usage: gb-render RECORDING [--settings FILE]... [options] -o OUTPUT.mp4\n"
         "       gb-render --example-settings\n"
         "       gb-render [--settings FILE]... [options] --print-settings\n\n"
         "Exports a framed video of a GoPro recording, with telemetry text and a route map over\n"
         "the picture or on a panel beside it. RECORDING is any chapter of the recording; a\n"
         "range may cross chapters. Settings files are gb.export/1 INI (see --example-settings);\n"
         "later files and options win, key by key.\n\n"
         "Range:\n"
         "  --start POS            c2:00:05:31.400, GH020073.MP4:00:05:31.4, or recording time\n"
         "  --end POS              end of the range (exclusive)\n"
         "  --duration SECONDS     preview: a short range from the start, instead of --end\n"
         "Framing and layout:\n"
         "  --view MODE            rectilinear, fisheye, stereographic, little-planet, panorama,\n"
         "                         raw-1, raw-2, original\n"
         "  --aspect W:H           output frame aspect ratio (16:9, 4:3, 1:1, 9:16, ...)\n"
         "  --width PIXELS         output frame width; the height follows\n"
         "  --placement WHERE      none, overlay, left, right, top or bottom\n"
         "  --map-width DISTANCE   ground distance across the map (800m, 1.2km); adds a map\n"
         "Output:\n"
         "  -o, --output FILE      .mp4 or .mkv\n"
         "  --codec h264|hevc      video codec (default h264)\n"
         "  --crf N                quality, 0..51, lower is better (default 20)\n"
         "  --encoder-preset NAME  ultrafast .. veryslow (default medium)\n"
         "  --no-audio             no audio track\n"
         "  --overwrite            replace an existing output\n"
         "Map tiles:\n"
         "  --map-provider ID      tile provider (default osm: OpenStreetMap)\n"
         "  --map-no-network       use cached tiles only; missing ones are placeholders\n"
         "  --map-cache DIR        tile cache directory\n"
         "  --max-tiles N          refuse to fetch more than N tiles (default 300)\n"
         "  --require-tiles        a tile that cannot be had is an error\n"
         "Other:\n"
         "  --settings FILE        a gb.export/1 settings file (repeatable)\n"
         "  --set SECTION.KEY=VALUE  override one setting (repeatable)\n"
         "  --draft                half width (a quarter of the pixels), fastest encoder preset\n"
         "  --dry-run              report what would be done, write nothing\n"
         "  --print-settings       print the combined settings and exit\n"
         "  --manifest FILE        write a JSON record of every frame written (gb.render.manifest/v1)\n"
         "  --json                 a JSON report on stdout (gb.render/v1); diagnostics stay on stderr\n"
         "  --example-settings     print a commented settings file and exit\n"
         "  --decoder-threads N    decoder threads (default: automatic)\n"
         "  --help, --version\n\n"
         "Exit status: 0 done, 1 failure, 2 usage or settings error, 130 cancelled.\n";
}

Options parse_arguments(const int argc, char **argv) {
  Options options;
  const auto require_value = [&](int &index, const std::string &name) -> std::string {
    if (index + 1 >= argc)
      throw UsageError(name + " requires a value");
    return argv[++index];
  };
  const auto integer = [](const std::string &text, const std::string &name) {
    try {
      std::size_t used{};
      const auto value = std::stoi(text, &used);
      if (used != text.size())
        throw std::invalid_argument(text);
      return value;
    } catch (const std::exception &) {
      throw UsageError(name + ": expected a whole number, got '" + text + "'");
    }
  };
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help" || argument == "-h") {
      help();
      std::exit(0);
    } else if (argument == "--version") {
      std::cout << "gb-render " << kVersion << '\n';
      std::exit(0);
    } else if (argument == "--settings") {
      options.settings_files.emplace_back(require_value(index, argument));
    } else if (argument == "--set") {
      options.assignments.push_back(require_value(index, argument));
    } else if (argument == "--start") {
      options.assignments.push_back("range.start=" + require_value(index, argument));
    } else if (argument == "--end") {
      options.assignments.push_back("range.end=" + require_value(index, argument));
    } else if (argument == "--duration") {
      const auto text = require_value(index, argument);
      try {
        std::size_t used{};
        options.duration_seconds = std::stod(text, &used);
        if (used != text.size() || !(*options.duration_seconds > 0.0))
          throw std::invalid_argument(text);
      } catch (const std::exception &) {
        throw UsageError("--duration: expected a positive number of seconds, got '" + text + "'");
      }
    } else if (argument == "--view") {
      options.assignments.push_back("view.mode=" + require_value(index, argument));
    } else if (argument == "--aspect") {
      options.assignments.push_back("frame.aspect=" + require_value(index, argument));
    } else if (argument == "--width") {
      options.assignments.push_back("frame.width=" + require_value(index, argument));
    } else if (argument == "--placement") {
      options.assignments.push_back("layout.placement=" + require_value(index, argument));
    } else if (argument == "--map-width") {
      options.assignments.push_back("map.width=" + require_value(index, argument));
    } else if (argument == "--codec") {
      options.assignments.push_back("output.codec=" + require_value(index, argument));
    } else if (argument == "--crf") {
      options.assignments.push_back("output.crf=" + require_value(index, argument));
    } else if (argument == "--encoder-preset") {
      options.assignments.push_back("output.encoder_preset=" + require_value(index, argument));
    } else if (argument == "--no-audio") {
      options.assignments.emplace_back("output.audio=no");
    } else if (argument == "-o" || argument == "--output") {
      options.output = require_value(index, argument);
    } else if (argument == "--overwrite") {
      options.overwrite = true;
    } else if (argument == "--draft") {
      options.draft = true;
    } else if (argument == "--dry-run") {
      options.dry_run = true;
    } else if (argument == "--print-settings") {
      options.print_settings = true;
    } else if (argument == "--manifest") {
      options.manifest = require_value(index, argument);
    } else if (argument == "--json") {
      options.json = true;
    } else if (argument == "--example-settings") {
      options.example_settings = true;
    } else if (argument == "--map-provider") {
      options.map_provider = require_value(index, argument);
      if (gb::geo::find_provider(options.map_provider) == nullptr)
        throw UsageError("unknown map provider: " + options.map_provider);
    } else if (argument == "--map-no-network") {
      options.map_network = false;
    } else if (argument == "--map-cache") {
      options.map_cache = require_value(index, argument);
    } else if (argument == "--max-tiles") {
      options.max_tiles = integer(require_value(index, argument), argument);
      if (options.max_tiles < 0)
        throw UsageError("--max-tiles must not be negative");
    } else if (argument == "--require-tiles") {
      options.require_tiles = true;
    } else if (argument == "--decoder-threads") {
      options.decoder_threads = integer(require_value(index, argument), argument);
    } else if (!argument.empty() && argument.front() == '-') {
      throw UsageError("unknown option: " + argument);
    } else if (options.recording.empty()) {
      options.recording = argument;
    } else {
      throw UsageError("only one recording is accepted");
    }
  }
  return options;
}

ex::SettingsSource read_settings_file(const fs::path &path) {
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error)
    throw UsageError("cannot read " + path.string() + ": " + error.message());
  if (size > ex::kMaximumSettingsFile)
    throw UsageError(path.string() + ": settings file is larger than 1 MiB");
  std::ifstream file(path, std::ios::binary);
  std::ostringstream text;
  text << file.rdbuf();
  if (!file && !file.eof())
    throw UsageError("cannot read " + path.string());
  try {
    return {path.string(), ex::parse_ini(text.str()), true};
  } catch (const ex::SettingsError &failure) {
    const std::string message = failure.what();
    // "line N: message" becomes "file:N: message".
    if (failure.line() > 0) {
      const auto colon = message.find(": ");
      throw UsageError(path.string() + ":" + std::to_string(failure.line()) + ": " +
                       message.substr(colon + 2U));
    }
    throw UsageError(path.string() + ": " + message);
  }
}

long double seconds(const std::int64_t ticks, const gb::media::Rational base) {
  return static_cast<long double>(ticks) * base.numerator / base.denominator;
}

std::string clock(const long double value) { return gb::media::format_clock(value); }

// "10×", "2.5×".
std::string speed_text(const double speed) {
  std::array<char, 32> text{};
  const auto end = std::to_chars(text.data(), text.data() + text.size(), speed).ptr;
  return std::string(text.data(), end) + "×";
}

// ---- telemetry ----------------------------------------------------------------------------------

// Where a GPS record comes from: its chapter and its time in that chapter's telemetry track.
struct RecordOrigin {
  int chapter{};
  std::int64_t media_time_ticks{};
};

// The GPS records of the chapters the range touches, on the recording timeline. `origins`
// receives, for each record, where it comes from.
std::vector<gb::telemetry::GpsPoint> load_track(const gb::media::RecordingTimeline &timeline,
                                                const std::int64_t start, const std::int64_t end,
                                                std::vector<std::string> &warnings,
                                                std::vector<RecordOrigin> &origins) {
  // Each chapter is read on its own thread: extraction reads every metadata sample of the file.
  std::vector<std::pair<const gb::media::TimelineChapter *, std::future<gb::telemetry::Extraction>>>
      pending;
  for (const auto &chapter : timeline.chapters) {
    if (chapter.offset_ticks + chapter.duration_ticks < start || chapter.offset_ticks > end)
      continue;
    pending.emplace_back(&chapter, std::async(std::launch::async, [path = chapter.path] {
      return gb::telemetry::extract(path);
    }));
  }
  std::vector<gb::telemetry::GpsPoint> points;
  for (auto &[chapter_pointer, future] : pending) {
    const auto &chapter = *chapter_pointer;
    gb::telemetry::Extraction extraction;
    try {
      extraction = future.get();
    } catch (const std::exception &error) {
      warnings.push_back(chapter.path.filename().string() + ": no GPS telemetry (" + error.what() +
                         ")");
      continue;
    }
    const auto base = extraction.time_base;
    constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    if (base.numerator == 0U || base.denominator == 0U || base.numerator > limit ||
        base.denominator > limit) {
      warnings.push_back(chapter.path.filename().string() + ": unusable telemetry time base");
      continue;
    }
    const gb::media::Rational telemetry_base{static_cast<std::int32_t>(base.numerator),
                                             static_cast<std::int32_t>(base.denominator)};
    for (auto point : extraction.gps) {
      const auto local_ticks = point.media_time_ticks;
      point.media_time_ticks =
          chapter.offset_ticks +
          gb::media::rescale_ticks(point.media_time_ticks, telemetry_base, timeline.time_base);
      if (!points.empty() && point.media_time_ticks < points.back().media_time_ticks)
        continue; // keep the records in order; out-of-order records would break the lookup
      points.push_back(point);
      origins.push_back({chapter.number, local_ticks});
    }
  }
  return points;
}

// ---- map tiles ----------------------------------------------------------------------------------

struct TileSet {
  std::map<gb::geo::TileKey, QImage> images;
  int unavailable{};
};

gb::render::MapScene base_scene(const gb::render::ExportTrack &track,
                                const gb::geo::TileProvider &provider) {
  gb::render::MapScene scene;
  scene.min_zoom = provider.min_zoom;
  scene.max_zoom = provider.max_zoom;
  if (!track.route().empty()) {
    scene.route = &track.route();
    scene.points = &track.route_points();
  }
  return scene;
}

// With an automatic scale, a record is shown from halfway after the previous one to halfway
// before the next, at the widths of the curve over that time: the ends and the middle are taken.
std::set<gb::geo::TileKey> needed_tiles(const ex::ExportSettings &settings,
                                        const ex::FrameLayout &layout,
                                        const gb::render::ExportTrack &track,
                                        const gb::geo::TileProvider &provider,
                                        const gb::geo::ScaleCurve *curve) {
  std::set<gb::geo::TileKey> keys;
  if (!track.has_usable_route())
    return keys;
  auto scene = base_scene(track, provider);
  const auto &points = track.route_points();
  for (const auto index : track.range_records()) {
    if (!points[index].usable)
      continue;
    scene.center = track.route()[index];
    if (curve == nullptr) {
      for (const auto key : gb::render::export_map_tiles(settings, layout, scene))
        keys.insert(key);
      continue;
    }
    const auto ticks = points[index].ticks;
    const auto before = index > 0U ? (points[index - 1U].ticks + ticks) / 2 : ticks;
    const auto after = index + 1U < points.size() ? (ticks + points[index + 1U].ticks) / 2 : ticks;
    for (const auto at : {before, ticks, after}) {
      for (const auto key :
           gb::render::export_map_tiles(settings, layout, scene, curve->width_at(at)))
        keys.insert(key);
    }
  }
  return keys;
}

TileSet load_tiles(const std::set<gb::geo::TileKey> &keys, const gb::geo::TileProvider &provider,
                   const Options &options, const std::optional<fs::path> &cache_root) {
  TileSet set;
  std::vector<gb::geo::TileKey> missing;
  if (cache_root) {
    const gb::geo::TileCache cache(*cache_root);
    for (const auto key : keys) {
      if (const auto tile = cache.load(provider, key); tile) {
        QImage image;
        if (image.loadFromData(tile->bytes.data(), static_cast<int>(tile->bytes.size()))) {
          set.images.emplace(key, image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
          continue;
        }
      }
      missing.push_back(key);
    }
  } else {
    missing.assign(keys.begin(), keys.end());
  }
  if (missing.empty())
    return set;
  if (!options.map_network) {
    set.unavailable = static_cast<int>(missing.size());
    return set;
  }
  if (static_cast<int>(missing.size()) > options.max_tiles) {
    throw std::runtime_error(
        "the map needs " + std::to_string(missing.size()) +
        " tiles that are not cached, more than "
        "--max-tiles " +
        std::to_string(options.max_tiles) +
        "; use a smaller map width or a shorter range, raise --max-tiles, or --map-no-network");
  }
  std::cerr << "gb-render: fetching " << missing.size() << " map tiles from " << provider.name
            << " (this reveals the route's area to the tile server)\n";
  auto fetcher_options =
      gb::tiles::application_fetcher_options("gb-render", kVersion, options.map_cache);
  gb::tiles::TileFetcher fetcher(provider, fetcher_options);
  std::set<gb::geo::TileKey> awaiting(missing.begin(), missing.end());
  QEventLoop loop;
  QTimer idle;
  idle.setSingleShot(true);
  idle.setInterval(60000);
  const auto answered = [&](const gb::geo::TileKey key, const QByteArray *data) {
    if (awaiting.erase(key) == 0U)
      return;
    QImage image;
    if (data != nullptr && image.loadFromData(*data))
      set.images.emplace(key, image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    else
      ++set.unavailable;
    std::cerr << "\rgb-render: map tiles " << (missing.size() - awaiting.size()) << "/"
              << missing.size() << std::flush;
    idle.start();
    if (awaiting.empty())
      loop.quit();
  };
  QObject::connect(&fetcher, &gb::tiles::TileFetcher::tileReady,
                   [&](const int z, const int x, const int y, const QByteArray &data) {
                     answered({z, x, y}, &data);
                   });
  QObject::connect(&fetcher, &gb::tiles::TileFetcher::tileUnavailable,
                   [&](const int z, const int x, const int y, const QString &) {
                     answered({z, x, y}, nullptr);
                   });
  QObject::connect(&idle, &QTimer::timeout, &loop, &QEventLoop::quit);
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &loop, [&loop] {
    if (cancelled.load())
      loop.quit();
  });
  poll.start(200);
  idle.start();
  fetcher.request(missing);
  loop.exec();
  std::cerr << '\n';
  if (cancelled.load())
    throw std::runtime_error("cancelled");
  set.unavailable += static_cast<int>(awaiting.size());
  if (const auto statistics = fetcher.statistics(); statistics.blocked)
    std::cerr << "gb-render: the tile server refused further requests: "
              << statistics.last_error.toStdString() << '\n';
  return set;
}

// ---- audio --------------------------------------------------------------------------------------

// The audio track of the output: source samples go to the writer as they are, or through a
// pitch-preserving time stretch when the export is faster than real time.
class AudioOut {
public:
  AudioOut(gb::media::VideoWriter &writer, const int rate, const double speed) : writer_(writer) {
    if (speed > 1.0)
      stretcher_ = std::make_unique<gb::media::TempoStretcher>(rate, speed);
  }

  void write(const float *samples, const std::int64_t frames) {
    if (!stretcher_) {
      writer_.write_audio(samples, frames);
      return;
    }
    stretcher_->push(samples, frames, stretched_);
    flush();
  }

  void finish() {
    if (!stretcher_)
      return;
    stretcher_->finish(stretched_);
    flush();
  }

private:
  void flush() {
    if (stretched_.empty())
      return;
    writer_.write_audio(stretched_.data(), static_cast<std::int64_t>(stretched_.size() / 2U));
    stretched_.clear();
  }

  gb::media::VideoWriter &writer_;
  std::unique_ptr<gb::media::TempoStretcher> stretcher_;
  std::vector<float> stretched_;
};

// Feeds the writer the recording's audio from the first frame on, chapter after chapter. Each
// chapter's audio is placed where its video starts on the timeline, so a chapter whose audio is a
// little shorter or longer than its video cannot shift the ones after it.
class AudioFeed {
public:
  AudioFeed(const gb::media::RecordingTimeline &timeline, const int stream, const int rate,
            const std::int64_t first_ticks)
      : timeline_(timeline), stream_(stream), rate_(rate), first_(first_ticks),
        chapter_(timeline.chapter_at(first_ticks)) {}

  void feed_until(const std::int64_t end_ticks, AudioOut &writer) {
    const auto target = sample_at(end_ticks);
    while (written_ < target && chapter_ < timeline_.chapters.size()) {
      const auto chapter_start = sample_at(timeline_.chapters[chapter_].offset_ticks);
      const auto chapter_end = chapter_ + 1U < timeline_.chapters.size()
                                   ? sample_at(timeline_.chapters[chapter_ + 1U].offset_ticks)
                                   : std::numeric_limits<std::int64_t>::max();
      if (written_ >= chapter_end) {
        next_chapter();
        continue;
      }
      if (written_ < chapter_start) {
        write_silence(std::min(chapter_start, target) - written_, writer);
        continue;
      }
      if (!reader_ && !open(written_ - chapter_start)) {
        // No audio in this chapter: silence until the next one.
        write_silence(std::min(chapter_end, target) - written_, writer);
        continue;
      }
      if (buffer_.empty()) {
        auto block = reader_->next();
        if (!block) {
          if (chapter_ + 1U >= timeline_.chapters.size()) {
            chapter_ = timeline_.chapters.size(); // the recording's audio ends here
            break;
          }
          write_silence(std::min(chapter_end, target) - written_, writer);
          continue;
        }
        // Drop what precedes the wanted sample (the seek is accurate, so normally nothing).
        const auto wanted = written_ - chapter_start;
        const auto skip =
            std::clamp<std::int64_t>(wanted - block->first_sample, 0, block->frames());
        buffer_.assign(block->samples.begin() + static_cast<std::ptrdiff_t>(skip * 2),
                       block->samples.end());
        offset_ = 0;
        if (buffer_.empty())
          continue;
      }
      const auto available = static_cast<std::int64_t>(buffer_.size() / 2U) - offset_;
      const auto count = std::min({available, target - written_, chapter_end - written_});
      writer.write(buffer_.data() + offset_ * 2, count);
      written_ += count;
      offset_ += count;
      if (offset_ * 2 >= static_cast<std::int64_t>(buffer_.size())) {
        buffer_.clear();
        offset_ = 0;
      }
    }
  }

private:
  std::int64_t sample_at(const std::int64_t ticks) const {
    return static_cast<std::int64_t>(
        std::llround(seconds(ticks - first_, timeline_.time_base) * rate_));
  }

  bool open(const std::int64_t local_sample) {
    try {
      reader_ =
          std::make_unique<gb::media::AudioReader>(timeline_.chapters[chapter_].path, stream_);
      if (reader_->sample_rate() != rate_) {
        reader_.reset();
        return false;
      }
      if (local_sample > 0)
        reader_->seek(local_sample);
      buffer_.clear();
      offset_ = 0;
      return true;
    } catch (const std::exception &) {
      reader_.reset();
      return false;
    }
  }

  void next_chapter() {
    ++chapter_;
    reader_.reset();
    buffer_.clear();
    offset_ = 0;
  }

  void write_silence(const std::int64_t frames, AudioOut &writer) {
    if (frames <= 0)
      return;
    const std::vector<float> silence(static_cast<std::size_t>(frames) * 2U, 0.0F);
    writer.write(silence.data(), frames);
    written_ += frames;
  }

  const gb::media::RecordingTimeline &timeline_;
  int stream_;
  int rate_;
  std::int64_t first_;
  std::size_t chapter_;
  std::unique_ptr<gb::media::AudioReader> reader_;
  std::vector<float> buffer_;
  std::int64_t offset_{};
  std::int64_t written_{};
};

// ---- the export ---------------------------------------------------------------------------------

gb::render::ViewParameters view_parameters(const ex::ExportSettings &settings,
                                           const gb::media::PlaybackLayout &layout) {
  constexpr auto radians = std::numbers::pi / 180.0;
  gb::render::ViewParameters view;
  view.mode = settings.mode;
  view.layout = layout.layout == gb::media::SourceLayout::max_dual_eac
                    ? gb::render::MaxDualEacLayout
                : layout.layout == gb::media::SourceLayout::equirectangular
                    ? gb::render::EquirectangularLayout
                    : gb::render::FlatLayout;
  view.overlap = static_cast<float>(layout.overlap);
  view.yaw = static_cast<float>(settings.yaw * radians);
  view.pitch = static_cast<float>(settings.pitch * radians);
  view.roll = static_cast<float>(settings.roll * radians);
  view.field_of_view = static_cast<float>(settings.field_of_view * radians);
  view.zoom = static_cast<float>(settings.zoom);
  view.pan_x = static_cast<float>(settings.pan_x);
  view.pan_y = static_cast<float>(settings.pan_y);
  view.canvas = QSizeF(settings.canvas_aspect(), 1.0);
  const auto crop = settings.crop_or_whole();
  view.view_crop = QRectF(crop.x, crop.y, crop.width, crop.height);
  return view;
}

void check_view(const ex::ExportSettings &settings, const gb::media::PlaybackLayout &layout) {
  const auto flat = layout.layout == gb::media::SourceLayout::flat;
  if (flat && (ex::is_spherical_mode(settings.mode) || settings.mode == 4)) {
    throw UsageError("the view " + ex::view_mode_name(settings.mode) +
                     " needs a 360° recording; this one is flat (use original)");
  }
  if (settings.mode == 6 && layout.streams.size() < 2U)
    throw UsageError("the view raw-2 needs a recording with two video streams");
}

std::string size_text(const std::uintmax_t bytes) {
  std::ostringstream out;
  out.precision(1);
  out << std::fixed;
  if (bytes >= 1024U * 1024U * 1024U)
    out << static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0) << " GiB";
  else
    out << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MiB";
  return out.str();
}

// ---- JSON ---------------------------------------------------------------------------------------

std::string json_string(const std::string_view input) {
  std::string output;
  output.reserve(input.size() + 2U);
  output += '"';
  for (const char character : input) {
    const auto byte = static_cast<unsigned char>(character);
    if (character == '"' || character == '\\') {
      output += '\\';
      output += character;
    } else if (character == '\n') {
      output += "\\n";
    } else if (character == '\t') {
      output += "\\t";
    } else if (byte < 0x20U) {
      std::array<char, 8> escaped{};
      std::snprintf(escaped.data(), escaped.size(), "\\u%04x", byte);
      output += escaped.data();
    } else {
      output += character;
    }
  }
  output += '"';
  return output;
}

std::string json_path(const fs::path &path) { return json_string(path.string()); }

// The shortest text that reads back as the same double; null when not finite.
std::string json_number(const double value) {
  if (!std::isfinite(value))
    return "null";
  std::array<char, 32> text{};
  const auto end = std::to_chars(text.data(), text.data() + text.size(), value).ptr;
  return {text.data(), end};
}

std::string json_rational(const gb::media::Rational value) {
  return "{\"numerator\": " + std::to_string(value.numerator) +
         ", \"denominator\": " + std::to_string(value.denominator) + "}";
}

std::string json_strings(const std::vector<std::string> &values, const std::size_t from = 0U) {
  std::string output = "[";
  for (auto index = from; index < values.size(); ++index)
    output += (index == from ? "" : ", ") + json_string(values[index]);
  return output + "]";
}

// The per-frame record of --manifest: written as the frames are, one line each, and removed
// unless the export completes.
class Manifest {
public:
  Manifest(fs::path path, const bool overwrite) : path_(std::move(path)) {
    if (fs::exists(path_) && !overwrite)
      throw UsageError(path_.string() + " exists; use --overwrite to replace it");
    file_.open(path_, std::ios::binary | std::ios::trunc);
    if (!file_)
      throw std::runtime_error("cannot write " + path_.string());
  }
  Manifest(const Manifest &) = delete;
  Manifest &operator=(const Manifest &) = delete;
  ~Manifest() {
    if (!kept_) {
      file_.close();
      std::error_code ignored;
      fs::remove(path_, ignored);
    }
  }

  std::ofstream &out() { return file_; }

  void frame(const std::string &line) {
    file_ << (frames_ == 0 ? "\n    " : ",\n    ") << line;
    ++frames_;
  }

  void finish() {
    file_ << (frames_ == 0 ? "]\n}\n" : "\n  ]\n}\n");
    file_.close();
    if (!file_)
      throw std::runtime_error("cannot write " + path_.string());
    kept_ = true;
  }

  [[nodiscard]] const fs::path &path() const { return path_; }

private:
  fs::path path_;
  std::ofstream file_;
  long long frames_{};
  bool kept_{};
};

int run(const Options &options, int argc, char **argv) {
  if ((options.json || options.manifest) && (options.example_settings || options.print_settings))
    throw UsageError("--json and --manifest report an export; they cannot be combined with "
                     "--example-settings or --print-settings");
  if (options.manifest && options.dry_run)
    throw UsageError("--manifest records the frames of an export; it cannot be combined with "
                     "--dry-run");
  if (options.example_settings) {
    std::cout << ex::example_settings();
    return 0;
  }
  std::vector<ex::SettingsSource> sources;
  for (const auto &file : options.settings_files)
    sources.push_back(read_settings_file(file));
  std::vector<std::string> warnings;
  ex::ExportSettings settings;
  try {
    if (!options.assignments.empty())
      sources.push_back(ex::override_source("command line", options.assignments));
    settings = ex::combine_settings(sources, &warnings);
  } catch (const ex::SettingsValidationError &error) {
    throw UsageError(error.what());
  }
  if (options.draft) {
    settings.width = std::max(16, settings.width / 2);
    settings.output.encoder_preset = "ultrafast";
  }
  if (options.print_settings) {
    std::cout << ex::write_export_settings(
        settings, {"gb-render " + std::string(kVersion) + ": combined settings"});
    return 0;
  }
  if (options.recording.empty())
    throw UsageError("no recording given (see --help)");
  if (options.output.empty() && !options.dry_run)
    throw UsageError("no output given: -o FILE.mp4 or .mkv");
  if (!options.output.empty()) {
    auto extension = options.output.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".mp4" && extension != ".mkv")
      throw UsageError("the output must end in .mp4 or .mkv");
  }

  // Qt for fonts, painting and offscreen OpenGL; no window is ever shown.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication application(argc, argv);
  // Qt adopts the user's locale; the parsers and the settings use the C locale, as gb-player does.
  std::setlocale(LC_NUMERIC, "C");
  QCoreApplication::setApplicationName(QStringLiteral("gb-render"));
  QCoreApplication::setApplicationVersion(
      QString::fromUtf8(kVersion.data(), static_cast<qsizetype>(kVersion.size())));

  const auto zone = gb::render::time_zone_for(settings.units.time_zone);
  if (!zone.isValid())
    throw UsageError("[units] time_zone: unknown time zone '" + settings.units.time_zone + "'");

  const auto timeline = gb::media::open_timeline(options.recording);
  for (const auto &chapter : timeline.chapters) {
    if (!options.output.empty() && fs::exists(options.output) &&
        fs::equivalent(chapter.path, options.output)) {
      throw UsageError("refusing to overwrite the recording " + chapter.path.filename().string());
    }
    if (options.manifest && fs::exists(*options.manifest) &&
        fs::equivalent(chapter.path, *options.manifest)) {
      throw UsageError("refusing to overwrite the recording " + chapter.path.filename().string());
    }
  }
  if (options.manifest && !options.output.empty() && fs::exists(*options.manifest) &&
      fs::exists(options.output) && fs::equivalent(*options.manifest, options.output))
    throw UsageError("the manifest and the output must be different files");
  const auto &source_layout = timeline.chapters.front().layout;
  check_view(settings, source_layout);
  // A flat picture (the source itself, or the 2:1 panorama) with no framing given fills the frame:
  // the canvas is the picture's own shape and the crop its largest centred part of the frame's
  // shape. gb-player's Capture mode always writes both, so its framing is kept exactly.
  std::string framing_note;
  if (!settings.crop && !settings.canvas) {
    const auto effective =
        source_layout.layout == gb::media::SourceLayout::flat ? 7 : settings.mode;
    if (effective >= 4) {
      const auto &stream = timeline.chapters.front()
                               .streams[static_cast<std::size_t>(source_layout.streams.front())];
      const auto content =
          effective == 4 ? 2.0 : static_cast<double>(stream.width) / std::max(1, stream.height);
      settings.canvas = content;
      settings.crop = ex::fit_crop(content, settings.picture_aspect(), 0.5, 0.5, 1.0);
      const auto &crop = *settings.crop;
      if (crop.width < 0.999 || crop.height < 0.999) {
        std::ostringstream note;
        note.precision(0);
        note << std::fixed << "  framing the picture fills the frame: the middle "
             << crop.width * 100.0 << " % × " << crop.height * 100.0
             << " % of the source (set [frame] crop to choose)\n";
        framing_note = note.str();
      }
    }
  }
  const auto layout = settings.frame_layout();

  // The range.
  const auto resolve = [&timeline](const std::string &text, const std::string &what) {
    const auto position = gb::media::parse_position(text);
    if (!position)
      throw UsageError(what + ": cannot read the position '" + text + "'");
    try {
      return gb::media::resolve_position(timeline, *position);
    } catch (const gb::media::MediaError &error) {
      throw UsageError(what + ": " + error.what());
    }
  };
  const auto start = settings.start ? resolve(*settings.start, "start") : 0;
  auto end = settings.end ? resolve(*settings.end, "end") : timeline.duration_ticks;
  if (options.duration_seconds) {
    end =
        std::min(timeline.duration_ticks,
                 start + gb::media::rescale_ticks(std::llround(*options.duration_seconds * 1000.0),
                                                  {1, 1000}, timeline.time_base));
  }
  if (end <= start)
    throw UsageError("the range is empty: the end must come after the start");
  const auto rate = timeline.frame_rate;
  const auto speed = settings.output.speed;
  const auto frames_expected = static_cast<long long>(std::llround(
      seconds(end - start, timeline.time_base) * rate.numerator / rate.denominator / speed));

  // Telemetry and the map.
  const auto overlays = settings.placement != ex::Placement::none;
  const auto needs_track = overlays && (settings.map.enabled || settings.telemetry);
  gb::render::ExportTrack track;
  std::vector<RecordOrigin> origins;
  // The manifest names the GPS record each frame shows, or would show, even without overlays.
  if (needs_track || options.manifest) {
    track = gb::render::ExportTrack(load_track(timeline, start, end, warnings, origins), start,
                                    end, static_cast<double>(seconds(1, timeline.time_base)));
    if (needs_track && !track.has_usable_route())
      warnings.emplace_back("no valid GPS fix in the range: telemetry shows \"—\"");
  }
  const auto &provider = *gb::geo::find_provider(options.map_provider);
  const auto attribution = QString::fromStdString(provider.attribution);
  const auto cache_root =
      options.map_cache ? options.map_cache : gb::geo::default_tile_cache_root();
  std::optional<gb::geo::ScaleCurve> scale_curve;
  if (overlays && settings.map.enabled && settings.map.auto_width) {
    scale_curve.emplace(track.all_route_points(), start, end, track.seconds_per_tick(),
                        gb::geo::ScaleCurveSettings{settings.map.ahead_seconds,
                                                    settings.map.closest_metres,
                                                    settings.map.widest_metres});
  }
  std::set<gb::geo::TileKey> tile_keys;
  if (overlays && settings.map.enabled)
    tile_keys =
        needed_tiles(settings, layout, track, provider, scale_curve ? &*scale_curve : nullptr);

  // Audio.
  std::optional<gb::media::AudioStream> audio;
  const auto audio_too_fast = settings.output.audio && speed > ex::kMaxAudioSpeed;
  if (settings.output.audio && !audio_too_fast) {
    try {
      const auto streams = gb::media::audio_streams(timeline.chapters.front().path);
      const auto selection = gb::media::select_playback_audio(streams);
      if (selection.stream) {
        for (const auto &stream : streams) {
          if (stream.index == *selection.stream)
            audio = stream;
        }
      } else {
        warnings.push_back("no audio: " + selection.description);
      }
    } catch (const std::exception &error) {
      warnings.push_back(std::string("no audio: ") + error.what());
    }
  }

  const auto range_text = gb::media::format_recording_position(timeline, start) + " to " +
                          gb::media::format_recording_position(timeline, end);
  std::cerr << "gb-render: " << options.recording.filename().string() << " ("
            << timeline.chapters.size()
            << (timeline.chapters.size() == 1U ? " chapter, " : " chapters, ")
            << source_layout.description << ")\n"
            << "  range   " << range_text << " (" << clock(seconds(end - start, timeline.time_base))
            << ", about " << frames_expected << " frames at "
            << static_cast<double>(rate.numerator) / rate.denominator << " fps"
            << (speed > 1.0 ? ", " + speed_text(speed) + " real time: " +
                                  clock(seconds(end - start, timeline.time_base) / speed)
                            : std::string())
            << ")\n"
            << "  output  " << layout.width << "×" << layout.height
            << (layout.panel ? " with a " + ex::placement_name(settings.placement) + " panel" : "")
            << ", " << (settings.output.codec == ex::Codec::hevc ? "hevc" : "h264") << " crf "
            << settings.output.crf << " " << settings.output.encoder_preset
            << (audio            ? (speed > 1.0 ? ", audio at its pitch" : ", audio")
                : audio_too_fast ? ", no audio (faster than " + speed_text(ex::kMaxAudioSpeed) + ")"
                                 : ", no audio")
            << '\n';
  std::cerr << framing_note;
  if (needs_track)
    std::cerr << "  GPS     " << track.points().size() << " records, "
              << track.route_points().size() << " in the range\n";
  const auto map_shown = overlays && settings.map.enabled;
  int cached = 0;
  std::set<int> zooms;
  if (map_shown) {
    if (cache_root) {
      const gb::geo::TileCache cache(*cache_root);
      for (const auto key : tile_keys)
        cached += cache.load(provider, key).has_value() ? 1 : 0;
    }
    for (const auto key : tile_keys)
      zooms.insert(key.zoom);
    std::cerr << "  map     ";
    if (scale_curve) {
      const auto closest = scale_curve->closest();
      const auto widest = scale_curve->widest();
      std::cerr << "auto, " << ex::minutes_text(settings.map.ahead_seconds)
                << " ahead: " << ex::distance_text(closest.metres) << " across at "
                << gb::media::format_recording_position(timeline, closest.ticks) << " to "
                << ex::distance_text(widest.metres) << " at "
                << gb::media::format_recording_position(timeline, widest.ticks) << "\n"
                << "          ";
    } else {
      std::cerr << ex::distance_text(settings.map.width_metres) << " across, ";
    }
    std::cerr << tile_keys.size() << " tiles (zoom";
    for (const auto zoom : zooms)
      std::cerr << ' ' << zoom;
    std::cerr << "), " << cached << " cached, "
              << (options.map_network
                      ? std::to_string(tile_keys.size() - static_cast<std::size_t>(cached)) +
                            " to fetch"
                      : std::string("no network"))
              << '\n';
  }
  for (const auto &warning : warnings)
    std::cerr << "gb-render: warning: " << warning << '\n';

  // The --json report: what would be done, and after an export, what was.
  const auto json_report = [&](const std::string &result) {
    const auto position = [&timeline](const std::int64_t ticks) {
      return json_string(gb::media::format_recording_position(timeline, ticks));
    };
    std::ostringstream out;
    out << "{\n  \"schema\": \"gb.render/v1\",\n  \"version\": " << json_string(kVersion)
        << ",\n  \"recording\": " << json_path(options.recording)
        << ",\n  \"layout\": " << json_string(source_layout.description)
        << ",\n  \"chapters\": [";
    for (std::size_t index = 0; index < timeline.chapters.size(); ++index) {
      const auto &chapter = timeline.chapters[index];
      out << (index == 0U ? "\n    " : ",\n    ") << "{\"number\": " << chapter.number
          << ", \"path\": " << json_path(chapter.path) << ", \"offset_ticks\": "
          << chapter.offset_ticks << ", \"duration_ticks\": " << chapter.duration_ticks << '}';
    }
    out << "\n  ],\n  \"time_base\": " << json_rational(timeline.time_base)
        << ",\n  \"frame_rate\": " << json_rational(rate) << ",\n  \"range\": {\"start\": "
        << position(start) << ", \"end\": " << position(end) << ", \"start_ticks\": " << start
        << ", \"end_ticks\": " << end << ", \"seconds\": "
        << json_number(static_cast<double>(seconds(end - start, timeline.time_base)))
        << "},\n  \"speed\": " << json_number(speed)
        << ",\n  \"frames_expected\": " << frames_expected << ",\n  \"output\": {\"path\": "
        << (options.output.empty() ? std::string("null") : json_path(options.output))
        << ", \"width\": " << layout.width << ", \"height\": " << layout.height
        << ", \"placement\": " << json_string(ex::placement_name(settings.placement))
        << ", \"panel\": " << (layout.panel ? "true" : "false") << ", \"codec\": "
        << json_string(settings.output.codec == ex::Codec::hevc ? "hevc" : "h264")
        << ", \"crf\": " << settings.output.crf
        << ", \"encoder_preset\": " << json_string(settings.output.encoder_preset)
        << ", \"audio\": " << (audio ? "true" : "false") << ", \"draft\": "
        << (options.draft ? "true" : "false") << "},\n  \"gps\": ";
    if (needs_track || options.manifest) {
      out << "{\"records\": " << track.points().size()
          << ", \"in_range\": " << track.route_points().size()
          << ", \"usable\": " << (track.has_usable_route() ? "true" : "false") << '}';
    } else {
      out << "null";
    }
    out << ",\n  \"map\": ";
    if (map_shown) {
      out << "{\"provider\": " << json_string(provider.id) << ", \"scale\": ";
      if (scale_curve) {
        const auto closest = scale_curve->closest();
        const auto widest = scale_curve->widest();
        out << "\"auto\", \"ahead_seconds\": " << json_number(settings.map.ahead_seconds)
            << ", \"closest\": {\"metres\": " << json_number(closest.metres)
            << ", \"position\": " << position(closest.ticks)
            << "}, \"widest\": {\"metres\": " << json_number(widest.metres)
            << ", \"position\": " << position(widest.ticks) << '}';
      } else {
        out << "\"fixed\", \"width_metres\": " << json_number(settings.map.width_metres);
      }
      out << ", \"tiles\": " << tile_keys.size() << ", \"zooms\": [";
      for (auto zoom = zooms.begin(); zoom != zooms.end(); ++zoom)
        out << (zoom == zooms.begin() ? "" : ", ") << *zoom;
      out << "], \"cached\": " << cached << ", \"to_fetch\": "
          << (options.map_network ? std::to_string(tile_keys.size() - static_cast<std::size_t>(cached))
                                  : std::string("null"))
          << ", \"network\": " << (options.map_network ? "true" : "false") << '}';
    } else {
      out << "null";
    }
    out << ",\n  \"dry_run\": " << (options.dry_run ? "true" : "false")
        << ",\n  \"result\": " << result << ",\n  \"warnings\": " << json_strings(warnings)
        << "\n}\n";
    std::cout << out.str() << std::flush;
  };

  if (options.dry_run) {
    if (options.json)
      json_report("null");
    return 0;
  }
  auto printed_warnings = warnings.size();

  if (fs::exists(options.output) && !options.overwrite)
    throw UsageError(options.output.string() + " exists; use --overwrite to replace it");
  std::optional<Manifest> manifest;
  if (options.manifest) {
    manifest.emplace(*options.manifest, options.overwrite);
    auto &out = manifest->out();
    out << "{\n  \"schema\": \"gb.render.manifest/v1\",\n  \"version\": " << json_string(kVersion)
        << ",\n  \"recording\": " << json_path(options.recording)
        << ",\n  \"output\": " << json_path(options.output) << ",\n  \"chapters\": [";
    for (std::size_t index = 0; index < timeline.chapters.size(); ++index) {
      const auto &chapter = timeline.chapters[index];
      out << (index == 0U ? "\n    " : ",\n    ") << "{\"number\": " << chapter.number
          << ", \"path\": " << json_path(chapter.path)
          << ", \"offset_ticks\": " << chapter.offset_ticks << '}';
    }
    out << "\n  ],\n  \"time_base\": " << json_rational(timeline.time_base)
        << ",\n  \"frame_rate\": " << json_rational(rate) << ",\n  \"speed\": "
        << json_number(speed) << ",\n  \"frames\": [";
  }

  TileSet tiles;
  if (!tile_keys.empty()) {
    tiles = load_tiles(tile_keys, provider, options, cache_root);
    if (tiles.unavailable > 0) {
      const auto message =
          std::to_string(tiles.unavailable) + " map tiles not available, drawn as placeholders";
      if (options.require_tiles)
        throw std::runtime_error(message + " (--require-tiles)");
      std::cerr << "gb-render: warning: " << message << '\n';
      warnings.push_back(message);
      printed_warnings = warnings.size();
    }
  }

  // Render and encode.
  gb::render::OffscreenRenderer renderer;
  if (!renderer.initialize())
    throw std::runtime_error("offscreen rendering is unavailable: " + renderer.error());
  const auto view = view_parameters(settings, source_layout);
  const auto partial = options.output.parent_path() / (options.output.stem().string() + ".partial" +
                                                       options.output.extension().string());
  struct Cleanup {
    fs::path path;
    bool keep{};
    ~Cleanup() {
      if (!keep) {
        std::error_code ignored;
        fs::remove(path, ignored);
      }
    }
  } cleanup{partial};

  gb::media::VideoWriterOptions writer_options;
  writer_options.width = layout.width;
  writer_options.height = layout.height;
  writer_options.time_base = timeline.time_base;
  writer_options.frame_rate = rate;
  writer_options.codec = settings.output.codec == ex::Codec::hevc ? gb::media::VideoCodec::hevc
                                                                  : gb::media::VideoCodec::h264;
  writer_options.crf = settings.output.crf;
  writer_options.preset = settings.output.encoder_preset;
  if (audio)
    writer_options.audio_sample_rate = audio->sample_rate;
  writer_options.audio_bitrate = settings.output.audio_bitrate;
  gb::media::VideoWriter writer(partial, writer_options);

  auto scene = base_scene(track, provider);
  scene.tile = [&tiles](const gb::geo::TileKey key) -> const QImage * {
    const auto found = tiles.images.find(key);
    return found == tiles.images.end() ? nullptr : &found->second;
  };
  gb::render::CourseFilter course_filter;
  std::unique_ptr<AudioOut> audio_out;
  std::unique_ptr<AudioFeed> audio_feed;
  // Faster than real time, output frame k shows the first source frame at or after
  // k × speed frame periods, and is written at k frame periods.
  const auto period_ticks =
      static_cast<long double>(timeline.time_base.denominator) * rate.denominator /
      (static_cast<long double>(timeline.time_base.numerator) * rate.numerator);
  std::int64_t next_output = 0;

  auto chapter = timeline.chapter_at(start);
  auto reader = std::make_unique<gb::media::SynchronizedReader>(
      timeline.chapters[chapter].path, source_layout.streams, options.decoder_threads);
  reader->seek(start - timeline.chapters[chapter].offset_ticks);

  std::optional<std::int64_t> first;
  std::int64_t last{};
  long long written = 0;
  int missing_tiles_max = 0;
  QElapsedTimer clock_timer;
  clock_timer.start();
  qint64 reported = -1000;
  const auto interactive = isatty(STDERR_FILENO) != 0;
  QImage frame(layout.width, layout.height, QImage::Format_RGB32);
  while (!cancelled.load()) {
    auto set = reader->next();
    if (!set) {
      if (++chapter >= timeline.chapters.size())
        break;
      reader = std::make_unique<gb::media::SynchronizedReader>(
          timeline.chapters[chapter].path, source_layout.streams, options.decoder_threads);
      continue;
    }
    const auto ticks = timeline.chapters[chapter].offset_ticks +
                       gb::media::rescale_ticks(set->pts, set->time_base, timeline.time_base);
    if (ticks < start || (first && ticks <= last))
      continue;
    if (ticks >= end)
      break;
    std::int64_t pts = 0;
    if (speed > 1.0 && first) {
      const auto slot = static_cast<std::int64_t>(
          std::floor(static_cast<long double>(ticks - *first) / (period_ticks * speed) + 1e-6L));
      if (slot < next_output) {
        last = ticks;
        continue;
      }
      next_output = slot;
      pts = std::llround(static_cast<long double>(slot) * period_ticks);
    } else if (first) {
      pts = ticks - *first;
    }
    if (!first) {
      first = ticks;
      if (audio) {
        audio_out = std::make_unique<AudioOut>(writer, audio->sample_rate, speed);
        audio_feed = std::make_unique<AudioFeed>(timeline, audio->index, audio->sample_rate, ticks);
      }
    }
    ++next_output;
    last = ticks;

    const auto picture =
        renderer.render(*set, view, QSize(layout.picture.width, layout.picture.height));
    frame.fill(Qt::black);
    QPainter painter(&frame);
    painter.drawImage(QPoint(layout.picture.x, layout.picture.y), picture);
    if (overlays) {
      gb::render::OverlayFrame overlay;
      const auto *fix = track.fix_at(ticks);
      overlay.fields = gb::render::field_texts(settings, zone, fix, track.distance_at(ticks));
      overlay.scene = scene;
      if (scale_curve)
        overlay.map_width_metres = scale_curve->width_at(ticks);
      const auto time = static_cast<double>(seconds(ticks, timeline.time_base));
      std::optional<double> course;
      if (fix != nullptr) {
        if (const auto index = track.nearest(ticks); index) {
          if (const auto azimuth = gb::telemetry::motion_azimuth_degrees(track.points(), *index))
            course = static_cast<double>(*azimuth);
        }
      }
      overlay.scene.course = course_filter.update(course, time);
      if (!fix)
        overlay.scene.course.reset();
      if (const auto index = track.route_index(ticks); index) {
        overlay.scene.record = *index;
        if (const auto anchor = track.route_anchor(*index); anchor)
          overlay.scene.center = track.route()[*anchor];
        overlay.scene.marker =
            fix != nullptr ? gb::render::MapMarker::valid : gb::render::MapMarker::none;
      }
      const auto result =
          gb::render::paint_export_overlays(painter, settings, layout, overlay, attribution);
      missing_tiles_max = std::max(missing_tiles_max, result.missing_tiles);
    }
    painter.end();
    writer.write_video(frame.constBits(), static_cast<int>(frame.bytesPerLine()), pts);
    if (manifest) {
      const auto &source = timeline.chapters[chapter];
      std::ostringstream line;
      line << "{\"frame\": " << written << ", \"pts\": " << pts << ", \"source_ticks\": " << ticks
           << ", \"chapter\": " << source.number
           << ", \"chapter_ticks\": " << ticks - source.offset_ticks << ", \"position\": "
           << json_string(gb::media::format_recording_position(timeline, ticks)) << ", \"gps\": ";
      if (const auto index = track.nearest(ticks); index) {
        const auto &point = track.points()[*index];
        const auto &origin = origins[*index];
        line << "{\"record\": " << *index << ", \"chapter\": " << origin.chapter
             << ", \"media_time_ticks\": " << origin.media_time_ticks
             << ", \"time_base\": {\"numerator\": " << point.time_base.numerator
             << ", \"denominator\": " << point.time_base.denominator << "}, \"utc\": "
             << (point.utc ? json_string(*point.utc) : std::string("null"))
             << ", \"validity\": " << json_string(gb::telemetry::validity_name(point.validity))
             << ", \"shown\": " << (track.fix_at(ticks) != nullptr ? "true" : "false") << '}';
      } else {
        line << "null";
      }
      if (scale_curve)
        line << ", \"map_width_metres\": " << json_number(scale_curve->width_at(ticks));
      line << '}';
      manifest->frame(line.str());
    }
    if (audio_feed) {
      const auto duration =
          set->duration > 0
              ? gb::media::rescale_ticks(set->duration, set->time_base, timeline.time_base)
              : gb::media::rescale_ticks(1, {rate.denominator, rate.numerator}, timeline.time_base);
      audio_feed->feed_until(ticks + duration, *audio_out);
    }
    ++written;

    const auto elapsed = clock_timer.elapsed();
    if (elapsed - reported >= 500) {
      reported = elapsed;
      const auto fps =
          static_cast<double>(written) * 1000.0 / static_cast<double>(std::max<qint64>(elapsed, 1));
      const auto remaining =
          fps > 0.0 ? static_cast<long double>(std::max(0LL, frames_expected - written)) / fps
                    : 0.0L;
      std::cerr << (interactive ? "\r" : "") << "gb-render: frame " << written << "/"
                << frames_expected << "  " << static_cast<int>(fps * 10.0) / 10.0 << " fps  "
                << gb::media::format_position(std::nullopt,
                                              static_cast<std::int64_t>(remaining * 1000.0L))
                       .substr(0, 8)
                << " left" << (interactive ? "   " : "\n") << std::flush;
    }
  }
  if (interactive)
    std::cerr << '\n';
  if (cancelled.load()) {
    std::cerr << "gb-render: cancelled; " << partial.filename().string() << " removed\n";
    return kExitCancelled;
  }
  if (written == 0)
    throw std::runtime_error("no frame in the range");
  if (audio_out)
    audio_out->finish();
  writer.finish();
  std::error_code error;
  if (options.overwrite)
    fs::remove(options.output, error);
  fs::rename(partial, options.output, error);
  if (error)
    throw std::runtime_error("cannot rename " + partial.string() + " to " +
                             options.output.string() + ": " + error.message());
  if (manifest)
    manifest->finish();
  cleanup.keep = true;
  const auto seconds_taken = static_cast<double>(clock_timer.elapsed()) / 1000.0;
  std::cerr << "gb-render: wrote " << options.output.string() << " ("
            << size_text(fs::file_size(options.output)) << ", " << written << " frames, "
            << writer.description() << ") in " << static_cast<int>(seconds_taken) << " s\n";
  if (missing_tiles_max > 0 && tiles.unavailable == 0)
    warnings.emplace_back("some map tiles were drawn as placeholders");
  for (auto index = printed_warnings; index < warnings.size(); ++index)
    std::cerr << "gb-render: warning: " << warnings[index] << '\n';
  if (options.json) {
    const auto bytes = fs::file_size(options.output);
    json_report("{\"path\": " + json_path(options.output) + ", \"bytes\": " +
                std::to_string(bytes) + ", \"frames\": " + std::to_string(written) +
                ", \"encoding\": " + json_string(writer.description()) +
                ", \"seconds\": " + json_number(seconds_taken) + ", \"manifest\": " +
                (manifest ? json_path(manifest->path()) : std::string("null")) + "}");
  }
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  std::signal(SIGINT, on_interrupt);
  std::signal(SIGTERM, on_interrupt);
  try {
    const auto options = parse_arguments(argc, argv);
    return run(options, argc, argv);
  } catch (const UsageError &error) {
    std::cerr << "gb-render: " << error.what() << '\n';
    return kExitUsage;
  } catch (const std::exception &error) {
    std::cerr << "gb-render: " << error.what() << '\n';
    return cancelled.load() ? kExitCancelled : kExitFailure;
  }
}
