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

#include "rqt_camera_grid/thumbnail_cell.hpp"

#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <utility>

#include "rqt_camera_grid/camera_pane_widget.hpp"

namespace rqt_camera_grid
{

namespace
{
// Generous thumbnail floor: 16:9 scaled down to a size where base topic
// names in the overlay label stay readable without wrapping.
constexpr int kMinThumbWidth = 160;
constexpr int kMinThumbHeight = 90;
// Selection outline — high contrast against the pane's dark background.
const QColor kSelectedBorder(80, 160, 255);
constexpr int kSelectedBorderPx = 3;
constexpr int kUnselectedBorderPx = 1;
}  // namespace

ThumbnailCell::ThumbnailCell(
  rclcpp::Node::SharedPtr node,
  std::shared_ptr<image_transport::ImageTransport> it,
  const PaneConfig & config,
  int index,
  ClickHandler on_click,
  QWidget * parent)
: QWidget(parent),
  pane_(nullptr),
  index_(index),
  on_click_(std::move(on_click))
{
  // Inset layout so the cell's own border is visible around the pane.
  const int pad = kSelectedBorderPx + 2;
  auto * layout = new QVBoxLayout(this);
  layout->setContentsMargins(pad, pad, pad, pad);
  layout->setSpacing(0);
  pane_ = new CameraPaneWidget(std::move(node), std::move(it), config, this);
  layout->addWidget(pane_);
  setMinimumSize(
    kMinThumbWidth + 2 * pad,
    kMinThumbHeight + 2 * pad);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ThumbnailCell::set_selected(bool selected)
{
  if (selected == selected_) {return;}
  selected_ = selected;
  update();
}

void ThumbnailCell::mousePressEvent(QMouseEvent * event)
{
  if (event->button() == Qt::LeftButton && on_click_) {
    on_click_(index_);
  }
  QWidget::mousePressEvent(event);
}

void ThumbnailCell::paintEvent(QPaintEvent * event)
{
  QWidget::paintEvent(event);
  QPainter painter(this);
  QColor color;
  int width_px;
  if (selected_) {
    color = kSelectedBorder;
    width_px = kSelectedBorderPx;
  } else {
    // Neutral frame so the cell boundary is visible but unobtrusive.
    color = QColor(60, 60, 60);
    width_px = kUnselectedBorderPx;
  }
  painter.setPen(QPen(color, width_px));
  painter.setBrush(Qt::NoBrush);
  const int half = width_px / 2;
  painter.drawRect(
    rect().adjusted(half, half, -half - (width_px % 2), -half - (width_px % 2)));
}

}  // namespace rqt_camera_grid
