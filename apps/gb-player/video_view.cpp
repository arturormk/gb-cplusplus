#include "video_view.hpp"

#include "playback_engine.hpp"

#include "gb/media/playback.hpp"
#include "gb/render/frame_renderer.hpp"

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QQuickWindow>
#include <QSGRenderNode>
#include <QSGSimpleRectNode>
#include <QtMath>

#include <array>
#include <cmath>
#include <optional>

#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

namespace {

constexpr int kTextureCount = 6;
constexpr int kQueryCount = 4;

float radians(const double degrees) { return static_cast<float>(qDegreesToRadians(degrees)); }

// Draws the presented frame set with the shared FrameRenderer, keeping the scene graph's GL state
// as it found it, and reports upload and GPU timings to the engine.
class VideoRenderNode final : public QSGRenderNode {
public:
  ~VideoRenderNode() override {
    if (QOpenGLContext::currentContext() != nullptr)
      releaseResources();
  }

  void synchronize(PlaybackEngine *engine, PresentedFrame frame,
                   const gb::render::ViewParameters &parameters) {
    engine_ = engine;
    frame_ = std::move(frame);
    parameters_ = parameters;
  }

  [[nodiscard]] StateFlags changedStates() const override {
    return BlendState | DepthState | ScissorState | CullState;
  }

  [[nodiscard]] RenderingFlags flags() const override {
    return BoundedRectRendering | OpaqueRendering;
  }

  [[nodiscard]] QRectF rect() const override { return {QPointF(0.0, 0.0), parameters_.canvas}; }

  // Qt 6.2's RHI renderer only guarantees matrix() during prepare(); in render() it can point at
  // stale memory, so keep a copy.
  void prepare() override { model_matrix_ = *this->matrix(); }

  void releaseResources() override {
    auto *context = QOpenGLContext::currentContext();
    if (context == nullptr || !renderer_.initialized())
      return;
    auto *f = context->extraFunctions();
    renderer_.release(f);
    if (timer_queries_)
      f->glDeleteQueries(kQueryCount, queries_.data());
    timer_queries_ = false;
    uploaded_serial_ = 0;
  }

  void render(const RenderState *state) override {
    auto *context = QOpenGLContext::currentContext();
    if (context == nullptr)
      return;
    auto *f = context->extraFunctions();
    if (!renderer_.initialized() && !renderer_.failed()) {
      if (renderer_.initialize(context)) {
        // Timer queries are a desktop OpenGL 3.3 feature; ES contexts report no GPU timing.
        timer_queries_ = !renderer_.isOpenGLES();
        if (timer_queries_) {
          f->glGenQueries(kQueryCount, queries_.data());
          query_pending_.fill(false);
        }
      } else {
        qWarning("gb-player: %s", renderer_.error().c_str());
      }
    }
    if (engine_ != nullptr && !renderer_reported_ &&
        (renderer_.initialized() || renderer_.failed())) {
      engine_->setRendererDescription(renderer_.description());
      renderer_reported_ = true;
    }
    if (renderer_.failed())
      return;

    GLint previous_program{};
    GLint previous_vertex_array{};
    GLint previous_buffer{};
    GLint previous_active{};
    GLint previous_row_length{};
    GLint previous_alignment{};
    std::array<GLint, kTextureCount> previous_textures{};
    f->glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    f->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vertex_array);
    f->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_buffer);
    f->glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active);
    f->glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previous_row_length);
    f->glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_alignment);
    for (int unit = 0; unit < kTextureCount; ++unit) {
      f->glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
      f->glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_textures[static_cast<std::size_t>(unit)]);
    }

    const bool upload = frame_.frames != nullptr && frame_.serial != uploaded_serial_;
    std::optional<int> query_slot;
    if (upload && timer_queries_)
      query_slot = beginQuery(f);
    QElapsedTimer upload_timer;
    upload_timer.start();
    if (upload)
      renderer_.upload(f, *frame_.frames);
    if (frame_.frames == nullptr)
      renderer_.clearFrame();
    const auto upload_ms = static_cast<double>(upload_timer.nsecsElapsed()) / 1.0e6;

    f->glDisable(GL_BLEND);
    f->glDisable(GL_DEPTH_TEST);
    f->glDisable(GL_CULL_FACE);
    if (state->scissorEnabled()) {
      const auto clip = state->scissorRect();
      f->glEnable(GL_SCISSOR_TEST);
      f->glScissor(clip.x(), clip.y(), clip.width(), clip.height());
    } else {
      f->glDisable(GL_SCISSOR_TEST);
    }
    renderer_.draw(f, parameters_, *state->projectionMatrix() * model_matrix_);
    if (query_slot.has_value())
      f->glEndQuery(GL_TIME_ELAPSED);

    if (upload) {
      uploaded_serial_ = frame_.serial;
      const auto gpu_ms = timer_queries_ ? collectQueries(f) : std::nullopt;
      if (engine_ != nullptr)
        engine_->reportRendered(frame_.serial, frame_.generation, upload_ms, gpu_ms);
    }

    for (int unit = 0; unit < kTextureCount; ++unit) {
      f->glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
      f->glBindTexture(GL_TEXTURE_2D,
                       static_cast<GLuint>(previous_textures[static_cast<std::size_t>(unit)]));
    }
    f->glActiveTexture(static_cast<GLenum>(previous_active));
    f->glPixelStorei(GL_UNPACK_ROW_LENGTH, previous_row_length);
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, previous_alignment);
    f->glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previous_buffer));
    f->glBindVertexArray(static_cast<GLuint>(previous_vertex_array));
    f->glUseProgram(static_cast<GLuint>(previous_program));
  }

