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

#include "rqt_marine_sonar/echogram_widget.hpp"

#include <QColor>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <builtin_interfaces/msg/time.hpp>

#include "rqt_marine_sonar/ping.hpp"

namespace rqt_marine_sonar
{

namespace
{

/// Upper bound on the echogram's depth-sample (row) count, bounding the texture
/// height so a degenerate ping (near-zero bin) can't request a huge allocation.
constexpr int kMaxDepthSamples = 1 << 14;

int64_t stampToNanoseconds(const builtin_interfaces::msg::Time & stamp)
{
  return static_cast<int64_t>(stamp.sec) * 1000000000LL +
         static_cast<int64_t>(stamp.nanosec);
}

/// Snap a raw spacing to the nearest "nice" 1/2/5 x 10^n value for round labels.
double niceInterval(double raw)
{
  if (!(raw > 0.0)) {
    return 0.0;
  }
  const double e = std::floor(std::log10(raw));
  const double base = std::pow(10.0, e);
  const double f = raw / base;
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

EchogramWidget::EchogramWidget(QWidget * parent)
: QOpenGLWidget(parent)
{
  // 3.3 compatibility profile so the QPainter axis overlay (legacy GL paint
  // engine) coexists with GpuColorMap's modern shader path — same arrangement
  // as WaterfallWidget.
  QSurfaceFormat fmt = format();
  fmt.setVersion(3, 3);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  setFormat(fmt);
  setMinimumSize(256, 128);
  setMouseTracking(true);
}

EchogramWidget::~EchogramWidget()
{
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

void EchogramWidget::addPing(const marine_acoustic_msgs::msg::RawSonarImage & ping)
{
  if (ingestPing(ping)) {
    data_dirty_ = true;
    update();
  }
}

void EchogramWidget::addPings(
  const std::vector<marine_acoustic_msgs::msg::RawSonarImage> & pings)
{
  bool any = false;
  for (const auto & ping : pings) {
    any = ingestPing(ping) || any;
  }
  if (any) {
    data_dirty_ = true;
    update();
  }
}

bool EchogramWidget::ingestPing(const marine_acoustic_msgs::msg::RawSonarImage & ping)
{
  Ping view(ping);
  DecodedPing decoded;
  decoded.min_depth = view.minimumDepth();
  decoded.max_depth = view.maximumDepth();
  decoded.bin_size = view.binSize();
  decoded.samples = view.samples();
  if (decoded.samples.empty()) {
    qWarning(
      "EchogramWidget: dropping ping with unsupported dtype %u or empty image",
      static_cast<unsigned>(ping.image.dtype));
    return false;
  }
  // Reject non-finite / degenerate geometry up front so the render path only
  // indexes pings it can render. Positive-form checks: a NaN fails them all.
  if (!std::isfinite(decoded.min_depth) || !std::isfinite(decoded.max_depth) ||
    !(decoded.bin_size > 0.0f) || !(decoded.max_depth > decoded.min_depth))
  {
    qWarning(
      "EchogramWidget: dropping ping with invalid geometry "
      "(min_depth=%g max_depth=%g bin_size=%g; check sample_rate/sound_speed)",
      decoded.min_depth, decoded.max_depth, decoded.bin_size);
    return false;
  }
  pings_[stampToNanoseconds(ping.header.stamp)] = std::move(decoded);
  while (static_cast<int>(pings_.size()) > maximum_ping_count_) {
    pings_.erase(pings_.begin()->first);
  }
  return true;
}

bool EchogramWidget::recomputeGeometry()
{
  if (pings_.empty()) {
    return false;
  }
  float min_depth = pings_.begin()->second.min_depth;
  float max_depth = pings_.begin()->second.max_depth;
  float bin_size = pings_.begin()->second.bin_size;
  for (const auto & p : pings_) {
    min_depth = std::min(min_depth, p.second.min_depth);
    max_depth = std::max(max_depth, p.second.max_depth);
    bin_size = std::min(bin_size, p.second.bin_size);
  }
  if (!(bin_size > 0.0f) || !std::isfinite(bin_size) || !std::isfinite(min_depth) ||
    !std::isfinite(max_depth) || !(max_depth > min_depth))
  {
    return false;
  }
  min_depth_ = min_depth;
  max_depth_ = max_depth;
  bin_size_ = bin_size;
  return true;
}

std::pair<float, float> EchogramWidget::visibleDepthWindow() const
{
  if (!(max_depth_ > min_depth_)) {
    return {0.0f, 0.0f};
  }
  const float full = max_depth_ - min_depth_;
  // zoom >= 1: the full extent is the most zoomed-out view (showing more than
  // the data has no meaning), so there is no dead sub-1.0 band.
  const float zoom = std::max(depth_zoom_, 1.0f);
  float range = full / zoom;
  range = std::min(range, full);
  float vis_min = min_depth_ + depth_offset_;
  // Keep the window within the data extent.
  vis_min = std::clamp(vis_min, min_depth_, max_depth_ - range);
  return {vis_min, vis_min + range};
}

void EchogramWidget::initializeGL()
{
  initializeOpenGLFunctions();
  if (!gpu_.initialize()) {
    gl_ready_ = false;
    return;
  }
  gpu_.set_palette(color_map_type_);
  palette_dirty_ = false;
  data_dirty_ = true;
  gl_ready_ = true;
}

void EchogramWidget::resizeGL(int w, int h)
{
  // w/h arrive in device pixels (already scaled by devicePixelRatio), which is
  // what glViewport expects — use them directly. (Mixing in logical height()
  // here would shrink the GL frame on HiDPI displays.)
  glViewport(0, 0, w, h);
  data_dirty_ = true;  // visible-ping count depends on width
}

void EchogramWidget::uploadTexture()
{
  if (!recomputeGeometry()) {
    has_data_ = false;
    return;
  }
  const auto window = visibleDepthWindow();
  vis_min_depth_ = window.first;
  vis_max_depth_ = window.second;
  if (!(vis_max_depth_ > vis_min_depth_)) {
    has_data_ = false;
    return;
  }

  GLint max_tex = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
  const int max_dim = (max_tex > 0) ? max_tex : 2048;

  int depth_rows = static_cast<int>(
    std::min(
      std::ceil(static_cast<double>(vis_max_depth_ - vis_min_depth_) / bin_size_),
      static_cast<double>(std::min(kMaxDepthSamples, max_dim))));
  if (depth_rows <= 0) {
    has_data_ = false;
    return;
  }
  // The texture covers exactly depth_rows bins; snap the cached window max to
  // that grid so the depth-axis overlay (which maps span across the same
  // viewport) lines up with the rendered rows rather than being off by up to a
  // bin from the ceil().
  vis_max_depth_ = vis_min_depth_ + depth_rows * bin_size_;

  // Visible ping columns: honor ping_spacing_ (wider spacing -> fewer, wider
  // columns) and the canvas width; newest pings on the right.
  const int ping_count = static_cast<int>(pings_.size());
  const float spacing = std::max(ping_spacing_, 1.0f);
  int visible_pings = std::max(1, static_cast<int>(std::ceil(width() / spacing)));
  visible_pings = std::min(std::min(visible_pings, ping_count), max_dim);
  const int startx = ping_count - visible_pings;

  std::vector<float> data;
  try {
    data.assign(
      static_cast<std::size_t>(visible_pings) * static_cast<std::size_t>(depth_rows),
      std::numeric_limits<float>::quiet_NaN());
  } catch (const std::exception &) {
    has_data_ = false;
    return;
  }

  // Walk the buffered pings (ordered oldest->newest); pack the newest
  // `visible_pings` into columns [0, visible_pings) left->right.
  int idx = 0;
  int col = 0;
  for (auto it = pings_.begin(); it != pings_.end() && col < visible_pings; ++it, ++idx) {
    if (idx < startx) {
      continue;
    }
    const DecodedPing & ping = it->second;
    if (!(ping.bin_size > 0.0f)) {
      ++col;
      continue;
    }
    for (int r = 0; r < depth_rows; ++r) {
      const float depth = vis_min_depth_ + r * bin_size_;
      if (!(depth >= ping.min_depth && depth < ping.max_depth)) {
        continue;  // outside this ping's water column -> leave NaN (palette floor)
      }
      const double si = (depth - ping.min_depth) / ping.bin_size;
      if (!(si >= 0.0 && si < static_cast<double>(ping.samples.size()))) {
        continue;
      }
      // data row r (r=0 = shallowest) maps to texture v=0; drawn with flip_v so
      // screen-top shows the shallowest sample.
      data[static_cast<std::size_t>(r) * visible_pings + col] =
        ping.samples[static_cast<std::size_t>(si)];
    }
    ++col;
  }

  if (intensity_tex_ == 0) {
    glGenTextures(1, &intensity_tex_);
  }
  glBindTexture(GL_TEXTURE_2D, intensity_tex_);
  glGetError();
  glTexImage2D(
    GL_TEXTURE_2D, 0, GL_R32F, visible_pings, depth_rows, 0, GL_RED, GL_FLOAT,
    data.data());
  const GLenum err = glGetError();
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  if (err != GL_NO_ERROR) {
    static bool warned = false;
    if (!warned) {
      qWarning("EchogramWidget: intensity texture upload failed (GL 0x%x).", err);
      warned = true;
    }
    has_data_ = false;
    return;
  }
  has_data_ = true;
}

void EchogramWidget::paintGL()
{
  glClearColor(20.0f / 255.0f, 20.0f / 255.0f, 24.0f / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  const bool window_set = value_max_ > value_min_;
  if (gl_ready_) {
    if (palette_dirty_) {
      gpu_.set_palette(color_map_type_);
      palette_dirty_ = false;
    }
    if (data_dirty_) {
      uploadTexture();
      data_dirty_ = false;
    }
    if (has_data_ && window_set) {
      gpu_.set_range(value_min_, value_max_);
      gpu_.set_gain(gain_);
      gpu_.set_contrast(contrast_);
      gpu_.draw(intensity_tex_, /*flip_v=*/true);
    }
  }

  QPainter painter(this);
  if (!has_data_ || !window_set) {
    painter.setPen(QColor(120, 120, 130));
    painter.drawText(rect(), Qt::AlignCenter, tr("No sonar data"));
    return;
  }

  // Depth-axis overlay along the left edge: nice gridlines + meter labels.
  const double h = height();
  const double span = vis_max_depth_ - vis_min_depth_;
  if (span > 0.0 && h > 0.0) {
    const double mpp = span / h;  // meters per pixel
    const double interval = niceInterval(100.0 * mpp);
    if (interval > 0.0) {
      const double first = std::ceil(vis_min_depth_ / interval) * interval;
      painter.setPen(QColor(180, 180, 190, 110));
      for (double d = first; d <= vis_max_depth_ + 1e-6; d += interval) {
        const int y = static_cast<int>(std::lround((d - vis_min_depth_) / mpp));
        painter.drawLine(0, y, width(), y);
      }
      painter.setPen(QColor(225, 225, 230));
      const int decimals = (interval < 1.0) ? 1 : 0;
      for (double d = first; d <= vis_max_depth_ + 1e-6; d += interval) {
        const int y = static_cast<int>(std::lround((d - vis_min_depth_) / mpp));
        painter.drawText(
          QRect(2, y - 16, 80, 14), Qt::AlignLeft | Qt::AlignBottom,
          tr("%1 m").arg(d, 0, 'f', decimals));
      }
    }
  }
}

void EchogramWidget::wheelEvent(QWheelEvent * event)
{
  const double h = height();
  if (h <= 0.0 || !(max_depth_ > min_depth_)) {
    return;
  }
  const double angle_degrees = event->angleDelta().y() / 8.0;
  double zoom_per_degree = 0.01;
  if (QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
    zoom_per_degree /= 3.0;
  }
  const double scale = std::pow(2.0, zoom_per_degree * angle_degrees);

  // Depth under the cursor before the zoom, kept fixed across it.
  const double frac = std::clamp(event->position().y() / h, 0.0, 1.0);
  const double cursor_depth = vis_min_depth_ + frac * (vis_max_depth_ - vis_min_depth_);

  depth_zoom_ = std::clamp(static_cast<float>(depth_zoom_ * scale), 1.0f, 5000.0f);

  const float full = max_depth_ - min_depth_;
  const float new_range = std::min(full / std::max(depth_zoom_, 1.0f), full);
  const double new_vis_min = cursor_depth - frac * new_range;
  depth_offset_ = static_cast<float>(new_vis_min - min_depth_);

  data_dirty_ = true;
  update();
}

void EchogramWidget::mousePressEvent(QMouseEvent * event)
{
  if (event->button() == Qt::LeftButton) {
    depth_offset_start_ = depth_offset_;
    depth_translation_start_ = static_cast<float>(event->localPos().y());
    translating_depth_ = true;
  }
  QOpenGLWidget::mousePressEvent(event);
}

void EchogramWidget::mouseMoveEvent(QMouseEvent * event)
{
  const double h = height();
  if (translating_depth_ && h > 0.0) {
    const double dy = event->localPos().y() - depth_translation_start_;
    const double range = vis_max_depth_ - vis_min_depth_;
    // Drag down -> reveal shallower water (window moves up).
    depth_offset_ = static_cast<float>(depth_offset_start_ - range * dy / h);
    data_dirty_ = true;
    update();
  }
  emit mouseMoved(event->localPos());
  QOpenGLWidget::mouseMoveEvent(event);
}

void EchogramWidget::mouseReleaseEvent(QMouseEvent * event)
{
  if (event->button() == Qt::LeftButton) {
    translating_depth_ = false;
  }
  QOpenGLWidget::mouseReleaseEvent(event);
}

QImage EchogramWidget::echogramImage()
{
  return grabFramebuffer();
}

void EchogramWidget::setMinimumValue(float value)
{
  if (value_min_ != value) {
    value_min_ = value;
    update();
  }
}

void EchogramWidget::setMaximumValue(float value)
{
  if (value_max_ != value) {
    value_max_ = value;
    update();
  }
}

void EchogramWidget::setGain(float gain)
{
  if (gain_ != gain) {
    gain_ = gain;
    update();
  }
}

void EchogramWidget::setContrast(float contrast)
{
  if (contrast_ != contrast) {
    contrast_ = contrast;
    update();
  }
}

void EchogramWidget::setColorMapIndex(int index)
{
  const auto type = rqt_sonar_waterfall::color_map_from_index(index);
  if (color_map_type_ != type) {
    color_map_type_ = type;
    palette_dirty_ = true;
    update();
  }
}

void EchogramWidget::setPingSpacing(float spacing)
{
  if (ping_spacing_ != spacing) {
    ping_spacing_ = spacing;
    data_dirty_ = true;
    update();
  }
}

float EchogramWidget::minimumValue() const {return value_min_;}
float EchogramWidget::maximumValue() const {return value_max_;}
float EchogramWidget::gain() const {return gain_;}
float EchogramWidget::contrast() const {return contrast_;}
int EchogramWidget::colorMapIndex() const
{
  return rqt_sonar_waterfall::color_map_index(color_map_type_);
}
float EchogramWidget::pingSpacing() const {return ping_spacing_;}

}  // namespace rqt_marine_sonar
