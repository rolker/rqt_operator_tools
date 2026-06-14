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
#include <QRect>
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
  // Precompute the TVG-corrected copy at ingest while TVG is enabled, so a later
  // on/off toggle is a buffer swap rather than a per-sample recompute. When TVG
  // is off this is skipped; ensure_tvg_cache() populates the backlog on enable.
  if (tvg_) {
    compute_row_tvg(stored);
  }
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

void WaterfallWidget::set_ground_range(bool enabled)
{
  ground_range_ = enabled;
  data_dirty_ = true;  // remap geometry
  update();
}

void WaterfallWidget::set_uniform_scale(bool enabled)
{
  uniform_scale_ = enabled;
  data_dirty_ = true;
  update();
}

void WaterfallWidget::set_range_lines(bool enabled)
{
  range_lines_ = enabled;
  update();  // overlay-only, no re-upload
}

void WaterfallWidget::set_range_line_density(float density)
{
  // Keep the line count sane regardless of operator input.
  range_line_density_ = std::clamp(density, 0.25f, 4.0f);
  update();
}

void WaterfallWidget::set_tvg(bool enabled)
{
  if (tvg_ == enabled) {
    return;
  }
  tvg_ = enabled;
  if (tvg_) {
    ensure_tvg_cache();  // populate the backlog so the swap has data
  }
  data_dirty_ = true;
  update();
}

void WaterfallWidget::set_tvg_slope(float slope)
{
  if (tvg_slope_ == slope) {
    return;
  }
  tvg_slope_ = slope;
  if (tvg_) {
    ensure_tvg_cache();  // slope changed -> re-derive the cache (rare)
    data_dirty_ = true;
  }
  update();
}

