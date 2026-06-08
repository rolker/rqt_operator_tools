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

#include "rqt_sonar_waterfall/waterfall_widget.hpp"

#include <QColor>
#include <QPainter>
#include <QString>
#include <QSurfaceFormat>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <utility>
#include <vector>

namespace rqt_sonar_waterfall
{

WaterfallWidget::WaterfallWidget(QWidget * parent)
: QOpenGLWidget(parent)
{
  // Request a 3.3 context. Compatibility profile so the QPainter label overlay
  // (legacy GL paint engine) coexists with GpuColorMap's modern shader path.
  QSurfaceFormat fmt = format();
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  setFormat(fmt);
  setMinimumSize(256, 256);
}

WaterfallWidget::~WaterfallWidget()
{
  // Release GL objects with our context current. Guard against the widget never
  // having had a context (never shown / headless) or it already being gone --
  // makeCurrent() on an absent context is unsafe. gpu_.cleanup() must run here,
  // while the context is current, because the GpuColorMap member is otherwise
  // destroyed after this body (and after the context is released).
  if (context() != nullptr && context()->isValid()) {
    makeCurrent();
    if (intensity_tex_ != 0) {
      glDeleteTextures(1, &intensity_tex_);
      intensity_tex_ = 0;
    }
    gpu_.cleanup();
    doneCurrent();
  }
}

void WaterfallWidget::add_row(const WaterfallRow & row)
{
  if (frozen_) {
    return;
  }
  WaterfallRow stored = row;
  // Cache the row's intensity extremes once so auto-range scans two numbers per
  // row (O(rows)) instead of every sample on each ping.
  std::tie(stored.min_intensity, stored.max_intensity) = row_min_max(stored);
  stored.has_intensity_range = !stored.intensities.empty();
  buffer_.push(std::move(stored));
  data_dirty_ = true;
  update();
}

void WaterfallWidget::clear()
{
  buffer_.clear();
  data_dirty_ = true;
  range_max_ = 0.0;
  update();
}

void WaterfallWidget::set_color_map(ColorMapType type)
{
  color_map_type_ = type;
  palette_dirty_ = true;
  update();
}

void WaterfallWidget::set_gain(float gain)
{
  gain_ = gain;
  update();
}

void WaterfallWidget::set_contrast(float contrast)
{
  contrast_ = contrast;
  update();
}

void WaterfallWidget::set_history(std::size_t rows)
{
  buffer_.set_capacity(rows);
  data_dirty_ = true;
  update();
}

void WaterfallWidget::set_frozen(bool frozen)
{
  frozen_ = frozen;
}

void WaterfallWidget::set_auto_range(bool enabled)
{
  auto_range_ = enabled;
  update();
}

void WaterfallWidget::set_manual_range(float min, float max)
{
  // Selecting a manual range implies leaving auto-range, otherwise the shader
  // would keep getting the data-derived min/max.
  auto_range_ = false;
  manual_min_ = min;
  manual_max_ = max;
  update();
}

std::pair<float, float> WaterfallWidget::row_min_max(const WaterfallRow & row)
{
  if (row.intensities.empty()) {
    return {0.0f, 1.0f};
  }
  float lo = std::numeric_limits<float>::max();
  float hi = std::numeric_limits<float>::lowest();
  for (float v : row.intensities) {
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  return {lo, hi};
}

std::pair<float, float> WaterfallWidget::active_range() const
{
  if (auto_range_) {
    return auto_range(buffer_.rows());
  }
  return {manual_min_, manual_max_};
}

void WaterfallWidget::initializeGL()
{
  initializeOpenGLFunctions();
  if (!gpu_.initialize()) {
    // Shader failed to build; paintGL falls back to the cleared placeholder.
    gl_ready_ = false;
    return;
  }
  gpu_.set_palette(color_map_type_);
  palette_dirty_ = false;
  data_dirty_ = true;  // upload whatever is already buffered on first paint
  gl_ready_ = true;
}

void WaterfallWidget::resizeGL(int w, int h)
{
  glViewport(0, 0, w, h);
}

void WaterfallWidget::upload_texture()
{
  const auto & rows = buffer_.rows();

  std::size_t width = 0;
  for (const auto & row : rows) {
    width = std::max(width, row.intensities.size());
  }
  if (rows.empty() || width == 0) {
    has_data_ = false;
    range_max_ = 0.0;
    return;
  }

  // Clamp to the GPU's max texture size so a very wide ping or a very deep
  // history can't push glTexImage2D past the limit (which would fail and leave a
  // broken texture rendered as if valid). Keep the newest rows; downsample width.
  GLint max_tex = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
  const std::size_t max_dim = (max_tex > 0) ? static_cast<std::size_t>(max_tex) : 2048;
  std::size_t height = rows.size();
  std::size_t first_row = 0;
  bool clamped = false;
  if (width > max_dim) {
    width = max_dim;
    clamped = true;
  }
  if (height > max_dim) {
    first_row = height - max_dim;  // keep the newest max_dim rows
    height = max_dim;
    clamped = true;
  }
  if (clamped) {
    static bool warned = false;
    if (!warned) {
      qWarning(
        "WaterfallWidget: texture clamped to GL_MAX_TEXTURE_SIZE (%d); "
        "reduce history depth or sample count.", max_tex);
      warned = true;
    }
  }

  // The GL_MAX_TEXTURE_SIZE clamp bounds each dimension, but width*height can
  // still be a very large host allocation (up to ~max^2 floats). Fall back to
  // the placeholder rather than letting a pathological history/width OOM-abort
  // the UI.
  std::vector<float> data;
  try {
    data.assign(width * height, 0.0f);
  } catch (const std::exception &) {
    static bool warned = false;
    if (!warned) {
      qWarning(
        "WaterfallWidget: intensity staging buffer allocation failed (%zux%zu); "
        "reduce history depth or sample count.",
        width, height);
      warned = true;
    }
    has_data_ = false;
    range_max_ = 0.0;
    return;
  }
  // Store oldest-first (front -> row 0). Screen-top is texture v=1 = the last
  // row = newest, so no V flip is needed at draw time.
  for (std::size_t y = 0; y < height; ++y) {
    const WaterfallRow & row = rows[first_row + y];
    const std::size_t rw = row.intensities.size();
    float * dst = data.data() + y * width;
    for (std::size_t x = 0; x < width; ++x) {
      std::size_t sample = 0;
      if (rw > 1 && width > 1) {
        sample = static_cast<std::size_t>(
          std::lround(static_cast<double>(x) * (rw - 1) / (width - 1)));
      }
      dst[x] = (rw > 0) ? row.intensities[sample] : 0.0f;
    }
  }

  if (intensity_tex_ == 0) {
    glGenTextures(1, &intensity_tex_);
  }
  glBindTexture(GL_TEXTURE_2D, intensity_tex_);
  glGetError();  // clear any prior error so the check below is about this upload
  glTexImage2D(
    GL_TEXTURE_2D, 0, GL_R32F, static_cast<int>(width), static_cast<int>(height), 0, GL_RED,
    GL_FLOAT, data.data());
  const GLenum upload_err = glGetError();
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (upload_err != GL_NO_ERROR) {
    // Don't render a half-valid texture as if it were data: fall back to the
    // placeholder rather than show garbage.
    static bool warned = false;
    if (!warned) {
      qWarning("WaterfallWidget: intensity texture upload failed (GL error 0x%x).", upload_err);
      warned = true;
    }
    has_data_ = false;
    range_max_ = 0.0;
    return;
  }

  has_data_ = true;
  range_max_ = rows.back().range_max;  // newest row
}

void WaterfallWidget::paintGL()
{
  glClearColor(20.0f / 255.0f, 20.0f / 255.0f, 24.0f / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  if (gl_ready_) {
    if (palette_dirty_) {
      gpu_.set_palette(color_map_type_);
      palette_dirty_ = false;
    }
    if (data_dirty_) {
      upload_texture();
      data_dirty_ = false;
    }
    if (has_data_) {
      const auto [lo, hi] = active_range();
      gpu_.set_range(lo, hi);
      gpu_.set_gain(gain_);
      gpu_.set_contrast(contrast_);
      gpu_.draw(intensity_tex_, /*flip_v=*/false);
    }
  }

  // Text overlay on top of the GL frame.
  QPainter painter(this);
  if (!has_data_) {
    painter.setPen(QColor(120, 120, 130));
    painter.drawText(rect(), Qt::AlignCenter, tr("No sonar data"));
  } else if (range_max_ > 0.0) {
    painter.setPen(QColor(220, 220, 220));
    const QString label = tr("%1 m").arg(range_max_, 0, 'f', 1);
    // Outer edges are max slant range; the center column is nadir.
    painter.drawText(rect().adjusted(4, 2, 0, 0), Qt::AlignTop | Qt::AlignLeft, label);
    painter.drawText(rect().adjusted(0, 2, -4, 0), Qt::AlignTop | Qt::AlignRight, label);
  }
}

}  // namespace rqt_sonar_waterfall
