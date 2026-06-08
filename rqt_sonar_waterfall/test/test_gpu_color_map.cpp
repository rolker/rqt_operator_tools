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

// Offscreen-GL CPU/GPU parity test for GpuColorMap. Renders a known intensity
// ramp through the marine_colormap fragment shader and asserts each pixel
// matches the CPU path (scale_intensity() + ColorMap::lookup()) within a small
// tolerance. This is the parity validation deferred from marine_colormap#5.
//
// Requires a real OpenGL 3.3 context. When none can be created (headless runner
// with no software-GL stack — see rqt_operator_tools#42), every test self-skips
// with GTEST_SKIP rather than failing the build.

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions_3_3_Core>
#include <QSurfaceFormat>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "rqt_sonar_waterfall/color_map.hpp"
#include "rqt_sonar_waterfall/gpu_color_map.hpp"

namespace
{

using rqt_sonar_waterfall::ColorMap;
using rqt_sonar_waterfall::ColorMapType;
using rqt_sonar_waterfall::GpuColorMap;

// Owns a current offscreen GL 3.3 context for the duration of a test, or reports
// that none could be created (so the test skips instead of failing).
class GlContext
{
public:
  bool create()
  {
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);

    surface_ = std::make_unique<QOffscreenSurface>();
    surface_->setFormat(fmt);
    surface_->create();
    if (!surface_->isValid()) {
      return false;
    }

    context_ = std::make_unique<QOpenGLContext>();
    context_->setFormat(fmt);
    if (!context_->create() || !context_->makeCurrent(surface_.get())) {
      return false;
    }
    // GL 3.3+ is required for GL_R32F + the `#version 330` shaders. A 3.0-3.2
    // context would not skip but would then fail shader compile, so require the
    // exact minimum here.
    const QSurfaceFormat got = context_->format();
    const bool have_33 =
      got.majorVersion() > 3 || (got.majorVersion() == 3 && got.minorVersion() >= 3);
    if (!have_33) {
      return false;
    }
    return true;
  }

  ~GlContext()
  {
    if (context_ && context_->isValid()) {
      context_->doneCurrent();
    }
  }

private:
  std::unique_ptr<QOffscreenSurface> surface_;
  std::unique_ptr<QOpenGLContext> context_;
};

