#pragma once

#include "gb/export/framing.hpp"
#include "gb/image/photo.hpp"
#include "gb/render/frame_renderer.hpp"

#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QString>

#include <memory>
#include <optional>
#include <string>

namespace gb::media {
struct FrameSet;
}

namespace gb::render {

// Renders frame sets with FrameRenderer into an offscreen framebuffer and reads them back. Owns
// its own OpenGL context on a QOffscreenSurface, so it works without a window, and on the GUI
// thread of an application whose scene graph renders elsewhere. Create and use it on the thread
// that owns the QGuiApplication.
class OffscreenRenderer {
public:
  OffscreenRenderer();
  ~OffscreenRenderer();
  OffscreenRenderer(const OffscreenRenderer &) = delete;
  OffscreenRenderer &operator=(const OffscreenRenderer &) = delete;

  // Creates the context and compiles the shader on first use. False, with error(), when no
  // usable OpenGL context exists.
  bool initialize();
  [[nodiscard]] const std::string &error() const noexcept { return error_; }
  [[nodiscard]] std::string description() const { return renderer_.description(); }

  // Draws `view.view_crop` of the canvas at `size` pixels. Throws std::runtime_error when the
  // renderer is unavailable or the size exceeds what the GPU can render.
  [[nodiscard]] QImage render(const gb::media::FrameSet &frames, const ViewParameters &view,
                              QSize size);

private:
  std::unique_ptr<QOffscreenSurface> surface_;
  std::unique_ptr<QOpenGLContext> context_;
  FrameRenderer renderer_;
  std::string error_;
  bool tried_{};
};

// The whole output frame for a picture rendered at `layout.picture`'s size: black where the
// panel is, the picture in its place.
[[nodiscard]] QImage compose_frame(const QImage &picture, const gb::exporting::FrameLayout &layout);

// Renders a still: `view` with its view_crop at the picture size of `layout`, then the frame.
[[nodiscard]] QImage render_still(OffscreenRenderer &renderer, const gb::media::FrameSet &frames,
                                  const ViewParameters &view,
                                  const gb::exporting::FrameLayout &layout);

enum class StillFormat { jpeg, png };

struct StillOptions {
  StillFormat format{StillFormat::jpeg};
  int jpeg_quality{92}; // 1..100
  // JPEG only: written as an EXIF GPS IFD (gb::image::build_gps_exif). Without it the still
  // carries no EXIF at all.
  std::optional<gb::image::PhotoGps> gps;
};

// .jpg / .jpeg / .png, case-insensitively.
[[nodiscard]] std::optional<StillFormat> still_format_for(const QString &path);

// Encodes and writes the still atomically (a temporary file renamed into place). It replaces an
// existing file: callers decide whether that is allowed. Throws std::runtime_error with the
// reason on failure.
void write_still(const QImage &image, const QString &path, const StillOptions &options);

} // namespace gb::render
