#include "project_controller.hpp"

#include "gb/media/media.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

std::int64_t ticksAt(const double seconds, const gb::media::Rational time_base) {
  if (!std::isfinite(seconds) || seconds < 0.0 || time_base.numerator <= 0 ||
      time_base.denominator <= 0) {
    throw std::runtime_error("invalid media time or stream time base");
  }
  const auto ticks =
      static_cast<long double>(seconds) * time_base.denominator / time_base.numerator;
  if (ticks > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
    throw std::runtime_error("media time is out of range");
  }
  return static_cast<std::int64_t>(std::llround(ticks));
}

gb::media::ProjectionMode projectionMode(const int value) {
  switch (value) {
  case 0:
    return gb::media::ProjectionMode::rectilinear;
  case 1:
    return gb::media::ProjectionMode::fisheye;
  case 2:
    return gb::media::ProjectionMode::stereographic;
  case 3:
    return gb::media::ProjectionMode::little_planet;
  default:
    throw std::runtime_error("unknown projection mode");
  }
}

long double centerLuma(const gb::media::RgbFrame &frame) {
  const auto face_width = frame.height;
  const auto begin = (frame.width - face_width) / 2;
  long double total{};
  std::uint64_t count{};
  for (int y = 0; y < frame.height; y += 8) {
    for (int x = begin; x < begin + face_width; x += 8) {
      const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                           static_cast<std::size_t>(x)) *
                          3U;
      total += 0.2126L * frame.pixels[offset] + 0.7152L * frame.pixels[offset + 1U] +
               0.0722L * frame.pixels[offset + 2U];
      ++count;
    }
  }
  return count == 0U ? 0.0L : total / count;
}

} // namespace

