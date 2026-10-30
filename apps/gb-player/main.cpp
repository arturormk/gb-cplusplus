#include "benchmark.hpp"
#include "player_controller.hpp"
#include "video_view.hpp"

#include "gb/media/playback.hpp"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

#include <QElapsedTimer>

#include <algorithm>
#include <array>
#include <clocale>
#include <functional>
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Options {
  QString path;
  double seconds{};
  int view{VideoView::Rectilinear};
  bool play{};
  PlayerOptions player;
  std::optional<BenchmarkOptions> benchmark;
  int seeks{10};
  std::uint32_t seed{1};
  QString screenshot;
  bool smoke_test{};
};

constexpr std::array<std::string_view, 8> kViewNames{
    "rectilinear", "fisheye", "stereographic", "little-planet", "panorama", "raw-1", "raw-2", "original"};

void help() {
  std::cout
      << "Usage: gb-player [options] [recording]\n\n"
         "Native Qt player for GoPro media. MAX two-stream .360 recordings are reconstructed\n"
         "and projected on the GPU; single-stream videos are shown flat. Files are opened\n"
         "read-only.\n\n"
         "Options:\n"
         "  --time SECONDS        initial media time (default 0)\n"
         "  --view MODE           rectilinear, fisheye, stereographic, little-planet,\n"
         "                        panorama, raw-1, raw-2, or original (default rectilinear)\n"
         "  --play                start playing after opening\n"
         "  --decoder-threads N   decoder threads per stream, 0 = automatic (default 0)\n"
         "  --queue N             decoded frame sets buffered ahead, 1..32 (default 4)\n"
         "  --no-audio            do not open an audio device\n"
         "  --benchmark SECONDS   play for SECONDS, check that scrubbing and seeking stay\n"
         "                        silent, run random seeks, print a gb.player.benchmark/v1\n"
         "                        JSON report, and exit\n"
         "  --seeks N             benchmark seek count (default 10)\n"
         "  --seed N              benchmark seek seed (default 1)\n"
         "  --screenshot PATH     save a window screenshot after the first frame, then exit\n"
         "  --help                show this help and exit\n"
         "  --version             show the program version and exit\n\n"
         "Keys: Space play/pause, Left/Right seek 5 s, ,/. step one frame, M mute,\n"
         "F full screen, R reset view, 1-8 select view mode.\n\n"
         "Audio plays only during stable playback. It is silent while paused, while the\n"
         "timeline is dragged, while a seek is decoding, and for 250 ms after each seek.\n";
}

double parseNumber(const std::string_view text, const char *what) {
  std::size_t consumed{};
  const std::string value(text);
  const auto result = std::stod(value, &consumed);
  if (consumed != value.size() || !std::isfinite(result)) {
    throw std::runtime_error(std::string("invalid ") + what);
  }
  return result;
}

int parseInteger(const std::string_view text, const char *what, const int minimum, const int maximum) {
  std::size_t consumed{};
  const std::string value(text);
  const auto result = std::stoi(value, &consumed);
  if (consumed != value.size() || result < minimum || result > maximum) {
    throw std::runtime_error(std::string("invalid ") + what);
  }
  return result;
}

Options parse(const int argc, char **argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value = [&]() -> std::string_view {
      if (++index >= argc) throw std::runtime_error(std::string(argument) + " requires a value");
      return argv[index];
    };
    if (argument == "--time") {
      options.seconds = parseNumber(value(), "media time");
      if (options.seconds < 0.0) throw std::runtime_error("invalid media time");
    } else if (argument == "--view") {
      const auto name = value();
      const auto found = std::find(kViewNames.begin(), kViewNames.end(), name);
      if (found == kViewNames.end()) throw std::runtime_error("unknown view mode: " + std::string(name));
      options.view = static_cast<int>(std::distance(kViewNames.begin(), found));
    } else if (argument == "--play") {
      options.play = true;
    } else if (argument == "--decoder-threads") {
      options.player.decoder_threads = parseInteger(value(), "decoder thread count", 0, 64);
    } else if (argument == "--queue") {
      options.player.queue_capacity = static_cast<std::size_t>(parseInteger(value(), "queue size", 1, 32));
    } else if (argument == "--no-audio") {
      options.player.audio = false;
    } else if (argument == "--benchmark") {
      BenchmarkOptions benchmark;
      benchmark.play_seconds = parseNumber(value(), "benchmark duration");
      if (benchmark.play_seconds <= 0.0 || benchmark.play_seconds > 3600.0) {
        throw std::runtime_error("benchmark duration must be within (0, 3600] seconds");
      }
      options.benchmark = benchmark;
    } else if (argument == "--seeks") {
      options.seeks = parseInteger(value(), "seek count", 0, 1000);
    } else if (argument == "--seed") {
      options.seed = static_cast<std::uint32_t>(parseInteger(value(), "seed", 0, 2147483647));
    } else if (argument == "--screenshot") {
      options.screenshot = QString::fromLocal8Bit(value().data());
    } else if (argument == "--smoke-test") {
      options.smoke_test = true;
    } else if (!argument.empty() && argument.front() == '-') {
      throw std::runtime_error("unknown option: " + std::string(argument));
    } else if (options.path.isEmpty()) {
      options.path = QString::fromLocal8Bit(argv[index]);
    } else {
      throw std::runtime_error("only one input file is accepted");
    }
  }
  if (options.benchmark.has_value()) {
    options.benchmark->seeks = options.seeks;
    options.benchmark->seed = options.seed;
    options.benchmark->view_mode = options.view;
  }
  if ((options.benchmark.has_value() || !options.screenshot.isEmpty()) && options.path.isEmpty()) {
    throw std::runtime_error("--benchmark and --screenshot require an input file");
  }
  return options;
}

