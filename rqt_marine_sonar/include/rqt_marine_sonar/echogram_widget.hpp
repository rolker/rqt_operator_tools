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

#ifndef RQT_MARINE_SONAR__ECHOGRAM_WIDGET_HPP_
#define RQT_MARINE_SONAR__ECHOGRAM_WIDGET_HPP_

#include <QChartView>
#include <QGraphicsPixmapItem>
#include <QImage>
#include <QValueAxis>

#include <cstdint>
#include <map>
#include <vector>

#include <marine_acoustic_msgs/msg/raw_sonar_image.hpp>
#include <rqt_sonar_waterfall/color_map.hpp>

namespace rqt_marine_sonar
{

/// Scrolling water-column echogram. Each incoming RawSonarImage ping becomes a
/// vertical column of the image; depth runs down the left QValueAxis. Pings of
/// any sample dtype (decoded once on arrival via the shared
/// rqt_sonar_waterfall decoder) are depth-binned to a shared bin size and
/// mapped through the marine_colormap pipeline (normalize against the
/// [min, max] value window -> gain -> contrast -> palette). The view supports
/// mouse-wheel depth zoom (Ctrl for fine zoom, focused on the cursor) and
/// left-drag depth panning.
class EchogramWidget : public QtCharts::QChartView
{
  Q_OBJECT

public:
  explicit EchogramWidget(QWidget * parent);

  float minimumValue() const;
  float maximumValue() const;
  float gain() const;
  float contrast() const;
  int colorMapIndex() const;
  float pingSpacing() const;

signals:
  void mouseMoved(QPointF position);

public slots:
  void addPing(const marine_acoustic_msgs::msg::RawSonarImage & ping);
  void setMinimumValue(float value);
  void setMaximumValue(float value);
  void setGain(float gain);
  void setContrast(float contrast);
  void setColorMapIndex(int index);

  /// Set the horizontal spacing between pings for display.
  void setPingSpacing(float spacing);

protected:
  void resizeEvent(QResizeEvent * event) override;
  void wheelEvent(QWheelEvent * event) override;
  void mouseMoveEvent(QMouseEvent * event) override;
  void mousePressEvent(QMouseEvent * event) override;
  void mouseReleaseEvent(QMouseEvent * event) override;

protected slots:
  void updateEchogram();
  void adjustPixmap();
  void adjustAxis();

private:
  /// One ping, decoded once on arrival: geometry + float samples (any dtype).
  struct DecodedPing
  {
    float min_depth;
    float max_depth;
    float bin_size;
    std::vector<float> samples;
  };

  std::map<int64_t, DecodedPing> pings_;
  int maximum_ping_count_ = 2048;

  // Display window: raw sample values mapped to [0, 1] before gain/contrast.
  // Degenerate (max <= min) means "not yet configured" -- the plugin seeds a
  // dtype-aware default on the first ping.
  float value_min_ = 0.0;
  float value_max_ = 0.0;
  float gain_ = 1.0;
  float contrast_ = 1.0;
  rqt_sonar_waterfall::ColorMap color_map_;
  float ping_spacing_ = 1.0;

  float min_depth_ = 0.0;
  float max_depth_ = 0.0;
  float bin_size_ = 0.0;

  float depth_zoom_ = 1.0;
  float depth_offset_ = 0.0;

  bool translating_depth_ = false;
  float depth_translation_start_ = 0.0;
  float depth_offset_start_ = 0.0;

  QGraphicsPixmapItem * pixmap_item_ = nullptr;
  QtCharts::QValueAxis * depth_axis_ = nullptr;

  QImage echogram_;
};

}  // namespace rqt_marine_sonar

#endif  // RQT_MARINE_SONAR__ECHOGRAM_WIDGET_HPP_