FrameImageProvider::FrameImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage FrameImageProvider::requestImage(const QString &id, QSize *size,
                                        const QSize &requested_size) {
  const QMutexLocker lock(&mutex_);
  const auto key = id.section('/', 0, 0);
  auto result = images_.value(key);
  if (size != nullptr)
    *size = result.size();
  if (requested_size.isValid()) {
    result = result.scaled(requested_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  }
  return result;
}

void FrameImageProvider::setImage(const QString &id, QImage image) {
  const QMutexLocker lock(&mutex_);
  images_.insert(id, std::move(image));
}

ProjectController::ProjectController(FrameImageProvider *provider, QObject *parent)
    : QObject(parent), provider_(provider) {}

ProjectController::~ProjectController() = default;

QString ProjectController::inputPath() const { return input_path_; }
QString ProjectController::inputName() const { return QFileInfo(input_path_).fileName(); }
QString ProjectController::status() const { return status_; }
QString ProjectController::rawFrontSource() const { return raw_front_source_; }
QString ProjectController::rawRearSource() const { return raw_rear_source_; }
QString ProjectController::panoramaSource() const { return panorama_source_; }
QString ProjectController::outputSource() const { return output_source_; }
bool ProjectController::ready() const { return panorama_ != nullptr; }

void ProjectController::openFile(const double seconds, const int panorama_width) {
  const auto selected = QFileDialog::getOpenFileName(
      nullptr, QStringLiteral("Open GoPro media"), QString(),
      QStringLiteral("GoPro media (*.360 *.MP4 *.mp4 *.mov);;All files (*)"));
  if (!selected.isEmpty())
    loadFile(selected, seconds, panorama_width);
}

void ProjectController::loadFile(const QString &path, const double seconds,
                                 const int panorama_width) {
  try {
    if (panorama_width < 512 || panorama_width > 8192 || panorama_width % 2 != 0) {
      throw std::runtime_error("panorama width must be an even value from 512 to 8192");
    }
    setStatus(QStringLiteral("Decoding synchronized source frames..."));
    const std::filesystem::path source(path.toStdString());
    const auto streams = gb::media::video_streams(source);
    if (streams.size() != 2U) {
      throw std::runtime_error("gb-project currently requires exactly two video streams");
    }
    const auto first_ticks = ticksAt(seconds, streams[0].time_base);
    const auto second_ticks = ticksAt(seconds, streams[1].time_base);
    const auto first = gb::media::decode_frame(source, streams[0].index, first_ticks);
    const auto second = gb::media::decode_frame(source, streams[1].index, second_ticks);
    const auto first_seconds = static_cast<long double>(first.pts) * first.time_base.numerator /
                               first.time_base.denominator;
    const auto second_seconds = static_cast<long double>(second.pts) * second.time_base.numerator /
                                second.time_base.denominator;
    if (std::fabs(first_seconds - second_seconds) > 1e-9L) {
      throw std::runtime_error("selected source frames are not presentation-time synchronized");
    }
    const auto overlap = first.width - first.height * 3;
    auto panorama = gb::media::reconstruct_equirectangular(first, second, panorama_width,
                                                           panorama_width / 2, overlap);
    const auto output = gb::media::project_equirectangular(
        panorama, gb::media::ProjectionMode::rectilinear, 960, 540);
    const auto rear_luma = centerLuma(second);
    input_path_ = path;
    emit inputChanged();
    panorama_ = std::make_unique<gb::media::RgbFrame>(std::move(panorama));
    publish(first, second, *panorama_, output);
    auto message = QStringLiteral("Ready — streams %1/%2, selected PTS %3/%4, overlap %5 px")
                       .arg(first.stream_index)
                       .arg(second.stream_index)
                       .arg(first.pts)
                       .arg(second.pts)
                       .arg(overlap);
    if (rear_luma < 5.0L) {
      message += QStringLiteral(" — source BACK face is near-black (mean luma %1)")
                     .arg(static_cast<double>(rear_luma), 0, 'f', 2);
    }
    setStatus(message);
  } catch (const std::exception &error) {
    setStatus(QStringLiteral("Error: ") + QString::fromUtf8(error.what()));
  }
}

void ProjectController::saveProjection(const int mode, const double yaw, const double pitch,
                                       const double roll, const double field_of_view) {
  if (panorama_ == nullptr)
    return;
  auto destination = QFileDialog::getSaveFileName(
      nullptr, QStringLiteral("Save projected view"), QStringLiteral("projection.png"),
      QStringLiteral("PNG image (*.png);;PPM image (*.ppm);;RGB24 buffer (*.rgb24)"));
  if (destination.isEmpty())
    return;
  try {
    const auto projected = gb::media::project_equirectangular(
        *panorama_, projectionMode(mode), 1920, 1080, yaw, pitch, roll, field_of_view);
    const auto extension = QFileInfo(destination).suffix().toLower();
    if (extension == QStringLiteral("ppm")) {
      gb::media::write_ppm(destination.toStdString(), projected);
    } else if (extension == QStringLiteral("rgb24") || extension == QStringLiteral("rgb")) {
      gb::media::write_rgb24(destination.toStdString(), projected);
    } else {
      if (extension.isEmpty())
        destination += QStringLiteral(".png");
      gb::media::write_png(destination.toStdString(), projected);
    }
    setStatus(QStringLiteral("Saved deterministic CPU reference: ") + destination);
  } catch (const std::exception &error) {
    setStatus(QStringLiteral("Save failed: ") + QString::fromUtf8(error.what()));
  }
}

void ProjectController::updateProjection(const int mode, const double yaw, const double pitch,
                                         const double roll, const double field_of_view) {
  if (panorama_ == nullptr)
    return;
  try {
    publishOutput(gb::media::project_equirectangular(*panorama_, projectionMode(mode), 960, 540,
                                                     yaw, pitch, roll, field_of_view));
  } catch (const std::exception &error) {
    setStatus(QStringLiteral("Projection failed: ") + QString::fromUtf8(error.what()));
  }
}

QImage ProjectController::image(const gb::media::RgbFrame &frame) {
  const QImage borrowed(frame.pixels.data(), frame.width, frame.height, frame.width * 3,
                        QImage::Format_RGB888);
  return borrowed.copy();
}

void ProjectController::setStatus(QString value) {
  status_ = std::move(value);
  emit statusChanged();
}

void ProjectController::publish(const gb::media::RgbFrame &front, const gb::media::RgbFrame &rear,
                                const gb::media::RgbFrame &panorama,
                                const gb::media::RgbFrame &output) {
  provider_->setImage(QStringLiteral("front"), image(front));
  provider_->setImage(QStringLiteral("rear"), image(rear));
  provider_->setImage(QStringLiteral("panorama"), image(panorama));
  provider_->setImage(QStringLiteral("output"), image(output));
  ++revision_;
  raw_front_source_ = QStringLiteral("image://gb/front/%1").arg(revision_);
  raw_rear_source_ = QStringLiteral("image://gb/rear/%1").arg(revision_);
  panorama_source_ = QStringLiteral("image://gb/panorama/%1").arg(revision_);
  output_source_ = QStringLiteral("image://gb/output/%1").arg(revision_);
  emit imagesChanged();
}

void ProjectController::publishOutput(const gb::media::RgbFrame &output) {
  provider_->setImage(QStringLiteral("output"), image(output));
  ++revision_;
  output_source_ = QStringLiteral("image://gb/output/%1").arg(revision_);
  emit imagesChanged();
}