// Polls until the first frame is visible (or presented without a GPU view) and metadata has
// loaded, then runs the callback.
void whenFirstFrameReady(PlayerController &controller, QObject *context, std::function<void(bool)> callback) {
  auto *timer = new QTimer(context);
  auto *elapsed = new QElapsedTimer;
  elapsed->start();
  QObject::connect(timer, &QTimer::timeout, context, [&controller, timer, elapsed, callback] {
    const auto statistics = controller.playbackEngine().statistics();
    const auto shown = VideoView::gpuAvailable() ? statistics.rendered_frames : statistics.presented_frames;
    const auto failed = controller.statusIsError();
    if ((shown > 0U && !controller.metadataLoading()) || failed || elapsed->elapsed() > 30000) {
      timer->stop();
      timer->deleteLater();
      const auto ok = shown > 0U && !failed;
      delete elapsed;
      callback(ok);
    }
  });
  timer->start(20);
}

} // namespace

int main(int argc, char **argv) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help") {
      help();
      return 0;
    }
    if (argument == "--version") {
      std::cout << "gb-player " << GB_PLAYER_VERSION << '\n';
      return 0;
    }
  }
  Options options;
  try {
    options = parse(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "gb-player: " << error.what() << '\n';
    return 2;
  }

  // The GPU view is an OpenGL scene-graph render node. The smoke test uses the software
  // backend so it can run without a display or GPU.
  QQuickWindow::setGraphicsApi(options.smoke_test ? QSGRendererInterface::Software
                                                  : QSGRendererInterface::OpenGL);
  QApplication application(argc, argv);
  // Qt adopts the user's locale; the format libraries parse and print numbers with C functions.
  std::setlocale(LC_NUMERIC, "C");
  QCoreApplication::setApplicationName(QStringLiteral("gb-player"));
  QCoreApplication::setApplicationVersion(QStringLiteral(GB_PLAYER_VERSION));
  QCoreApplication::setOrganizationName(QStringLiteral("gb"));
  qmlRegisterType<VideoView>("Gb.Player", 1, 0, "VideoView");

  PlayerController controller(options.player);
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("player"), &controller);
  engine.rootContext()->setContextProperty(QStringLiteral("initialView"), options.view);
  engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  if (engine.rootObjects().isEmpty()) return 1;
  auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());

  if (!options.path.isEmpty()) {
    const auto opened = controller.open(options.path, options.seconds);
    if (!opened) {
      std::cerr << "gb-player: " << controller.status().toStdString() << '\n';
      if (options.smoke_test || options.benchmark || !options.screenshot.isEmpty()) return 1;
    } else if (options.play && !options.benchmark) {
      controller.play();
    }
  }

  BenchmarkRunner *benchmark = nullptr;
  if (options.benchmark.has_value()) {
    benchmark = new BenchmarkRunner(controller, window, *options.benchmark, &application);
    QObject::connect(benchmark, &BenchmarkRunner::finished, &application, &QCoreApplication::exit);
    benchmark->start();
  } else if (!options.screenshot.isEmpty()) {
    whenFirstFrameReady(controller, &application, [&application, window, &options](const bool ok) {
      if (!ok || window == nullptr) {
        std::cerr << "gb-player: no frame was displayed\n";
        application.exit(1);
        return;
      }
      QTimer::singleShot(500, &application, [&application, window, &options] {
        if (!window->grabWindow().save(options.screenshot)) {
          std::cerr << "gb-player: cannot save screenshot\n";
          application.exit(1);
          return;
        }
        application.quit();
      });
    });
  } else if (options.smoke_test) {
    if (options.path.isEmpty()) {
      QTimer::singleShot(250, &application, &QCoreApplication::quit);
    } else {
      whenFirstFrameReady(controller, &application, [&application, &controller](const bool ok) {
        const auto current = controller.playbackEngine().current();
        if (!ok || !current.frames) {
          std::cerr << "gb-player: smoke test failed: " << controller.status().toStdString() << '\n';
          application.exit(1);
          return;
        }
        std::cout << "gb-player smoke: layout=" << (controller.sourceLayout() == 1 ? "max_dual_eac" : "flat")
                  << " streams=" << current.frames->pictures.size() << " pts=" << current.frames->pts
                  << " telemetry=" << controller.telemetryState().toStdString()
                  << " audio=" << (controller.hasAudio() ? "open" : "none") << '\n';
        application.quit();
      });
    }
  }
  return application.exec();
}
