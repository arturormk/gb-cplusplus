#include "video_view.hpp"

#include "playback_engine.hpp"

#include "gb/media/playback.hpp"

#include <QElapsedTimer>
#include <QFile>
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
#include <string>

#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
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

struct ViewParameters {
  int mode{};
  int layout{};
  float overlap{};
  float yaw{};
  float pitch{};
  float roll{};
  float field_of_view{};
  float zoom{1.0F};
  float pan_x{};
  float pan_y{};
  QSizeF size;
};

float radians(const double degrees) { return static_cast<float>(qDegreesToRadians(degrees)); }

class VideoRenderNode final : public QSGRenderNode {
public:
  ~VideoRenderNode() override {
    if (QOpenGLContext::currentContext() != nullptr) releaseResources();
  }

  void synchronize(PlaybackEngine *engine, PresentedFrame frame, const ViewParameters &parameters) {
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

  [[nodiscard]] QRectF rect() const override { return {QPointF(0.0, 0.0), parameters_.size}; }

  // Qt 6.2's RHI renderer only guarantees matrix() during prepare(); in render() it can point at
  // stale memory, so keep a copy.
  void prepare() override { model_matrix_ = *this->matrix(); }

  void releaseResources() override {
    auto *context = QOpenGLContext::currentContext();
    if (context == nullptr || !initialized_) return;
    auto *f = context->extraFunctions();
    if (program_ != 0U) f->glDeleteProgram(program_);
    if (buffer_ != 0U) f->glDeleteBuffers(1, &buffer_);
    if (vertex_array_ != 0U) f->glDeleteVertexArrays(1, &vertex_array_);
    f->glDeleteTextures(kTextureCount, textures_.data());
    if (timer_queries_) f->glDeleteQueries(kQueryCount, queries_.data());
    program_ = buffer_ = vertex_array_ = 0U;
    textures_.fill(0U);
    texture_sizes_.fill(QSize());
    initialized_ = false;
    uploaded_serial_ = 0;
  }

  void render(const RenderState *state) override {
    auto *context = QOpenGLContext::currentContext();
    if (context == nullptr) return;
    auto *f = context->extraFunctions();
    if (!initialized_ && !failed_) initialize(context, f);
    if (failed_) return;
    if (engine_ != nullptr && !renderer_reported_) {
      engine_->setRendererDescription(renderer_);
      renderer_reported_ = true;
    }

    GLint previous_program{};
    GLint previous_vertex_array{};
    GLint previous_buffer{};
    GLint previous_active{};
    GLint previous_row_length{};
    GLint previous_alignment{};
    std::array<GLint, kTextureCount> previous_textures{};
    f->glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    if (vertex_array_ != 0U) f->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vertex_array);
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
    if (upload && timer_queries_) query_slot = beginQuery(f);
    QElapsedTimer upload_timer;
    upload_timer.start();
    if (upload) uploadFrame(f);
    const auto upload_ms = static_cast<double>(upload_timer.nsecsElapsed()) / 1.0e6;

    draw(f, state);
    if (query_slot.has_value()) f->glEndQuery(GL_TIME_ELAPSED);

    if (upload) {
      uploaded_serial_ = frame_.serial;
      const auto gpu_ms = timer_queries_ ? collectQueries(f) : std::nullopt;
      if (engine_ != nullptr) engine_->reportRendered(frame_.serial, frame_.generation, upload_ms, gpu_ms);
    }

    for (int unit = 0; unit < kTextureCount; ++unit) {
      f->glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
      f->glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_textures[static_cast<std::size_t>(unit)]));
    }
    f->glActiveTexture(static_cast<GLenum>(previous_active));
    f->glPixelStorei(GL_UNPACK_ROW_LENGTH, previous_row_length);
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, previous_alignment);
    f->glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previous_buffer));
    if (vertex_array_ != 0U) f->glBindVertexArray(static_cast<GLuint>(previous_vertex_array));
    f->glUseProgram(static_cast<GLuint>(previous_program));
  }

