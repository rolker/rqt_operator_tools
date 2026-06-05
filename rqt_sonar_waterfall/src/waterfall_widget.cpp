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

namespace rqt_sonar_waterfall
{

WaterfallWidget::WaterfallWidget(QWidget * parent)
: QWidget(parent)
{
  setMinimumSize(256, 256);
}

WaterfallWidget::~WaterfallWidget() = default;

void WaterfallWidget::paintEvent(QPaintEvent * /*event*/)
{
  QPainter painter(this);

  if (canvas_.isNull()) {
    // No data yet: dark placeholder canvas with a centered hint.
    painter.fillRect(rect(), QColor(20, 20, 24));
    painter.setPen(QColor(120, 120, 130));
    painter.drawText(rect(), Qt::AlignCenter, tr("No sonar data"));
    return;
  }

  // Stretch the waterfall image to fill the viewport (per-axis scaling and a
  // range ruler are added with the data path in a later step).
  painter.drawImage(rect(), canvas_);
}

}  // namespace rqt_sonar_waterfall
