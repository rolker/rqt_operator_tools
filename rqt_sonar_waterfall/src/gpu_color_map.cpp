// Copyright 2026 University of New Hampshire
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the University of New Hampshire nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "rqt_sonar_waterfall/gpu_color_map.hpp"

#include <QOpenGLContext>
#include <QOpenGLShaderProgram>

#include <algorithm>
#include <vector>

#include <marine_colormap/color.hpp>
#include <marine_colormap/colormap.hpp>
#include <marine_colormap/palette.hpp>
#include <marine_colormap/shader.hpp>
#include <marine_colormap/transfer.hpp>

namespace rqt_sonar_waterfall
{

namespace
{

// Same enum->palette-name mapping the CPU ColorMap uses (color_map.cpp), so the
// GPU and CPU paths resolve the identical shared palette.
const marine_colormap::Palette * shared_palette(ColorMapType type)
{
  switch (type) {
    case ColorMapType::Bronze:
      return marine_colormap::find_palette("bronze");
    case ColorMapType::Thermal:
      return marine_colormap::find_palette("thermal");
    case ColorMapType::Grayscale:
    default:
      return marine_colormap::find_palette("grayscale");
  }
}

const char * kVertexSource =
  "#version 330 core\n"
  "layout(location = 0) in vec2 a_pos;\n"
  "uniform int u_flip_v;\n"
  "out vec2 v_uv;\n"
  "void main()\n"
  "{\n"
  "  vec2 uv = a_pos * 0.5 + 0.5;\n"
  "  if (u_flip_v != 0) { uv.y = 1.0 - uv.y; }\n"
  "  v_uv = uv;\n"
  "  gl_Position = vec4(a_pos, 0.0, 1.0);\n"
  "}\n";

}  // namespace

GpuColorMap::GpuColorMap() = default;

GpuColorMap::~GpuColorMap()
{
  // The owner should call cleanup() with its context current (see cleanup()).
  // As a fallback, release here only if some context is current — deleting GL
  // objects with no current context is undefined. With no context we must leak
  // rather than risk a wrong-context delete / crash.
  if (QOpenGLContext::currentContext() != nullptr) {
    cleanup();
  }
}

void GpuColorMap::cleanup()
{
  // lut_tex_ != 0 implies set_palette() ran, which implies the GL functions were
  // initialized, so glDeleteTextures is safe to call here.
  if (lut_tex_ != 0) {
    glDeleteTextures(1, &lut_tex_);
    lut_tex_ = 0;
  }
  lut_size_ = 0.0f;
  vbo_.destroy();
  vao_.destroy();
  program_.reset();
}

QByteArray GpuColorMap::fragment_source()
{
  // Prepend our own preamble (version, precision, samplers, uniforms, varyings)
  // onto the version-agnostic shared math, then a main() that mirrors the CPU
  // scale_intensity() (normalize -> response) and samples the palette LUT.
  // Desktop GL 3.3 core: no `precision` statement (that's a GLES-only need, per
  // marine_colormap shader.hpp). Mesa's desktop GLSL rejects it.
  QByteArray src =
    "#version 330 core\n"
    "uniform sampler2D u_intensity;\n"
    "uniform sampler2D u_lut;\n"
    "uniform float u_min;\n"
    "uniform float u_max;\n"
    "uniform float u_gain;\n"
    "uniform float u_contrast;\n"
    "uniform float u_lut_size;\n"
    // Ring-buffer mapping: the intensity texture is a fixed capacity-tall ring.
    // The u_ring_filled valid rows start (oldest) at row u_ring_oldest and wrap.
    // Screen V is mapped to an exact texel-center row so NEAREST sampling never
    // interpolates across the wrap seam.
    "uniform float u_ring_oldest;\n"
    "uniform float u_ring_filled;\n"
    "uniform float u_ring_capacity;\n"
    "in vec2 v_uv;\n"
    "out vec4 frag_color;\n";
  src += marine_colormap::colormap_glsl();
  src +=
    "\nvoid main()\n"
    "{\n"
    "  // Map screen V (oldest at bottom -> newest at top) to a ring texel row.\n"
    "  float ridx = clamp(floor(v_uv.y * u_ring_filled), 0.0, u_ring_filled - 1.0);\n"
    "  float trow = mod(u_ring_oldest + ridx, u_ring_capacity);\n"
    "  float vy = (trow + 0.5) / u_ring_capacity;\n"
    "  float value = texture(u_intensity, vec2(v_uv.x, vy)).r;\n"
    "  // Non-finite samples -> palette floor. Avoids GLSL-undefined clamp(NaN)\n"
    "  // and matches the CPU scale_intensity() path's clamping behavior.\n"
    "  if (isnan(value) || isinf(value)) { value = u_min; }\n"
    "  float t = marine_colormap_response(\n"
    "              marine_colormap_normalize(value, u_min, u_max), u_gain, u_contrast);\n"
    "  // Map t to LUT texel centers so GL_LINEAR reconstructs the same\n"
    "  // piecewise-linear palette the CPU samples (entry i = palette.sample(i/(N-1))).\n"
    "  float coord = (clamp(t, 0.0, 1.0) * (u_lut_size - 1.0) + 0.5) / u_lut_size;\n"
    "  frag_color = texture(u_lut, vec2(coord, 0.5));\n"
    "}\n";
  return src;
}

bool GpuColorMap::initialize()
{
  initializeOpenGLFunctions();

  program_ = std::make_unique<QOpenGLShaderProgram>();
  if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexSource)) {
    qWarning("GpuColorMap vertex shader failed: %s", qPrintable(program_->log()));
    program_.reset();
    return false;
  }
  if (!program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment_source())) {
    qWarning("GpuColorMap fragment shader failed: %s", qPrintable(program_->log()));
    program_.reset();
    return false;
  }
  if (!program_->link()) {
    qWarning("GpuColorMap link failed: %s", qPrintable(program_->log()));
    program_.reset();
    return false;
  }

  // A unit quad as a triangle strip: (-1,-1) (1,-1) (-1,1) (1,1).
  static const float kQuad[] = {
    -1.0f, -1.0f,
    1.0f, -1.0f,
    -1.0f, 1.0f,
    1.0f, 1.0f,
  };
  vao_.create();
  vao_.bind();
  vbo_.create();
  vbo_.bind();
  vbo_.allocate(kQuad, sizeof(kQuad));
  program_->enableAttributeArray(0);
  program_->setAttributeBuffer(0, GL_FLOAT, 0, 2, 2 * sizeof(float));
  vbo_.release();
  vao_.release();

  return true;
}

