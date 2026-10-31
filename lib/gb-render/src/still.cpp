#include "gb/render/still.hpp"

#include "gb/image/exif_writer.hpp"
#include "gb/media/playback.hpp"

#include <QBuffer>
#include <QFileInfo>
#include <QImageWriter>
#include <QOpenGLExtraFunctions>
#include <QPainter>
#include <QSaveFile>

#include <algorithm>
#include <stdexcept>
#include <vector>

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_MAX_RENDERBUFFER_SIZE
#define GL_MAX_RENDERBUFFER_SIZE 0x84E8
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_PACK_ROW_LENGTH
#define GL_PACK_ROW_LENGTH 0x0D02
#endif

namespace gb::render {

OffscreenRenderer::OffscreenRenderer() = default;

OffscreenRenderer::~OffscreenRenderer() {
  if (context_ && surface_ && context_->makeCurrent(surface_.get())) {
    renderer_.release(context_->extraFunctions());
    context_->doneCurrent();
  }
}

bool OffscreenRenderer::initialize() {
  if (renderer_.initialized())
    return true;
  if (tried_)
    return false;
  tried_ = true;
  auto format = QSurfaceFormat::defaultFormat();
  if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGL) {
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
  } else {
    format.setVersion(3, 0);
  }
  context_ = std::make_unique<QOpenGLContext>();
  context_->setFormat(format);
  if (!context_->create()) {
    error_ = "cannot create an OpenGL context";
    return false;
  }
  surface_ = std::make_unique<QOffscreenSurface>();
  surface_->setFormat(context_->format());
  surface_->create();
  if (!surface_->isValid() || !context_->makeCurrent(surface_.get())) {
    error_ = "cannot make an offscreen OpenGL surface current";
    return false;
  }
  const auto ok = renderer_.initialize(context_.get());
  if (!ok)
    error_ = renderer_.error();
  context_->doneCurrent();
  return ok;
}

QImage OffscreenRenderer::render(const gb::media::FrameSet &frames, const ViewParameters &view,
                                 const QSize size) {
  if (!initialize())
    throw std::runtime_error("offscreen rendering is unavailable: " + error_);
  if (size.isEmpty())
    throw std::runtime_error("empty output size");
  if (!context_->makeCurrent(surface_.get()))
    throw std::runtime_error("cannot make the offscreen OpenGL context current");
  auto *f = context_->extraFunctions();
  struct Done {
    QOpenGLContext *context;
    ~Done() { context->doneCurrent(); }
  } done{context_.get()};

  GLint maximum{};
  f->glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maximum);
  if (size.width() > maximum || size.height() > maximum) {
    throw std::runtime_error("output size " + std::to_string(size.width()) + "×" +
                             std::to_string(size.height()) + " exceeds the GPU limit of " +
                             std::to_string(maximum) + " pixels");
  }

  GLuint framebuffer{};
  GLuint colour{};
  f->glGenFramebuffers(1, &framebuffer);
  f->glGenRenderbuffers(1, &colour);
  f->glBindRenderbuffer(GL_RENDERBUFFER, colour);
  f->glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, size.width(), size.height());
  f->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  f->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colour);
  const auto status = f->glCheckFramebufferStatus(GL_FRAMEBUFFER);
  const auto cleanup = [&] {
    f->glBindFramebuffer(GL_FRAMEBUFFER, context_->defaultFramebufferObject());
    f->glDeleteRenderbuffers(1, &colour);
    f->glDeleteFramebuffers(1, &framebuffer);
  };
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    cleanup();
    throw std::runtime_error("cannot create an offscreen framebuffer of that size");
  }

  f->glViewport(0, 0, size.width(), size.height());
  f->glDisable(GL_BLEND);
  f->glDisable(GL_DEPTH_TEST);
  f->glDisable(GL_CULL_FACE);
  f->glDisable(GL_SCISSOR_TEST);
  renderer_.upload(f, frames);
  if (frames.pictures.empty())
    renderer_.clearFrame();
  // Canvas coordinates, y down, onto the whole framebuffer: row 0 of the canvas ends up at the
  // top of the framebuffer, which glReadPixels returns last.
  QMatrix4x4 matrix;
  matrix.ortho(0.0F, static_cast<float>(view.canvas.width()),
               static_cast<float>(view.canvas.height()), 0.0F, -1.0F, 1.0F);
  renderer_.draw(f, view, matrix);

  QImage image(size, QImage::Format_RGBA8888);
  f->glPixelStorei(GL_PACK_ALIGNMENT, 4);
  f->glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
  const auto error = f->glGetError();
  cleanup();
  if (error != GL_NO_ERROR)
    throw std::runtime_error("reading the rendered picture back failed (GL error " +
                             std::to_string(error) + ")");
  return image.mirrored(false, true).convertToFormat(QImage::Format_RGB888);
}

QImage compose_frame(const QImage &picture, const gb::exporting::FrameLayout &layout) {
  if (!layout.panel && picture.size() == QSize(layout.width, layout.height))
    return picture;
  QImage frame(layout.width, layout.height, QImage::Format_RGB888);
  frame.fill(Qt::black);
  QPainter painter(&frame);
  painter.drawImage(QPoint(layout.picture.x, layout.picture.y), picture);
  painter.end();
  return frame;
}

QImage render_still(OffscreenRenderer &renderer, const gb::media::FrameSet &frames,
                    const ViewParameters &view, const gb::exporting::FrameLayout &layout) {
  const auto picture =
      renderer.render(frames, view, QSize(layout.picture.width, layout.picture.height));
  return compose_frame(picture, layout);
}

std::optional<StillFormat> still_format_for(const QString &path) {
  const auto suffix = QFileInfo(path).suffix().toLower();
  if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
    return StillFormat::jpeg;
  if (suffix == QLatin1String("png"))
    return StillFormat::png;
  return std::nullopt;
}

void write_still(const QImage &image, const QString &path, const StillOptions &options) {
  if (image.isNull())
    throw std::runtime_error("no picture to write");
  QByteArray encoded;
  {
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, options.format == StillFormat::jpeg ? "jpeg" : "png");
    if (options.format == StillFormat::jpeg)
      writer.setQuality(std::clamp(options.jpeg_quality, 1, 100));
    if (!writer.write(image))
      throw std::runtime_error("cannot encode the picture: " + writer.errorString().toStdString());
  }
  if (options.format == StillFormat::jpeg && options.gps) {
    const std::vector<std::uint8_t> jpeg(encoded.begin(), encoded.end());
    const auto with_exif =
        gb::image::insert_exif_segment(jpeg, gb::image::build_gps_exif_segment(*options.gps));
    encoded = QByteArray(reinterpret_cast<const char *>(with_exif.data()),
                         static_cast<qsizetype>(with_exif.size()));
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly))
    throw std::runtime_error("cannot write " + path.toStdString() + ": " +
                             file.errorString().toStdString());
  if (file.write(encoded) != encoded.size() || !file.commit()) {
    throw std::runtime_error("cannot write " + path.toStdString() + ": " +
                             file.errorString().toStdString());
  }
}

} // namespace gb::render
