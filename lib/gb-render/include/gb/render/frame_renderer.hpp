#pragma once

#include <QMatrix4x4>
#include <QRectF>
#include <QSize>
#include <QSizeF>

#include <array>
#include <cstdint>
#include <string>

class QOpenGLContext;
class QOpenGLExtraFunctions;

namespace gb::media {
struct FrameSet;
}

// Draws decoded frame sets with the player's shader (ADR-0007): one pass from planar YUV to the
// chosen view. Shared by gb-player's on-screen view and by stills and exports rendered offscreen,
// so both draw the same picture for the same view.
namespace gb::render {

// gb-player's view modes; the numbers are the shader's uMode and VideoView::Mode.
enum ViewMode : int {
  Rectilinear = 0,
  Fisheye = 1,
  Stereographic = 2,
  LittlePlanet = 3,
  Panorama = 4,
  RawFirst = 5,
  RawSecond = 6,
  Original = 7
};

// The shader's source layout: 0 flat, 1 MAX dual-stream EAC, 2 equirectangular.
enum SourceLayoutCode : int { FlatLayout = 0, MaxDualEacLayout = 1, EquirectangularLayout = 2 };

struct ViewParameters {
  int mode{Rectilinear};
  int layout{FlatLayout};
  float overlap{};
  // Equirectangular: where the image lies in the full 360×180° canvas, in canvas units.
  QRectF panorama_crop{0.0, 0.0, 1.0, 1.0};
  float yaw{}; // radians
  float pitch{};
  float roll{};
  float field_of_view{1.5707964F};
  float zoom{1.0F};
  float pan_x{};
  float pan_y{};
  // The canvas: the view as the player draws it. Only its aspect ratio enters the projection.
  QSizeF canvas{1.0, 1.0};
  // The part of the canvas that is drawn, in canvas units. The whole canvas on screen; the
  // capture rectangle for a still.
  QRectF view_crop{0.0, 0.0, 1.0, 1.0};
};

class FrameRenderer {
public:
  FrameRenderer() = default;
  FrameRenderer(const FrameRenderer &) = delete;
  FrameRenderer &operator=(const FrameRenderer &) = delete;
  // GL objects are not released here; call release() with the context current.
  ~FrameRenderer() = default;

  // Compiles the shader and creates textures in the current context. Needs OpenGL 3.3 or OpenGL
  // ES 3.0. Returns false, with error() set, when the context cannot run it.
  bool initialize(QOpenGLContext *context);
  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] bool failed() const noexcept { return failed_; }
  [[nodiscard]] const std::string &error() const noexcept { return error_; }
  // GL_RENDERER / GL_VERSION of the context.
  [[nodiscard]] const std::string &description() const noexcept { return description_; }
  [[nodiscard]] bool isOpenGLES() const noexcept { return es_; }

  // Uploads the planes of up to two pictures and remembers their size and colour coding.
  void upload(QOpenGLExtraFunctions *f, const gb::media::FrameSet &frames);
  [[nodiscard]] bool hasFrame() const noexcept { return has_frame_; }
  void clearFrame() noexcept { has_frame_ = false; }

  // Draws the uploaded frame (or the background without one) as a quad covering 0..canvas in the
  // coordinates that `matrix` maps onto the bound framebuffer. Blending, depth, culling and the
  // scissor are left to the caller. Texture bindings, the program and the vertex array change.
  void draw(QOpenGLExtraFunctions *f, const ViewParameters &parameters, const QMatrix4x4 &matrix);

  void release(QOpenGLExtraFunctions *f);

private:
  enum class Uniform {
    matrix,
    size,
    view_crop,
    source,
    mode,
    layout,
    has_frame,
    overlap,
    crop,
    yaw,
    pitch,
    roll,
    field_of_view,
    zoom,
    pan,
    luma_red,
    luma_blue,
    full_range,
    count
  };
  static constexpr int kTextureCount = 6;

  unsigned compile(QOpenGLExtraFunctions *f, unsigned type, const char *body, const char *name);
  void uploadPlane(QOpenGLExtraFunctions *f, int unit, int width, int height,
                   const std::uint8_t *data, int stride);
  [[nodiscard]] int uniform(Uniform name) const {
    return uniforms_[static_cast<std::size_t>(name)];
  }

  bool initialized_{};
  bool failed_{};
  bool es_{};
  std::string error_;
  std::string description_;
  unsigned program_{};
  unsigned buffer_{};
  unsigned vertex_array_{};
  std::array<unsigned, kTextureCount> textures_{};
  std::array<QSize, kTextureCount> texture_sizes_{};
  std::array<int, static_cast<std::size_t>(Uniform::count)> uniforms_{};

  bool has_frame_{};
  QSize source_size_{1, 1};
  float luma_red_{0.2126F};
  float luma_blue_{0.0722F};
  bool full_range_{};
};

} // namespace gb::render
