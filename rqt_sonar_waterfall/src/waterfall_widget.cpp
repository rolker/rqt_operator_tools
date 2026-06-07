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

#include <algorithm>
#include <cmath>
#include <utility>

namespace rqt_sonar_waterfall
{

WaterfallWidget::WaterfallWidget(QWidget * parent)
: QWidget(parent)
{
  setMinimumSize(256, 256);
}

WaterfallWidget::~WaterfallWidget() = default;

void WaterfallWidget::add_row(const WaterfallRow & row)
{
  if (frozen_) {
    return;
  }
  buffer_.push(row);
  rebuild_image();
}

void WaterfallWidget::clear()
{
  buffer_.clear();
  rebuild_image();
}

void WaterfallWidget::set_color_map(ColorMapType type)
{
  color_map_.set_type(type);
  rebuild_image();
}

void WaterfallWidget::set_gain(float gain)
{
  gain_ = gain;
  rebuild_image();
}

void WaterfallWidget::set_contrast(float contrast)
{
  contrast_ = contrast;
  rebuild_image();
}

void WaterfallWidget::set_history(std::size_t rows)
{
  buffer_.set_capacity(rows);
  rebuild_image();
}

void WaterfallWidget::set_frozen(bool frozen)
{
  frozen_ = frozen;
}

void WaterfallWidget::set_auto_range(bool enabled)
{
  auto_range_ = enabled;
  rebuild_image();
}

void WaterfallWidget::set_manual_range(float min, float max)
{
  // Selecting a manual range implies leaving auto-range, otherwise
  // rebuild_image() would keep overriding min/max from the data.
  auto_range_ = false;
  manual_min_ = min;
  manual_max_ = max;
  rebuild_image();
}

void WaterfallWidget::rebuild_image()
{
  const auto & rows = buffer_.rows();

  std::size_t width = 0;
  for (const auto & row : rows) {
    width = std::max(width, row.intensities.size());
  }

  if (rows.empty() || width == 0) {
    image_ = QImage();
    range_max_ = 0.0;
    update();
    return;
  }

  const std::size_t height = rows.size();
  QImage img(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGB888);

  float min = manual_min_;
  float max = manual_max_;
  if (auto_range_) {
    std::tie(min, max) = auto_range(rows);
  }

  for (std::size_t y = 0; y < height; ++y) {
    // Newest row at the top; older rows scroll downward.
    const WaterfallRow & row = rows[height - 1 - y];
    const std::size_t row_width = row.intensities.size();
    uchar * line = img.scanLine(static_cast<int>(y));
    for (std::size_t x = 0; x < width; ++x) {
      Rgb c{0, 0, 0};
      if (row_width > 0) {
        std::size_t sample = 0;
        if (row_width > 1 && width > 1) {
          sample = static_cast<std::size_t>(
            std::lround(static_cast<double>(x) * (row_width - 1) / (width - 1)));
        }
        const float t =
          scale_intensity(row.intensities[sample], min, max, gain_, contrast_);
        c = color_map_.lookup(t);
      }
      uchar * px = line + x * 3;
      px[0] = c.r;
      px[1] = c.g;
      px[2] = c.b;
    }
  }

  image_ = std::move(img);
  range_max_ = rows.back().range_max;  // newest row
  update();
}

void WaterfallWidget::paintEvent(QPaintEvent * event)
{
  Q_UNUSED(event);
  QPainter painter(this);

  if (image_.isNull()) {
    painter.fillRect(rect(), QColor(20, 20, 24));
    painter.setPen(QColor(120, 120, 130));
    painter.drawText(rect(), Qt::AlignCenter, tr("No sonar data"));
    return;
  }

  // Stretch the waterfall to fill the viewport. Smooth scaling would blur the
  // along-track structure, so use the default (nearest) transform.
  painter.drawImage(rect(), image_);

  if (range_max_ > 0.0) {
    painter.setPen(QColor(220, 220, 220));
    const QString label = tr("%1 m").arg(range_max_, 0, 'f', 1);
    // Outer edges are max slant range; the center column is nadir.
    painter.drawText(rect().adjusted(4, 2, 0, 0), Qt::AlignTop | Qt::AlignLeft, label);
    painter.drawText(rect().adjusted(0, 2, -4, 0), Qt::AlignTop | Qt::AlignRight, label);
  }
}

}  // namespace rqt_sonar_waterfall
