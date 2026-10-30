#include "project_controller.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QTimer>

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
  int panorama_width{2048};
  bool smoke_test{};
  QString screenshot;
};

void help() {
  std::cout << "Usage: gb-project [options] [recording.360]\n\n"
               "Qt projection laboratory for native GoPro MAX media.\n\n"
               "Options:\n"
               "  --time SECONDS  initial media time (default 0)\n"
               "  --width PIXELS  reconstruction width, even 512..8192 (default 2048)\n"
               "  --screenshot P  save a GUI screenshot after loading, then exit\n"
               "  --help          show this help and exit\n"
               "  --version       show the program version and exit\n";
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
      std::size_t consumed{};
      options.seconds = std::stod(std::string(value()), &consumed);
      if (consumed != std::string(argv[index]).size() || !std::isfinite(options.seconds) ||
          options.seconds < 0.0) {
        throw std::runtime_error("invalid media time");
      }
    } else if (argument == "--width") {
      std::size_t consumed{};
      options.panorama_width = std::stoi(std::string(value()), &consumed);
      if (consumed != std::string(argv[index]).size()) {
        throw std::runtime_error("invalid panorama width");
      }
    } else if (argument == "--smoke-test") {
      options.smoke_test = true;
    } else if (argument == "--screenshot") {
      const auto screenshot_path = value();
      options.screenshot = QString::fromLocal8Bit(screenshot_path.data(),
                                                   static_cast<int>(screenshot_path.size()));
    } else if (!argument.empty() && argument.front() == '-') {
      throw std::runtime_error("unknown option: " + std::string(argument));
    } else if (options.path.isEmpty()) {
      options.path = QString::fromLocal8Bit(argv[index]);
    } else {
      throw std::runtime_error("only one input file is accepted");
    }
  }
  return options;
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
      std::cout << "gb-project " << GB_PROJECT_VERSION << '\n';
      return 0;
    }
  }
  try {
    const auto options = parse(argc, argv);
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("gb-project"));
    QCoreApplication::setOrganizationName(QStringLiteral("gb"));
    auto *provider = new FrameImageProvider;
    ProjectController controller(provider);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("gb"), provider);
    engine.rootContext()->setContextProperty(QStringLiteral("projectController"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("initialSeconds"), options.seconds);
    engine.rootContext()->setContextProperty(QStringLiteral("initialPanoramaWidth"),
                                             options.panorama_width);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    if (!options.path.isEmpty()) {
      QTimer::singleShot(0, &controller, [&controller, options]() {
        controller.loadFile(options.path, options.seconds, options.panorama_width);
      });
    }
    if (!options.screenshot.isEmpty()) {
      QTimer::singleShot(1000, &application, [&application, &engine, options]() {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
        if (window == nullptr) {
          std::cerr << "gb-project: cannot save GUI screenshot\n";
          application.exit(1);
          return;
        }
        const auto grab = window->contentItem()->grabToImage();
        QObject::connect(grab.data(), &QQuickItemGrabResult::ready, &application,
                         [grab, &application, options]() {
                           if (!grab->image().save(options.screenshot)) {
                             std::cerr << "gb-project: cannot save GUI screenshot\n";
                             application.exit(1);
                             return;
                           }
                           application.quit();
                         });
      });
    } else if (options.smoke_test) {
      QTimer::singleShot(250, &application, &QCoreApplication::quit);
    }
    return application.exec();
  } catch (const std::exception &error) {
    std::cerr << "gb-project: " << error.what() << '\n';
    return 2;
  }
}