private:
  std::optional<int> beginQuery(QOpenGLExtraFunctions *f) {
    const auto slot = next_query_;
    if (query_pending_[static_cast<std::size_t>(slot)])
      return std::nullopt;
    f->glBeginQuery(GL_TIME_ELAPSED, queries_[static_cast<std::size_t>(slot)]);
    query_pending_[static_cast<std::size_t>(slot)] = true;
    next_query_ = (next_query_ + 1) % kQueryCount;
    return slot;
  }

  // Returns the most recent completed measurement without waiting for the GPU.
  std::optional<double> collectQueries(QOpenGLExtraFunctions *f) {
    std::optional<double> latest;
    for (int offset = 0; offset < kQueryCount; ++offset) {
      const auto slot = static_cast<std::size_t>((next_query_ + offset) % kQueryCount);
      if (!query_pending_[slot])
        continue;
      GLuint available{};
      f->glGetQueryObjectuiv(queries_[slot], GL_QUERY_RESULT_AVAILABLE, &available);
      if (available == 0U)
        continue;
      GLuint nanoseconds{};
      f->glGetQueryObjectuiv(queries_[slot], GL_QUERY_RESULT, &nanoseconds);
      query_pending_[slot] = false;
      latest = static_cast<double>(nanoseconds) / 1.0e6;
    }
    return latest;
  }

  PlaybackEngine *engine_{};
  PresentedFrame frame_;
  QMatrix4x4 model_matrix_;
  gb::render::ViewParameters parameters_;
  gb::render::FrameRenderer renderer_;
  bool renderer_reported_{};
  bool timer_queries_{};
  std::array<GLuint, kQueryCount> queries_{};
  std::array<bool, kQueryCount> query_pending_{};
  int next_query_{};
  std::uint64_t uploaded_serial_{};
};

} // namespace

VideoView::VideoView(QQuickItem *parent) : QQuickItem(parent) {
  setFlag(ItemHasContents, true);
  connect(this, &VideoView::viewChanged, this, &QQuickItem::update);
  connect(this, &QQuickItem::windowChanged, this, &VideoView::attachWindow);
}

void VideoView::attachWindow(QQuickWindow *window) {
  disconnect(animation_connection_);
  if (window != nullptr && gpuAvailable()) {
    animation_connection_ = connect(window, &QQuickWindow::afterAnimating, this, [this, window] {
      if (engine_ == nullptr || !engine_->playing())
        return;
      engine_->advanceFrame();
      window->update();
    });
  }
  connectEngine();
}

