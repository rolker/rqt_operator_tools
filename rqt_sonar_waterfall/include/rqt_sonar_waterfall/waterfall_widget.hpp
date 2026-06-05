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

#ifndef RQT_SONAR_WATERFALL__WATERFALL_WIDGET_HPP_
#define RQT_SONAR_WATERFALL__WATERFALL_WIDGET_HPP_

#include <QImage>
#include <QWidget>

#include <cstddef>

#include "rqt_sonar_waterfall/color_map.hpp"
#include "rqt_sonar_waterfall/waterfall_buffer.hpp"
#include "rqt_sonar_waterfall/waterfall_model.hpp"

namespace rqt_sonar_waterfall
{

/// Scrolling backscatter waterfall canvas.
///
/// Geometry-agnostic: it consumes already-assembled WaterfallRows (the newest is
/// drawn at the top and older rows scroll downward) and renders them with a
/// client-side intensity scaling (gain/contrast) and a selectable color map.
class WaterfallWidget : public QWidget
{
  Q_OBJECT

public:
  explicit WaterfallWidget(QWidget * parent = nullptr);
  ~WaterfallWidget() override;

  /// Append the newest row. No-op while frozen.
  void add_row(const WaterfallRow & row);

  /// Drop all buffered rows.
  void clear();

  // --- client-side view controls ---
  void set_color_map(ColorMapType type);
  void set_gain(float gain);
  void set_contrast(float contrast);
  /// Scrollback depth (rows); clamped to >= 1.
  void set_history(std::size_t rows);
  /// When frozen, add_row() is ignored so the current view holds still.
  void set_frozen(bool frozen);
  /// Auto-scale intensity to the buffered min/max (the default).
  void set_auto_range(bool enabled);
  /// Fixed intensity range used when auto-range is disabled.
  void set_manual_range(float min, float max);

  bool frozen() const {return frozen_;}
  std::size_t history() const {return buffer_.capacity();}

protected:
  void paintEvent(QPaintEvent * event) override;

private:
  void rebuild_image();

  WaterfallBuffer buffer_;
  ColorMap color_map_;
  QImage image_;          ///< cached render of the whole buffer
  double range_max_ = 0.0;  ///< slant range of the newest row, meters (0 = unknown)

  float gain_ = 1.0f;
  float contrast_ = 1.0f;
  bool frozen_ = false;
  bool auto_range_ = true;
  float manual_min_ = 0.0f;
  float manual_max_ = 1.0f;
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__WATERFALL_WIDGET_HPP_
