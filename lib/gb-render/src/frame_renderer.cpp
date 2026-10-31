#include "gb/render/frame_renderer.hpp"

#include "gb/media/playback.hpp"
#include "gb_render_shaders.hpp"

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>

#include <vector>

#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

namespace gb::render {
namespace {

constexpr std::array<const char *, 18> kUniformNames{
    "uMatrix",      "uSize",    "uViewCrop", "uSource", "uMode",  "uLayout",
    "uHasFrame",    "uOverlap", "uCrop",     "uYaw",    "uPitch", "uRoll",
    "uFieldOfView", "uZoom",    "uPan",      "uKr",     "uKb",    "uFullRange"};

} // namespace

unsigned FrameRenderer::compile(QOpenGLExtraFunctions *f, const unsigned type, const char *body,
                                const char *name) {
  const char *header = es_ ? "#version 300 es\nprecision highp float;\nprecision highp int;\n"
                           : "#version 330 core\n";
  std::array<const char *, 2> sources{header, body};
  const auto shader = f->glCreateShader(type);
  f->glShaderSource(shader, 2, sources.data(), nullptr);
  f->glCompileShader(shader);
  GLint compiled{};
  f->glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled == 0) {
    std::array<char, 4096> log{};
    f->glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
    error_ = std::string("shader ") + name + " failed to compile:\n" + log.data();
    f->glDeleteShader(shader);
    return 0U;
  }
  return shader;
}

bool FrameRenderer::initialize(QOpenGLContext *context) {
  if (initialized_)
    return true;
  if (failed_)
    return false;
  auto *f = context->extraFunctions();
  const auto format = context->format();
  es_ = context->isOpenGLES();
  const auto version = format.majorVersion() * 10 + format.minorVersion();
  const auto *renderer = reinterpret_cast<const char *>(f->glGetString(GL_RENDERER));
  const auto *gl_version = reinterpret_cast<const char *>(f->glGetString(GL_VERSION));
  description_ = std::string(renderer != nullptr ? renderer : "unknown renderer") + " / " +
                 (gl_version != nullptr ? gl_version : "unknown version");
  if ((es_ && version < 30) || (!es_ && version < 33)) {
    error_ = "OpenGL 3.3 or OpenGL ES 3.0 is required; context is " +
             std::to_string(format.majorVersion()) + "." + std::to_string(format.minorVersion());
    description_ += " (unsupported)";
    failed_ = true;
    return false;
  }
  const auto vertex = compile(f, GL_VERTEX_SHADER, kFrameVertexShader, "frame.vert");
  const auto fragment = compile(f, GL_FRAGMENT_SHADER, kFrameFragmentShader, "frame.frag");
  if (vertex == 0U || fragment == 0U) {
    if (vertex != 0U)
      f->glDeleteShader(vertex);
    if (fragment != 0U)
      f->glDeleteShader(fragment);
    failed_ = true;
    return false;
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
    error_ = "shader program failed to link";
    f->glDeleteProgram(program_);
    program_ = 0U;
    failed_ = true;
    return false;
  }

  GLint previous_vertex_array{};
  GLint previous_buffer{};
  GLint previous_program{};
  f->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vertex_array);
  f->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_buffer);
  f->glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);

  static constexpr std::array<GLfloat, 8> quad{0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
  f->glGenVertexArrays(1, &vertex_array_);
  f->glBindVertexArray(vertex_array_);
  f->glGenBuffers(1, &buffer_);
  f->glBindBuffer(GL_ARRAY_BUFFER, buffer_);
  f->glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad.data(), GL_STATIC_DRAW);
  f->glEnableVertexAttribArray(0);
  f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  f->glBindVertexArray(static_cast<GLuint>(previous_vertex_array));
  f->glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previous_buffer));

  f->glGenTextures(kTextureCount, textures_.data());
  const std::array<std::uint8_t, 1> black{0U};
  GLint previous_alignment{};
  f->glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_alignment);
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  for (const auto texture : textures_) {
    f->glBindTexture(GL_TEXTURE_2D, texture);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, black.data());
  }
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, previous_alignment);

  f->glUseProgram(program_);
  static constexpr std::array<const char *, kTextureCount> samplers{"uY0", "uU0", "uV0",
                                                                    "uY1", "uU1", "uV1"};
  for (int unit = 0; unit < kTextureCount; ++unit) {
    f->glUniform1i(f->glGetUniformLocation(program_, samplers[static_cast<std::size_t>(unit)]),
                   unit);
  }
  for (std::size_t index = 0; index < kUniformNames.size(); ++index)
    uniforms_[index] = f->glGetUniformLocation(program_, kUniformNames[index]);
  f->glUseProgram(static_cast<GLuint>(previous_program));
  initialized_ = true;
  return true;
}

