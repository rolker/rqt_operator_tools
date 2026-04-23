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

#include "rqt_camera_grid/camera_grid_widget.hpp"

#include <QPalette>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>
#include <vector>

#include "rqt_camera_grid/camera_pane_widget.hpp"
#include "rqt_camera_grid/grid_layout.hpp"

namespace rqt_camera_grid
{

namespace
{
constexpr int kTickIntervalMs = 1000;
}

CameraGridWidget::CameraGridWidget(rclcpp::Node::SharedPtr node, QWidget * parent)
: QWidget(parent),
  node_(std::move(node))
{
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, QColor(20, 20, 20));
  setPalette(pal);

  it_ = std::make_shared<image_transport::ImageTransport>(node_);

  tick_timer_ = new QTimer(this);
  connect(tick_timer_, &QTimer::timeout, this, &CameraGridWidget::onTick);
  tick_timer_->start(kTickIntervalMs);

  // Default to a 2x2 empty grid until a config arrives.
  config_.rows = 2;
  config_.cols = 2;
  resize_panes(config_, config_.rows, config_.cols);
  build_panes();
}

CameraGridWidget::~CameraGridWidget()
{
  // Stop the tick first so onTick can't touch panes mid-teardown.
  if (tick_timer_) {tick_timer_->stop();}
  teardown_panes();
}

void CameraGridWidget::load_config(const GridConfig & config)
{
  teardown_panes();
  config_ = config;
  // Warn before resize_panes truncates so the user sees the data loss.
  // resize_panes is the single source of truth for pane-count/grid-dim
  // alignment; build_panes just materializes widgets for the already-sized
  // config_.panes vector.
  const size_t total = static_cast<size_t>(config_.rows) * config_.cols;
  if (config_.panes.size() > total) {
    RCLCPP_WARN(
      node_->get_logger(),
      "config has %zu panes but grid has only %zu cells; truncating",
      config_.panes.size(), total);
  }
  resize_panes(config_, config_.rows, config_.cols);
  build_panes();
  relayout();
}

GridConfig CameraGridWidget::get_config() const
{
  return config_;
}

void CameraGridWidget::teardown_panes()
{
  // Deterministic teardown (stability rule 5): delete in order, synchronously.
  // qDeleteAll is safe here because load_config is invoked on the Qt main
  // thread and pane subscriptions never re-enter this path.
  qDeleteAll(panes_);
  panes_.clear();
}

void CameraGridWidget::build_panes()
{
  // Callers must run resize_panes first so config_.panes.size() already
  // equals config_.rows * config_.cols. Just materialize widgets for each.
  panes_.reserve(config_.panes.size());
  for (size_t i = 0; i < config_.panes.size(); ++i) {
    auto * pane = new CameraPaneWidget(node_, it_, config_.panes[i], this);
    connect(pane, &CameraPaneWidget::firstFrameSeen,
            this, &CameraGridWidget::onPaneFirstFrame);
    pane->show();
    panes_.push_back(pane);
  }
}

void CameraGridWidget::onPaneFirstFrame()
{
  const double new_aspect = compute_target_aspect();
  if (std::abs(new_aspect - target_aspect_) > 0.05) {
    target_aspect_ = new_aspect;
    relayout();
  }
}

double CameraGridWidget::compute_target_aspect() const
{
  std::vector<double> aspects;
  for (const auto * p : panes_) {
    if (!p) {continue;}
    double a = p->observed_aspect();
    if (a > 0.0) {
      aspects.push_back(a);
    }
  }
  if (aspects.empty()) {
    return 16.0 / 9.0;
  }
  std::sort(aspects.begin(), aspects.end());
  return aspects[aspects.size() / 2];
}

void CameraGridWidget::onTick()
{
  for (auto * p : panes_) {
    if (p) {p->tick();}
  }
}

void CameraGridWidget::resizeEvent(QResizeEvent * event)
{
  QWidget::resizeEvent(event);
  relayout();
}

void CameraGridWidget::relayout()
{
  const auto cells = compute_grid_layout(
    width(), height(), config_.rows, config_.cols, target_aspect_);
  for (size_t i = 0; i < cells.size() && i < panes_.size(); ++i) {
    auto * p = panes_[i];
    if (!p) {continue;}
    const auto & cg = cells[i];
    p->setGeometry(cg.cell.x, cg.cell.y, cg.cell.w, cg.cell.h);
    // Image rect is relative to the pane's own coordinate system.
    QRect local_image(
      cg.image.x - cg.cell.x, cg.image.y - cg.cell.y,
      cg.image.w, cg.image.h);
    p->set_image_rect(local_image);
  }
}

}  // namespace rqt_camera_grid