private:
  GLuint compile(QOpenGLExtraFunctions *f, const GLenum type, const QString &resource,
                 const QByteArray &header) {
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly)) {
      qWarning("gb-player: cannot read shader %s", qPrintable(resource));
      return 0U;
    }
    const auto source = header + file.readAll();
    const auto shader = f->glCreateShader(type);
    const char *data = source.constData();
    f->glShaderSource(shader, 1, &data, nullptr);
    f->glCompileShader(shader);
    GLint compiled{};
    f->glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == 0) {
      std::array<char, 4096> log{};
      f->glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
      qWarning("gb-player: shader %s failed to compile:\n%s", qPrintable(resource), log.data());
      f->glDeleteShader(shader);
      return 0U;
    }
    return shader;
  }

  void initialize(QOpenGLContext *context, QOpenGLExtraFunctions *f) {
    const auto format = context->format();
    const auto es = context->isOpenGLES();
    const auto version = format.majorVersion() * 10 + format.minorVersion();
    renderer_ = std::string(reinterpret_cast<const char *>(f->glGetString(GL_RENDERER))) + " / " +
                reinterpret_cast<const char *>(f->glGetString(GL_VERSION));
    if ((es && version < 30) || (!es && version < 33)) {
      qWarning("gb-player: OpenGL 3.3 or OpenGL ES 3.0 is required; context is %d.%d",
               format.majorVersion(), format.minorVersion());
      renderer_ += " (unsupported)";
      failed_ = true;
      return;
    }
    const QByteArray header = es ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
                                 : "#version 330 core\n";
    const auto vertex = compile(f, GL_VERTEX_SHADER, QStringLiteral(":/shaders/player.vert"), header);
    const auto fragment = compile(f, GL_FRAGMENT_SHADER, QStringLiteral(":/shaders/player.frag"), header);
    if (vertex == 0U || fragment == 0U) {
      failed_ = true;
      return;
    }
    program_ = f->glCreateProgram();
    f->glAttachShader(program_, vertex);
    f->glAttachShader(program_, fragment);
    f->glBindAttribLocation(program_, 0, "aPosition");
    f->glLinkProgram(program_);
    f->glDeleteShader(vertex);
    f->glDeleteShader(fragment);
    GLint linked{};
    f->glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked == 0) {
      qWarning("gb-player: shader program failed to link");
      failed_ = true;
      return;
    }

    static constexpr std::array<GLfloat, 8> quad{0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
    f->glGenVertexArrays(1, &vertex_array_);
    f->glBindVertexArray(vertex_array_);
    f->glGenBuffers(1, &buffer_);
    f->glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    f->glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad.data(), GL_STATIC_DRAW);
    f->glEnableVertexAttribArray(0);
    f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    f->glBindVertexArray(0);

    f->glGenTextures(kTextureCount, textures_.data());
    const std::array<std::uint8_t, 1> black{0U};
    for (const auto texture : textures_) {
      f->glBindTexture(GL_TEXTURE_2D, texture);
      f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      f->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, black.data());
    }

    f->glUseProgram(program_);
    static constexpr std::array<const char *, kTextureCount> samplers{"uY0", "uU0", "uV0",
                                                                      "uY1", "uU1", "uV1"};
    for (int unit = 0; unit < kTextureCount; ++unit) {
      f->glUniform1i(f->glGetUniformLocation(program_, samplers[static_cast<std::size_t>(unit)]), unit);
    }
    for (std::size_t index = 0; index < uniform_names_.size(); ++index) {
      uniforms_[index] = f->glGetUniformLocation(program_, uniform_names_[index]);
    }

    // Timer queries are a desktop OpenGL 3.3 feature; ES contexts report no GPU timing.
    timer_queries_ = !es;
    if (timer_queries_) {
      f->glGenQueries(kQueryCount, queries_.data());
      query_pending_.fill(false);
    }
    initialized_ = true;
  }

  std::optional<int> beginQuery(QOpenGLExtraFunctions *f) {
    const auto slot = next_query_;
    if (query_pending_[static_cast<std::size_t>(slot)]) return std::nullopt;
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
      if (!query_pending_[slot]) continue;
      GLuint available{};
      f->glGetQueryObjectuiv(queries_[slot], GL_QUERY_RESULT_AVAILABLE, &available);
      if (available == 0U) continue;
      GLuint nanoseconds{};
      f->glGetQueryObjectuiv(queries_[slot], GL_QUERY_RESULT, &nanoseconds);
      query_pending_[slot] = false;
      latest = static_cast<double>(nanoseconds) / 1.0e6;
    }
    return latest;
  }

  void uploadPlane(QOpenGLExtraFunctions *f, const int unit, const int width, const int height,
                   const std::uint8_t *data, const int stride) {
    const auto index = static_cast<std::size_t>(unit);
    f->glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    f->glBindTexture(GL_TEXTURE_2D, textures_[index]);
    f->glPixelStorei(GL_UNPACK_ROW_LENGTH, stride);
    if (texture_sizes_[index] != QSize(width, height)) {
      f->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data);
      texture_sizes_[index] = QSize(width, height);
    } else {
      f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_UNSIGNED_BYTE, data);
    }
  }

  void uploadFrame(QOpenGLExtraFunctions *f) {
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const auto &pictures = frame_.frames->pictures;
    for (std::size_t stream = 0; stream < pictures.size() && stream < 2U; ++stream) {
      const auto &picture = pictures[stream];
      const auto chroma_width = (picture.width + 1) / 2;
      const auto chroma_height = (picture.height + 1) / 2;
      const auto base = static_cast<int>(stream) * 3;
      uploadPlane(f, base, picture.width, picture.height, picture.planes[0], picture.strides[0]);
      uploadPlane(f, base + 1, chroma_width, chroma_height, picture.planes[1], picture.strides[1]);
      uploadPlane(f, base + 2, chroma_width, chroma_height, picture.planes[2], picture.strides[2]);
    }
    f->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  }

  void draw(QOpenGLExtraFunctions *f, const RenderState *state) {
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
    f->glUseProgram(program_);
    for (int unit = 0; unit < kTextureCount; ++unit) {
      f->glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
      f->glBindTexture(GL_TEXTURE_2D, textures_[static_cast<std::size_t>(unit)]);
    }
    const auto matrix = *state->projectionMatrix() * model_matrix_;
    const auto *picture = frame_.frames && !frame_.frames->pictures.empty()
                              ? &frame_.frames->pictures.front()
                              : nullptr;
    f->glUniformMatrix4fv(uniform(Uniform::matrix), 1, GL_FALSE, matrix.constData());
    f->glUniform2f(uniform(Uniform::size), static_cast<float>(parameters_.size.width()),
                   static_cast<float>(parameters_.size.height()));
    f->glUniform2f(uniform(Uniform::source), picture ? static_cast<float>(picture->width) : 1.0F,
                   picture ? static_cast<float>(picture->height) : 1.0F);
    f->glUniform1i(uniform(Uniform::mode), parameters_.mode);
    f->glUniform1i(uniform(Uniform::layout), parameters_.layout);
    f->glUniform1i(uniform(Uniform::has_frame), picture != nullptr ? 1 : 0);
    f->glUniform1f(uniform(Uniform::overlap), parameters_.overlap);
    f->glUniform1f(uniform(Uniform::yaw), parameters_.yaw);
    f->glUniform1f(uniform(Uniform::pitch), parameters_.pitch);
    f->glUniform1f(uniform(Uniform::roll), parameters_.roll);
    f->glUniform1f(uniform(Uniform::field_of_view), parameters_.field_of_view);
    f->glUniform1f(uniform(Uniform::zoom), parameters_.zoom);
    f->glUniform2f(uniform(Uniform::pan), parameters_.pan_x, parameters_.pan_y);
    f->glUniform1f(uniform(Uniform::luma_red), picture ? static_cast<float>(picture->luma_red) : 0.2126F);
    f->glUniform1f(uniform(Uniform::luma_blue), picture ? static_cast<float>(picture->luma_blue) : 0.0722F);
    f->glUniform1i(uniform(Uniform::full_range), picture != nullptr && picture->full_range ? 1 : 0);
    f->glBindVertexArray(vertex_array_);
    f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }

  enum class Uniform {
    matrix, size, source, mode, layout, has_frame, overlap, yaw, pitch, roll, field_of_view,
    zoom, pan, luma_red, luma_blue, full_range, count
  };
  static constexpr std::array<const char *, static_cast<std::size_t>(Uniform::count)> uniform_names_{
      "uMatrix", "uSize", "uSource", "uMode", "uLayout", "uHasFrame", "uOverlap", "uYaw", "uPitch",
      "uRoll", "uFieldOfView", "uZoom", "uPan", "uKr", "uKb", "uFullRange"};
  [[nodiscard]] GLint uniform(const Uniform name) const {
    return uniforms_[static_cast<std::size_t>(name)];
  }

  PlaybackEngine *engine_{};
  PresentedFrame frame_;
  QMatrix4x4 model_matrix_;
  ViewParameters parameters_;
  bool initialized_{};
  bool failed_{};
  bool renderer_reported_{};
  bool timer_queries_{};
  std::string renderer_;
  GLuint program_{};
  GLuint buffer_{};
  GLuint vertex_array_{};
  std::array<GLuint, kTextureCount> textures_{};
  std::array<QSize, kTextureCount> texture_sizes_{};
  std::array<GLint, static_cast<std::size_t>(Uniform::count)> uniforms_{};
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
      if (engine_ == nullptr || !engine_->playing()) return;
      engine_->advanceFrame();
      window->update();
    });
  }
  connectEngine();
}