void GpuColorMap::set_palette(ColorMapType type, int lut_size)
{
  if (lut_size < 2) {
    lut_size = 2;
  }
  const marine_colormap::Palette * pal = shared_palette(type);
  if (pal == nullptr) {
    return;  // never happens for a built-in name; guarded so a lib change can't crash
  }

  // Identity transfer: bake only the palette. Gain/contrast/min/max are applied
  // in the shader as uniforms, so re-ranging never re-bakes the LUT.
  const std::vector<marine_colormap::Rgba8> lut =
    marine_colormap::bake_lut(*pal, marine_colormap::TransferParams{},
      static_cast<std::size_t>(lut_size));

  if (lut_tex_ == 0) {
    glGenTextures(1, &lut_tex_);
  }
  glBindTexture(GL_TEXTURE_2D, lut_tex_);
  glTexImage2D(
    GL_TEXTURE_2D, 0, GL_RGBA8, lut_size, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, lut.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  lut_size_ = static_cast<float>(lut_size);
}

void GpuColorMap::draw(unsigned int intensity_tex, bool flip_v)
{
  if (program_ == nullptr || lut_tex_ == 0) {
    return;  // not initialized / no palette uploaded yet
  }
  program_->bind();
  program_->setUniformValue("u_min", min_);
  program_->setUniformValue("u_max", max_);
  program_->setUniformValue("u_gain", gain_);
  program_->setUniformValue("u_contrast", contrast_);
  program_->setUniformValue("u_lut_size", lut_size_);
  program_->setUniformValue("u_ring_oldest", ring_oldest_);
  program_->setUniformValue("u_ring_filled", ring_filled_);
  program_->setUniformValue("u_ring_capacity", ring_capacity_);
  program_->setUniformValue("u_flip_v", flip_v ? 1 : 0);
  program_->setUniformValue("u_intensity", 0);
  program_->setUniformValue("u_lut", 1);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, intensity_tex);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, lut_tex_);

  vao_.bind();
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  vao_.release();

  // Unbind both texture units and restore the active unit to 0 so we don't leak
  // GL state into whatever runs next in this context (e.g. the widget's QPainter
  // overlay drawn right after this pass).
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  program_->release();
}

}  // namespace rqt_sonar_waterfall