void WaterfallWidget::compute_row_tvg(WaterfallRow & row) const
{
  row.intensities_tvg = apply_tvg(
    row.intensities, row.nadir_index, row.range_max_port, row.range_max_stbd,
    static_cast<double>(tvg_slope_));
  float lo = std::numeric_limits<float>::max();
  float hi = std::numeric_limits<float>::lowest();
  for (float v : row.intensities_tvg) {
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  if (row.intensities_tvg.empty()) {
    lo = 0.0f;
    hi = 1.0f;
  }
  row.min_intensity_tvg = lo;
  row.max_intensity_tvg = hi;
  row.has_tvg = true;
  row.tvg_slope = static_cast<double>(tvg_slope_);
}

void WaterfallWidget::ensure_tvg_cache()
{
  for (auto & row : buffer_.mutable_rows()) {
    if (!row.has_tvg || row.tvg_slope != static_cast<double>(tvg_slope_)) {
      compute_row_tvg(row);
    }
  }
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

std::pair<float, float> WaterfallWidget::buffer_auto_range() const
{
  // Scan the active (raw or TVG) per-row cached extremes so auto-range tracks
  // whatever the shader is actually colormapping.
  float lo = std::numeric_limits<float>::max();
  float hi = std::numeric_limits<float>::lowest();
  bool any = false;
  for (const auto & row : buffer_.rows()) {
    const bool use_tvg = tvg_ && row.has_tvg;
    if (use_tvg || row.has_intensity_range) {
      lo = std::min(lo, use_tvg ? row.min_intensity_tvg : row.min_intensity);
      hi = std::max(hi, use_tvg ? row.max_intensity_tvg : row.max_intensity);
      any = true;
    }
  }
  if (!any) {
    return {0.0f, 1.0f};
  }
  return {lo, hi};
}

std::pair<float, float> WaterfallWidget::active_range() const
{
  if (auto_range_) {
    return buffer_auto_range();
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

namespace
{

// Per-row display geometry. When the ping carries real ranges we work in metres
// (slant or, with altitude, ground). When it doesn't (driver left sound-speed /
// sample-rate unset), we fall back to a unit-less sample axis so the row still
// renders centred — just without metric range lines.
struct RowGeom
{
  double range_port = 0.0;   ///< slant range (m) or port sample count (fallback)
  double range_stbd = 0.0;
  double altitude = 0.0;     ///< 0 disables water-column removal
  bool ground = false;       ///< ground projection (needs metric + altitude)
  bool metric = false;       ///< ranges are metres (vs sample counts)
  double half_width = 0.0;   ///< per-row display half-width (axis units)
};

RowGeom row_geom(const WaterfallRow & row, bool ground_enabled)
{
  RowGeom g;
  const std::size_t port_n = std::min(row.nadir_index, row.intensities.size());
  const std::size_t stbd_n = row.intensities.size() - port_n;
  g.metric = (row.range_max_port > 0.0 || row.range_max_stbd > 0.0);
  g.range_port = g.metric ? row.range_max_port : static_cast<double>(port_n);
  g.range_stbd = g.metric ? row.range_max_stbd : static_cast<double>(stbd_n);
  g.ground = ground_enabled && g.metric && row.altitude > 0.0;
  g.altitude = g.ground ? row.altitude : 0.0;
  const double dr_port = g.ground ? ground_range(g.range_port, g.altitude) : g.range_port;
  const double dr_stbd = g.ground ? ground_range(g.range_stbd, g.altitude) : g.range_stbd;
  g.half_width = std::max(dr_port, dr_stbd);
  return g;
}

// Snap a raw spacing to the nearest "nice" 1/2/5 x 10^n value for round labels.
double nice_interval(double raw)
{
  if (raw <= 0.0) {
    return 0.0;
  }
  const double e = std::floor(std::log10(raw));
  const double base = std::pow(10.0, e);
  const double f = raw / base;  // [1, 10)
  double nice = 10.0;
  if (f < 1.5) {
    nice = 1.0;
  } else if (f < 3.0) {
    nice = 2.0;
  } else if (f < 7.0) {
    nice = 5.0;
  }
  return nice * base;
}

}  // namespace

void WaterfallWidget::upload_texture()
{
  const auto & rows = buffer_.rows();
  display_half_width_ = 0.0;
  display_is_ground_ = false;
  display_metric_ = false;
  depth_missing_ = false;

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
  // Uniform scaling: fit every visible row to one half-width (the widest in the
  // viewport) so a sonar range change doesn't rescale the picture. Otherwise
  // each row fits its own range.
  double uniform_half = 0.0;
  for (std::size_t y = 0; y < height; ++y) {
    uniform_half = std::max(uniform_half, row_geom(rows[first_row + y], ground_range_).half_width);
  }

  // Store oldest-first (front -> row 0). Screen-top is texture v=1 = the last
  // row = newest, so no V flip is needed at draw time. Each row is projected
  // onto a centred display axis (nadir at the middle column) with the water
  // column removed and slant->ground applied per row_geom.
  for (std::size_t y = 0; y < height; ++y) {
    const WaterfallRow & row = rows[first_row + y];
    const RowGeom g = row_geom(row, ground_range_);
    const double half = uniform_scale_ ? uniform_half : g.half_width;
    // Swap in the precomputed TVG samples when enabled; else the raw samples.
    const std::vector<float> & src =
      (tvg_ && row.has_tvg) ? row.intensities_tvg : row.intensities;
    // Project straight into this row of the staging buffer (no per-row alloc).
    project_row_into(
      data.data() + y * width, src, row.nadir_index, g.range_port, g.range_stbd,
      g.altitude, g.ground, half, width);
  }

  // Overlay state from the newest row (top of the display).
  const WaterfallRow & newest = rows.back();
  const RowGeom ng = row_geom(newest, ground_range_);
  display_half_width_ = uniform_scale_ ? uniform_half : ng.half_width;
  display_is_ground_ = ng.ground;
  display_metric_ = ng.metric;
  depth_missing_ = ground_range_ && newest.altitude <= 0.0;

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
    return;
  }

  const double w = width();
  const double cx = w / 2.0;

  if (range_lines_ && display_half_width_ > 0.0) {
    // Across-track range grid, centred on nadir. Spacing auto-fits the window
    // width and the density control, snapped to round values.
    const double px_per_unit = (w / 2.0) / display_half_width_;
    const double target_px = 90.0 / std::max(0.25f, range_line_density_);
    const double lines_per_side = std::max(1.0, (w / 2.0) / target_px);
    const double interval = nice_interval(display_half_width_ / lines_per_side);
    const QString unit = display_metric_ ? tr(" m") : QString();
    const int decimals = (interval > 0.0 && interval < 1.0) ? 1 : 0;

    if (interval > 0.0) {
      painter.setPen(QColor(180, 180, 190, 120));
      for (double r = interval; r <= display_half_width_ + 1e-6; r += interval) {
        const int xl = static_cast<int>(std::lround(cx - r * px_per_unit));
        const int xr = static_cast<int>(std::lround(cx + r * px_per_unit));
        painter.drawLine(xl, 0, xl, height());
        painter.drawLine(xr, 0, xr, height());
      }
    }
    // Nadir centre line, brighter.
    painter.setPen(QColor(120, 200, 255, 160));
    const int xc = static_cast<int>(std::lround(cx));
    painter.drawLine(xc, 0, xc, height());

    if (interval > 0.0) {
      painter.setPen(QColor(225, 225, 230));
      for (double r = interval; r <= display_half_width_ + 1e-6; r += interval) {
        const QString lbl = tr("%1").arg(r, 0, 'f', decimals) + unit;
        const int xl = static_cast<int>(std::lround(cx - r * px_per_unit));
        const int xr = static_cast<int>(std::lround(cx + r * px_per_unit));
        painter.drawText(QRect(xl - 30, 2, 60, 16), Qt::AlignCenter, lbl);
        painter.drawText(QRect(xr - 30, 2, 60, 16), Qt::AlignCenter, lbl);
      }
    }
  } else if (range_max_ > 0.0) {
    // Range lines off: keep the simple max-range edge labels.
    painter.setPen(QColor(220, 220, 220));
    const QString label = tr("%1 m").arg(range_max_, 0, 'f', 1);
    painter.drawText(rect().adjusted(4, 2, 0, 0), Qt::AlignTop | Qt::AlignLeft, label);
    painter.drawText(rect().adjusted(0, 2, -4, 0), Qt::AlignTop | Qt::AlignRight, label);
  }

  // Axis-mode / depth indicator along the bottom-left.
  QString mode = display_is_ground_ ? tr("ground range") :
    (display_metric_ ? tr("slant range") : tr("samples"));
  if (depth_missing_) {
    mode += tr("  (no depth)");
    painter.setPen(QColor(240, 200, 90));
  } else {
    painter.setPen(QColor(150, 150, 160));
  }
  painter.drawText(rect().adjusted(4, 0, -4, -3), Qt::AlignBottom | Qt::AlignLeft, mode);
}

}  // namespace rqt_sonar_waterfall