// Build an R32F intensity texture (width x 1) from `values`.
unsigned int make_intensity_texture(
  QOpenGLFunctions_3_3_Core & gl,
  const std::vector<float> & values)
{
  unsigned int tex = 0;
  gl.glGenTextures(1, &tex);
  gl.glBindTexture(GL_TEXTURE_2D, tex);
  gl.glTexImage2D(
    GL_TEXTURE_2D, 0, GL_R32F, static_cast<int>(values.size()), 1, 0, GL_RED, GL_FLOAT,
    values.data());
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl.glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

struct Rgb8
{
  int r;
  int g;
  int b;
};

// Render `values` through GpuColorMap and read back the row of RGBA8 pixels.
// One output pixel per input value (FBO is width x 1, NEAREST sampling).
std::vector<Rgb8> render_gpu(
  QOpenGLFunctions_3_3_Core & gl, const std::vector<float> & values, ColorMapType type, float min,
  float max, float gain, float contrast)
{
  const int width = static_cast<int>(values.size());

  GpuColorMap cmap;
  if (!cmap.initialize()) {
    return {};  // shader compile/link failed -> signal "no result"
  }
  cmap.set_palette(type, 256);
  cmap.set_range(min, max);
  cmap.set_gain(gain);
  cmap.set_contrast(contrast);

  QOpenGLFramebufferObject fbo(width, 1);
  fbo.bind();
  gl.glViewport(0, 0, width, 1);
  gl.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  gl.glClear(GL_COLOR_BUFFER_BIT);

  const unsigned int intensity = make_intensity_texture(gl, values);
  cmap.draw(intensity, /*flip_v=*/false);
  gl.glFinish();

  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * 4);
  gl.glReadPixels(0, 0, width, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  fbo.release();
  gl.glDeleteTextures(1, &intensity);

  std::vector<Rgb8> out(width);
  for (int x = 0; x < width; ++x) {
    out[x] = {pixels[x * 4 + 0], pixels[x * 4 + 1], pixels[x * 4 + 2]};
  }
  return out;
}

class GpuColorMapTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "test_gpu_color_map";
      static char * argv[] = {arg0, nullptr};
      app_ = std::make_unique<QApplication>(argc, argv);
    }
    if (!ctx_.create()) {
      GTEST_SKIP() << "No OpenGL 3.3 context available (headless without software GL).";
    }
    gl_.initializeOpenGLFunctions();
  }

  // Assert the GPU render of `values` matches the CPU path within `tol` (per
  // channel, 0..255). A valid GL 3.3 context is guaranteed by SetUp(), so a
  // shader compile/link failure here is a real failure (ASSERT), not a skip.
  void expect_parity(
    const std::vector<float> & values, ColorMapType type, float min, float max, float gain,
    float contrast, int tol)
  {
    const std::vector<Rgb8> gpu = render_gpu(gl_, values, type, min, max, gain, contrast);
    // SetUp() already skipped if no GL 3.3 context exists, so reaching here means
    // a capable context is current: an empty result is a real shader
    // compile/link regression, not a reason to skip (which would hide it).
    ASSERT_FALSE(gpu.empty()) << "Shader failed to compile/link despite a valid GL 3.3 context.";
    ASSERT_EQ(gpu.size(), values.size());

    ColorMap cpu(type);
    for (std::size_t i = 0; i < values.size(); ++i) {
      const float t =
        rqt_sonar_waterfall::scale_intensity(values[i], min, max, gain, contrast);
      const rqt_sonar_waterfall::Rgb want = cpu.lookup(t);
      EXPECT_NEAR(gpu[i].r, want.r, tol) << "red @ value=" << values[i];
      EXPECT_NEAR(gpu[i].g, want.g, tol) << "green @ value=" << values[i];
      EXPECT_NEAR(gpu[i].b, want.b, tol) << "blue @ value=" << values[i];
    }
  }

  std::unique_ptr<QApplication> app_;
  GlContext ctx_;
  QOpenGLFunctions_3_3_Core gl_;
};

// A ramp across [0, 1] in N steps, the canonical normalized input.
std::vector<float> ramp(int n)
{
  std::vector<float> v(n);
  for (int i = 0; i < n; ++i) {
    v[i] = (n > 1) ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.0f;
  }
  return v;
}

}  // namespace

// Grayscale is a pure linear palette, so GL_LINEAR LUT reconstruction is near
// exact: only the double 8-bit quantization (LUT storage + framebuffer) shows.
TEST_F(GpuColorMapTest, GrayscaleMatchesCpu)
{
  expect_parity(ramp(64), ColorMapType::Grayscale, 0.0f, 1.0f, 1.0f, 1.0f, /*tol=*/2);
}

// Multi-hue palette: add headroom for piecewise-linear LUT interpolation near
// stop boundaries on top of the quantization.
TEST_F(GpuColorMapTest, ThermalMatchesCpu)
{
  expect_parity(ramp(64), ColorMapType::Thermal, 0.0f, 1.0f, 1.0f, 1.0f, /*tol=*/4);
}

TEST_F(GpuColorMapTest, BronzeMatchesCpu)
{
  expect_parity(ramp(64), ColorMapType::Bronze, 0.0f, 1.0f, 1.0f, 1.0f, /*tol=*/4);
}

// Exercise the shader normalize step with a non-[0,1] data range: raw 16-bit-ish
// values mapped through [min, max].
TEST_F(GpuColorMapTest, NormalizeOverDataRange)
{
  std::vector<float> v(64);
  for (int i = 0; i < 64; ++i) {
    v[i] = static_cast<float>(i) * 500.0f;  // 0 .. 31500
  }
  expect_parity(v, ColorMapType::Grayscale, 0.0f, 31500.0f, 1.0f, 1.0f, /*tol=*/2);
}

// Exercise the gain/contrast response branch (clip-before-gamma + gamma).
TEST_F(GpuColorMapTest, GainAndContrastMatchCpu)
{
  expect_parity(ramp(64), ColorMapType::Grayscale, 0.0f, 1.0f, 1.5f, 0.6f, /*tol=*/3);
}