void VideoView::connectEngine() {
  if (engine_ == nullptr)
    return;
  engine_->setFrameDriven(window() != nullptr && gpuAvailable());
}

QObject *VideoView::engine() const { return engine_; }

void VideoView::setEngine(QObject *engine) {
  auto *typed = qobject_cast<PlaybackEngine *>(engine);
  if (typed == engine_)
    return;
  if (engine_ != nullptr)
    disconnect(engine_, nullptr, this, nullptr);
  engine_ = typed;
  if (engine_ != nullptr) {
    connect(engine_, &PlaybackEngine::frameChanged, this, &QQuickItem::update);
    // Keep the scene graph producing frames while playing.
    connect(engine_, &PlaybackEngine::playingChanged, this, [this] {
      if (window() != nullptr && engine_ != nullptr && engine_->playing())
        window()->update();
    });
    connectEngine();
  }
  emit engineChanged();
  update();
}

void VideoView::setValue(double &field, const double value) {
  if (!std::isfinite(value) || field == value)
    return;
  field = value;
  emit viewChanged();
}

void VideoView::setMode(const int value) {
  if (mode_ == value || value < Rectilinear || value > Original)
    return;
  mode_ = value;
  emit viewChanged();
}

void VideoView::setSourceLayout(const int value) {
  if (source_layout_ == value)
    return;
  source_layout_ = value;
  emit viewChanged();
}

void VideoView::setOverlap(const int value) {
  if (overlap_ == value)
    return;
  overlap_ = value;
  emit viewChanged();
}

void VideoView::setPanoramaCrop(const QRectF &value) {
  if (panorama_crop_ == value || !value.isValid())
    return;
  panorama_crop_ = value;
  emit viewChanged();
}

void VideoView::setYaw(const double value) { setValue(yaw_, value); }
void VideoView::setPitch(const double value) { setValue(pitch_, value); }
void VideoView::setRoll(const double value) { setValue(roll_, value); }
void VideoView::setFieldOfView(const double value) { setValue(field_of_view_, value); }
void VideoView::setZoom(const double value) { setValue(zoom_, value > 0.0 ? value : zoom_); }
void VideoView::setPanX(const double value) { setValue(pan_x_, value); }
void VideoView::setPanY(const double value) { setValue(pan_y_, value); }

bool VideoView::gpuAvailable() {
  // Qt 6.2 keeps reporting OpenGL after selecting the software backend, so check both.
  return QQuickWindow::graphicsApi() == QSGRendererInterface::OpenGL &&
         QQuickWindow::sceneGraphBackend() != QStringLiteral("software");
}

QSGNode *VideoView::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
  if (!gpuAvailable()) {
    auto *node = static_cast<QSGSimpleRectNode *>(old);
    if (node == nullptr)
      node = new QSGSimpleRectNode;
    node->setRect(boundingRect());
    node->setColor(QColor(9, 13, 17));
    return node;
  }
  auto *node = static_cast<VideoRenderNode *>(old);
  if (node == nullptr)
    node = new VideoRenderNode;
  gb::render::ViewParameters parameters;
  parameters.mode = mode_;
  parameters.layout = source_layout_;
  parameters.overlap = static_cast<float>(overlap_);
  parameters.panorama_crop = panorama_crop_;
  parameters.yaw = radians(yaw_);
  parameters.pitch = radians(pitch_);
  parameters.roll = radians(roll_);
  parameters.field_of_view = radians(field_of_view_);
  parameters.zoom = static_cast<float>(zoom_);
  parameters.pan_x = static_cast<float>(pan_x_);
  parameters.pan_y = static_cast<float>(pan_y_);
  // On screen the canvas is the item itself, drawn whole.
  parameters.canvas = size();
  node->synchronize(engine_.data(), engine_ != nullptr ? engine_->current() : PresentedFrame{},
                    parameters);
  node->markDirty(QSGNode::DirtyMaterial);
  return node;
}
