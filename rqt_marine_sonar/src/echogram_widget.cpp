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

#include <QChart>
#include <QGuiApplication>
#include <QWheelEvent>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <builtin_interfaces/msg/time.hpp>

#include "rqt_marine_sonar/ping.hpp"

namespace rqt_marine_sonar
{

namespace
{

/// Upper bound on the echogram's depth-sample (row) count. A degenerate ping
/// (near-zero sample_rate, hence a tiny bin size) could otherwise produce a
/// pathologically tall QImage that fails to allocate.
constexpr int kMaxDepthSamples = 1 << 16;

/// Map a message stamp to a strictly-ordered nanosecond key for the ping buffer.
/// (The ROS 1 plugin keyed an std::map on ros::Time; this is the ROS 2 analogue
/// without pulling in rclcpp::Time clock-source semantics.)
int64_t stampToNanoseconds(const builtin_interfaces::msg::Time & stamp)
{
  return static_cast<int64_t>(stamp.sec) * 1000000000LL +
         static_cast<int64_t>(stamp.nanosec);
}

}  // namespace

EchogramWidget::EchogramWidget(QWidget * parent)
: QtCharts::QChartView(parent)
{
  echogram_ = QImage(maximum_ping_count_, maximum_ping_count_, QImage::Format_RGB32);
  echogram_.fill(Qt::lightGray);
  pixmap_item_ = scene()->addPixmap(QPixmap::fromImage(echogram_));
  pixmap_item_->setTransformationMode(Qt::SmoothTransformation);

  setChart(new QtCharts::QChart());

  pixmap_item_->setParentItem(chart());

  depth_axis_ = new QtCharts::QValueAxis();
  depth_axis_->setTickType(QtCharts::QValueAxis::TicksDynamic);
  depth_axis_->setTickInterval(20.0);
  depth_axis_->setTickAnchor(0.0);
  chart()->addAxis(depth_axis_, Qt::AlignLeft);

  setRenderHints(QPainter::Antialiasing);
}

void EchogramWidget::addPing(const marine_acoustic_msgs::msg::RawSonarImage & ping)
{
  if (ingestPing(ping)) {
    updateEchogram();
  }
}

void EchogramWidget::addPings(
  const std::vector<marine_acoustic_msgs::msg::RawSonarImage> & pings)
{
  bool any_accepted = false;
  for (const auto & ping : pings) {
    any_accepted = ingestPing(ping) || any_accepted;
  }
  if (any_accepted) {
    updateEchogram();
  }
}

bool EchogramWidget::ingestPing(const marine_acoustic_msgs::msg::RawSonarImage & ping)
{
  // Decode once on arrival (any dtype, via the shared waterfall decoder);
  // redraws then index the float cache instead of re-reading raw bytes.
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
  // Reject non-finite or degenerate geometry here (e.g. sample_rate == 0
  // yields NaN/inf depths) so the buffer only ever holds pings the render
  // loop can index safely. Positive-form checks: a NaN fails them all.
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

void EchogramWidget::resizeEvent(QResizeEvent * event)
{
  QChartView::resizeEvent(event);
  adjustPixmap();
}

void EchogramWidget::wheelEvent(QWheelEvent * event)
{
  QChartView::wheelEvent(event);

  // wheel turn angles are encoded in 1/8 degree increments.
  auto angle_delta_degrees = event->angleDelta().y() / 8.0;

  double zoom_level_per_degree = 0.01;

  // fine zoom if ctrl key is pressed
  if (QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
    zoom_level_per_degree /= 3.0;
  }

  double scale_change = pow(2, zoom_level_per_degree * angle_delta_degrees);

  // figure out mouse position to focus zoom
  auto area = chart()->plotArea();
  if (area.height() <= 0.0) {
    return;  // not laid out yet / collapsed — nothing to zoom against
  }

  auto pixel_focus = event->position().y() - area.top();

  auto depth_range = depth_axis_->max() - depth_axis_->min();
  auto meters_per_pixel = depth_range / area.height();

  auto depth_focus = depth_axis_->max() - pixel_focus * meters_per_pixel;

  // apply zoom
  depth_zoom_ *= scale_change;

  // clamp zoom in
  depth_zoom_ = std::max(depth_zoom_, 0.5f);

  // adjust offset

  auto new_depth_range = (max_depth_ - min_depth_) / depth_zoom_;
  auto new_meters_per_pixel = new_depth_range / area.height();
  auto new_max = depth_focus + (pixel_focus * new_meters_per_pixel);
  depth_offset_ = -new_max - min_depth_;

  adjustAxis();
}

void EchogramWidget::mouseMoveEvent(QMouseEvent * event)
{
  QChartView::mouseMoveEvent(event);
  if (translating_depth_) {
    auto area = chart()->plotArea();
    if (area.height() <= 0.0) {
      return;
    }
    float dy = event->localPos().y() - depth_translation_start_;
    auto axis_range = depth_axis_->max() - depth_axis_->min();
    auto delta_depth = axis_range * dy / area.height();
    depth_offset_ = depth_offset_start_ - delta_depth;
    adjustAxis();
  }
}

void EchogramWidget::mousePressEvent(QMouseEvent * event)
{
  QChartView::mousePressEvent(event);
  if (event->button() == Qt::LeftButton) {
    depth_offset_start_ = depth_offset_;
    depth_translation_start_ = event->localPos().y();
    translating_depth_ = true;
  }
}

void EchogramWidget::mouseReleaseEvent(QMouseEvent * event)
{
  QChartView::mouseReleaseEvent(event);
  if (event->button() == Qt::LeftButton) {
    translating_depth_ = false;
  }
}

void EchogramWidget::adjustAxis()
{
  auto range = (max_depth_ - min_depth_) / depth_zoom_;
  auto max = -(min_depth_ + depth_offset_);
  auto min = max - range;
  depth_axis_->setRange(min, max);

  auto area = chart()->plotArea();
  if (area.height() <= 0.0) {
    return;  // not laid out yet — tick interval would divide by zero
  }
  auto meters_per_pixel = range / area.height();

  // 100 pixel tick interval
  auto min_tick_interval = 100 * meters_per_pixel;
  double factor = 0.1;
  std::vector<double> tick_bases = {1.0, 2.0, 5.0};
  bool done = false;
  while (!done) {
    for (auto tick_base : tick_bases) {
      if (tick_base * factor >= min_tick_interval) {
        depth_axis_->setTickInterval(tick_base * factor);
        done = true;
        break;
      }
    }
    factor *= 10.0;
  }

  adjustPixmap();
}

void EchogramWidget::adjustPixmap()
{
  auto area = chart()->plotArea();
  // ping_spacing_ divides below; a corrupted persisted value can reach here
  // (restoreSettings sets it before the spin box clamps to its 1.0 minimum).
  if (bin_size_ <= 0.0 || ping_spacing_ <= 0.0f || !std::isfinite(ping_spacing_) ||
    area.width() <= 0.0 || area.height() <= 0.0)
  {
    return;  // no valid ping geometry or no layout yet — nothing to place
  }

  // scale to match display area width with echogram width
  double area_to_echogram_scale = area.width() / static_cast<double>(echogram_.width());

  // make sure we display a ping as at least one pixel when not zoomed in
  double base_pixel_width = std::max(area_to_echogram_scale, 1.0);

  int visible_echogram_pixels = std::min(
    echogram_.width(),
    static_cast<int>(std::ceil(area.width() / (base_pixel_width * ping_spacing_))));
  int startx = echogram_.width() - visible_echogram_pixels;

  double xscale = area.width() / static_cast<double>(visible_echogram_pixels);

  auto axis_min = depth_axis_->min();
  auto axis_max = depth_axis_->max();

  auto axis_min_depth = -axis_max;
  auto axis_max_depth = -axis_min;

  auto axis_range = axis_max - axis_min;

  // meters per pixel
  auto axis_scale = axis_range / static_cast<double>(area.height());

  // Clamp in double before narrowing: after a large pan/zoom the ratio can fall
  // outside the representable int range, where the float->int cast would be UB.
  const double img_h = static_cast<double>(echogram_.height());
  const int starty =
    static_cast<int>(std::clamp((axis_min_depth - min_depth_) / bin_size_, 0.0, img_h));
  const int endy =
    static_cast<int>(std::clamp((axis_max_depth - min_depth_) / bin_size_, 0.0, img_h));

  auto pixmap = QPixmap::fromImage(echogram_).copy(
    startx, starty, visible_echogram_pixels, endy - starty);

  pixmap_item_->setPixmap(pixmap);

  // image pixels to area pixels
  float yscale = bin_size_ / axis_scale;

  pixmap_item_->setTransform(QTransform::fromScale(xscale, yscale));

  auto pixmap_min_depth = min_depth_ + starty * bin_size_;
  auto yoffset = (axis_min_depth - pixmap_min_depth) / axis_scale;
  QPointF top_left = area.topLeft();
  top_left.setY(top_left.y() - yoffset);
  pixmap_item_->setPos(top_left);
}

void EchogramWidget::updateEchogram()
{
  if (pings_.empty()) {
    echogram_ = QImage(maximum_ping_count_, maximum_ping_count_, QImage::Format_RGB32);
    echogram_.fill(Qt::lightGray);
    pixmap_item_->setPixmap(QPixmap::fromImage(echogram_));
    return;
  }

  // Compute geometry into locals and validate before committing to members:
  // a malformed ping (e.g. sample_rate == 0) yields inf/NaN here, and the
  // members feed wheelEvent()/adjustAxis() — poisoning them could hang the
  // tick-interval loop on a NaN comparison.
  float min_depth = pings_.begin()->second.min_depth;
  float max_depth = pings_.begin()->second.max_depth;
  float bin_size = pings_.begin()->second.bin_size;
  for (const auto & p : pings_) {
    min_depth = std::min(min_depth, p.second.min_depth);
    max_depth = std::max(max_depth, p.second.max_depth);
    bin_size = std::min(bin_size, p.second.bin_size);
  }

  if (bin_size > 0.0f && std::isfinite(bin_size) && std::isfinite(min_depth) &&
    std::isfinite(max_depth) && max_depth > min_depth && value_max_ > value_min_)
  {
    // ceil so the image height fully covers the half-open interval
    // [min_depth, max_depth) — a truncating cast can drop the deepest row to
    // floating-point rounding. Clamp in double before narrowing so a degenerate
    // ping (tiny finite bin) can't overflow the int cast; the cap also bounds
    // the image allocation.
    const int depth_sample_count = static_cast<int>(
      std::min(
        std::ceil(static_cast<double>(max_depth - min_depth) / bin_size),
        static_cast<double>(kMaxDepthSamples)));
    if (depth_sample_count <= 0) {
      return;
    }
    // Render into a local image and commit it together with the geometry
    // members only on success: allocation can fail (worst case
    // kMaxDepthSamples rows ~ half a GiB; QImage signals that with a null
    // image rather than throwing), and adjustPixmap() divides by the
    // committed image's width — keep the previous consistent image/geometry
    // pair rather than a null one.
    QImage fresh(maximum_ping_count_, depth_sample_count, QImage::Format_RGB32);
    if (fresh.isNull()) {
      return;
    }
    fresh.fill(Qt::black);
    min_depth_ = min_depth;
    max_depth_ = max_depth;
    bin_size_ = bin_size;

    uint32_t ping_count = pings_.size();
    uint32_t ping_number = maximum_ping_count_ - ping_count;
    for (const auto & entry : pings_) {
      const DecodedPing & ping = entry.second;
      for (int sample_number = 0; sample_number < depth_sample_count; sample_number++) {
        const float depth = min_depth_ + sample_number * bin_size_;
        // Half-open [min_depth, max_depth): at exactly max_depth the index
        // would be one past the last sample. Positive-form comparisons so a
        // NaN anywhere fails the guard instead of slipping through to the
        // index cast (ingestPing() rejects non-finite geometry, but keep the
        // loop safe on its own).
        if (!(depth >= ping.min_depth && depth < ping.max_depth) || !(ping.bin_size > 0.0f)) {
          continue;
        }
        const double index_d = (depth - ping.min_depth) / ping.bin_size;
        if (!(index_d >= 0.0 && index_d < static_cast<double>(ping.samples.size()))) {
          continue;
        }
        const float value = ping.samples[static_cast<size_t>(index_d)];
        // isfinite (not just !isnan): a FLOAT32 payload may legally contain
        // +/-inf; scale_intensity clamps, but inf*finite in the pipeline is NaN.
        if (std::isfinite(value)) {
          // Shared marine_colormap pipeline (normalize -> gain -> contrast ->
          // palette), identical math to the waterfall's CPU and GPU paths.
          const float t = rqt_sonar_waterfall::scale_intensity(
            value, value_min_, value_max_, gain_, contrast_);
          const rqt_sonar_waterfall::Rgb c = color_map_.lookup(t);
          reinterpret_cast<QRgb *>(fresh.scanLine(sample_number))[ping_number] =
            qRgb(c.r, c.g, c.b);
        }
      }
      ping_number++;
    }
    echogram_ = std::move(fresh);

    adjustAxis();
  } else {
    // Degenerate value window = the "unset" state (also how the operator
    // requests a reseed): show the placeholder instead of a stale rendering
    // so the reset visibly takes effect.
    echogram_ = QImage(maximum_ping_count_, maximum_ping_count_, QImage::Format_RGB32);
    echogram_.fill(Qt::lightGray);
    pixmap_item_->setPixmap(QPixmap::fromImage(echogram_));
  }
}

void EchogramWidget::setMinimumValue(float value)
{
  if (value_min_ != value) {
    value_min_ = value;
    updateEchogram();
  }
}

void EchogramWidget::setMaximumValue(float value)
{
  if (value_max_ != value) {
    value_max_ = value;
    updateEchogram();
  }
}

void EchogramWidget::setGain(float gain)
{
  if (gain_ != gain) {
    gain_ = gain;
    updateEchogram();
  }
}

void EchogramWidget::setContrast(float contrast)
{
  if (contrast_ != contrast) {
    contrast_ = contrast;
    updateEchogram();
  }
}

void EchogramWidget::setColorMapIndex(int index)
{
  const auto type = rqt_sonar_waterfall::color_map_from_index(index);
  if (color_map_.type() != type) {
    color_map_.set_type(type);
    updateEchogram();
  }
}

void EchogramWidget::setPingSpacing(float spacing)
{
  if (ping_spacing_ != spacing) {
    ping_spacing_ = spacing;
    updateEchogram();
  }
}

float EchogramWidget::minimumValue() const
{
  return value_min_;
}

float EchogramWidget::maximumValue() const
{
  return value_max_;
}

float EchogramWidget::gain() const
{
  return gain_;
}

float EchogramWidget::contrast() const
{
  return contrast_;
}

int EchogramWidget::colorMapIndex() const
{
  return rqt_sonar_waterfall::color_map_index(color_map_.type());
}

float EchogramWidget::pingSpacing() const
{
  return ping_spacing_;
}

}  // namespace rqt_marine_sonar
