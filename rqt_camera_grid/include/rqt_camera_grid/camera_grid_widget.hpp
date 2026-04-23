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

#ifndef RQT_CAMERA_GRID__CAMERA_GRID_WIDGET_HPP_
#define RQT_CAMERA_GRID__CAMERA_GRID_WIDGET_HPP_

#include <QTimer>
#include <QWidget>

#include <memory>
#include <vector>

#include <image_transport/image_transport.hpp>
#include <rclcpp/rclcpp.hpp>

#include "rqt_camera_grid/config_model.hpp"

namespace rqt_camera_grid
{

class CameraPaneWidget;

class CameraGridWidget : public QWidget
{
  Q_OBJECT

public:
  explicit CameraGridWidget(rclcpp::Node::SharedPtr node, QWidget * parent = nullptr);
  ~CameraGridWidget() override;

  // Applies a config atomically: tears down existing panes on the Qt main
  // thread, then constructs new ones. Never call from inside an image
  // callback; the dialog and plugin invoke this from OK / restoreSettings.
  void load_config(const GridConfig & config);

  // Returns a copy of the current config, including the rows/cols.
  GridConfig get_config() const;

protected:
  void resizeEvent(QResizeEvent * event) override;

private slots:
  void onPaneFirstFrame();
  void onTick();

private:
  void teardown_panes();
  void build_panes();
  void relayout();
  double compute_target_aspect() const;

  rclcpp::Node::SharedPtr node_;
  std::shared_ptr<image_transport::ImageTransport> it_;
  GridConfig config_;
  std::vector<CameraPaneWidget *> panes_;
  QTimer * tick_timer_;
  double target_aspect_{16.0 / 9.0};
};

}  // namespace rqt_camera_grid

#endif  // RQT_CAMERA_GRID__CAMERA_GRID_WIDGET_HPP_