void FrameRenderer::uploadPlane(QOpenGLExtraFunctions *f, const int unit, const int width,
                                const int height, const std::uint8_t *data, const int stride) {
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

void FrameRenderer::upload(QOpenGLExtraFunctions *f, const gb::media::FrameSet &frames) {
  if (!initialized_)
    return;
  f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  const auto &pictures = frames.pictures;
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
  has_frame_ = !pictures.empty();
  if (has_frame_) {
    const auto &first = pictures.front();
    source_size_ = QSize(first.width, first.height);
    luma_red_ = static_cast<float>(first.luma_red);
    luma_blue_ = static_cast<float>(first.luma_blue);
    full_range_ = first.full_range;
  }
}

void FrameRenderer::draw(QOpenGLExtraFunctions *f, const ViewParameters &parameters,
                         const QMatrix4x4 &matrix) {
  if (!initialized_)
    return;
  f->glUseProgram(program_);
  for (int unit = 0; unit < kTextureCount; ++unit) {
    f->glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    f->glBindTexture(GL_TEXTURE_2D, textures_[static_cast<std::size_t>(unit)]);
  }
  const auto value = [](const double number) { return static_cast<float>(number); };
  f->glUniformMatrix4fv(uniform(Uniform::matrix), 1, GL_FALSE, matrix.constData());
  f->glUniform2f(uniform(Uniform::size), value(parameters.canvas.width()),
                 value(parameters.canvas.height()));
  f->glUniform4f(uniform(Uniform::view_crop), value(parameters.view_crop.x()),
                 value(parameters.view_crop.y()), value(parameters.view_crop.width()),
                 value(parameters.view_crop.height()));
  f->glUniform2f(uniform(Uniform::source), value(source_size_.width()),
                 value(source_size_.height()));
  f->glUniform1i(uniform(Uniform::mode), parameters.mode);
  f->glUniform1i(uniform(Uniform::layout), parameters.layout);
  f->glUniform1i(uniform(Uniform::has_frame), has_frame_ ? 1 : 0);
  f->glUniform1f(uniform(Uniform::overlap), parameters.overlap);
  f->glUniform4f(uniform(Uniform::crop), value(parameters.panorama_crop.x()),
                 value(parameters.panorama_crop.y()), value(parameters.panorama_crop.width()),
                 value(parameters.panorama_crop.height()));
  f->glUniform1f(uniform(Uniform::yaw), parameters.yaw);
  f->glUniform1f(uniform(Uniform::pitch), parameters.pitch);
  f->glUniform1f(uniform(Uniform::roll), parameters.roll);
  f->glUniform1f(uniform(Uniform::field_of_view), parameters.field_of_view);
  f->glUniform1f(uniform(Uniform::zoom), parameters.zoom);
  f->glUniform2f(uniform(Uniform::pan), parameters.pan_x, parameters.pan_y);
  f->glUniform1f(uniform(Uniform::luma_red), luma_red_);
  f->glUniform1f(uniform(Uniform::luma_blue), luma_blue_);
  f->glUniform1i(uniform(Uniform::full_range), full_range_ ? 1 : 0);
  f->glBindVertexArray(vertex_array_);
  f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void FrameRenderer::release(QOpenGLExtraFunctions *f) {
  if (!initialized_)
    return;
  if (program_ != 0U)
    f->glDeleteProgram(program_);
  if (buffer_ != 0U)
    f->glDeleteBuffers(1, &buffer_);
  if (vertex_array_ != 0U)
    f->glDeleteVertexArrays(1, &vertex_array_);
  f->glDeleteTextures(kTextureCount, textures_.data());
  program_ = buffer_ = vertex_array_ = 0U;
  textures_.fill(0U);
  texture_sizes_.fill(QSize());
  initialized_ = false;
  has_frame_ = false;
}

} // namespace gb::render
