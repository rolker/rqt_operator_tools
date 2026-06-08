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

#ifndef RQT_SONAR_WATERFALL__GPU_COLOR_MAP_HPP_
#define RQT_SONAR_WATERFALL__GPU_COLOR_MAP_HPP_

#include <QByteArray>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>

#include <memory>

#include "rqt_sonar_waterfall/color_map.hpp"

class QOpenGLShaderProgram;

namespace rqt_sonar_waterfall
{

/// GPU colormap renderer for the waterfall.
///
/// Renders a scalar intensity texture (`GL_R32F`, full precision — not bound to
/// 8-bit data) to the currently bound framebuffer, applying the shared
/// `marine_colormap` transfer in a fragment shader: `normalize(value, min, max)`
/// then the gain/contrast `response`, both from `marine_colormap::colormap_glsl()`
/// so the GPU math matches the CPU `scale_intensity()` exactly. The palette is a
/// 1-D LUT texture baked once from `marine_colormap::bake_lut()` (identity
/// transfer — gain/contrast are shader uniforms, so re-ranging is free and the
/// LUT only re-bakes on a palette change).
///
/// Deliberately owns **no** framebuffer: the QOpenGLWidget binds its default FBO
/// and the parity test binds an offscreen FBO before calling draw(). All methods
/// require a current OpenGL 3.3 context.
///
/// Parity with the CPU path (`scale_intensity()` + `ColorMap::lookup()`) is the
/// design contract, validated by `test_gpu_color_map` (the offscreen-GL CPU/GPU
/// parity check deferred from marine_colormap#5).
class GpuColorMap : protected QOpenGLFunctions_3_3_Core
{
public:
  GpuColorMap();
  ~GpuColorMap();

  GpuColorMap(const GpuColorMap &) = delete;
  GpuColorMap & operator=(const GpuColorMap &) = delete;

  /// Release all GL objects (program, VBO, VAO, LUT texture). Must be called
  /// with the owning context current. The owner (the QOpenGLWidget) calls this
  /// from its destructor while the context is current, because a GpuColorMap
  /// *member* would otherwise be destroyed after the widget has already released
  /// its context, leaving the GL deletes to run against no/other context.
  /// Idempotent and safe to call when never initialized.
  void cleanup();

  /// Compile/link the program and create the quad geometry. Must be called with a
  /// current GL context. Returns false (and logs the shader log) on failure.
  bool initialize();
  bool initialized() const {return program_ != nullptr;}

  /// Upload the palette as a LUT texture (identity transfer; gain/contrast stay
  /// uniforms). Re-call only when the palette changes. `lut_size` is clamped to
  /// at least 2. Requires a current GL context.
  void set_palette(ColorMapType type, int lut_size = 256);

  void set_range(float min, float max)
  {
    min_ = min;
    max_ = max;
  }
  void set_gain(float gain) {gain_ = gain;}
  void set_contrast(float contrast) {contrast_ = contrast;}

  /// Draw a viewport-filling quad that samples `intensity_tex` (a `GL_R32F` 2-D
  /// texture) and writes colormapped RGBA to the bound framebuffer/viewport.
  /// `flip_v` flips the sampled V coordinate (texture-row vs screen-row order).
  void draw(unsigned int intensity_tex, bool flip_v = false);

  /// The assembled fragment-shader source (colormap_glsl() + preamble + main),
  /// exposed for tests / debugging.
  static QByteArray fragment_source();

private:
  std::unique_ptr<QOpenGLShaderProgram> program_;
  QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject vao_;
  unsigned int lut_tex_ = 0;
  float lut_size_ = 0.0f;
  float min_ = 0.0f;
  float max_ = 1.0f;
  float gain_ = 1.0f;
  float contrast_ = 1.0f;
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__GPU_COLOR_MAP_HPP_