void VideoView::connectEngine() {
  if (engine_ == nullptr) return;
  engine_->setFrameDriven(window() != nullptr && gpuAvailable());
}

QObject *VideoView::engine() const { return engine_; }

void VideoView::setEngine(QObject *engine) {
  auto *typed = qobject_cast<PlaybackEngine *>(engine);
  if (typed == engine_) return;
  if (engine_ != nullptr) disconnect(engine_, nullptr, this, nullptr);
  engine_ = typed;
  if (engine_ != nullptr) {
    connect(engine_, &PlaybackEngine::frameChanged, this, &QQuickItem::update);
    // Keep the scene graph producing frames while playing.
    connect(engine_, &PlaybackEngine::playingChanged, this, [this] {
      if (window() != nullptr && engine_ != nullptr && engine_->playing()) window()->update();
    });
    connectEngine();
  }
  emit engineChanged();
  update();
}

void VideoView::setValue(double &field, const double value) {
  if (!std::isfinite(value) || field == value) return;
  field = value;
  emit viewChanged();
}

void VideoView::setMode(const int value) {
  if (mode_ == value || value < Rectilinear || value > Original) return;
  mode_ = value;
  emit viewChanged();
}

void VideoView::setSourceLayout(const int value) {
  if (source_layout_ == value) return;
  source_layout_ = value;
  emit viewChanged();
}

void VideoView::setOverlap(const int value) {
  if (overlap_ == value) return;
  overlap_ = value;
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
    if (node == nullptr) node = new QSGSimpleRectNode;
    node->setRect(boundingRect());
    node->setColor(QColor(9, 13, 17));
    return node;
  }
  auto *node = static_cast<VideoRenderNode *>(old);
  if (node == nullptr) node = new VideoRenderNode;
  ViewParameters parameters;
  parameters.mode = mode_;
  parameters.layout = source_layout_;
  parameters.overlap = static_cast<float>(overlap_);
  parameters.yaw = radians(yaw_);
  parameters.pitch = radians(pitch_);
  parameters.roll = radians(roll_);
  parameters.field_of_view = radians(field_of_view_);
  parameters.zoom = static_cast<float>(zoom_);
  parameters.pan_x = static_cast<float>(pan_x_);
  parameters.pan_y = static_cast<float>(pan_y_);
  parameters.size = size();
  node->synchronize(engine_.data(), engine_ != nullptr ? engine_->current() : PresentedFrame{},
                    parameters);
  node->markDirty(QSGNode::DirtyMaterial);
  return node;
}
