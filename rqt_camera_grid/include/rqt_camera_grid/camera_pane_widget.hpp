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

#ifndef RQT_CAMERA_GRID__CAMERA_PANE_WIDGET_HPP_
#define RQT_CAMERA_GRID__CAMERA_PANE_WIDGET_HPP_

#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPixmap>

#include <memory>
#include <string>

#include <image_transport/image_transport.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "rqt_camera_grid/config_model.hpp"
#include "rqt_camera_grid/staleness_tracker.hpp"

// Required for cross-thread Qt signal delivery of the image payload.
// Without this, Qt::QueuedConnection emits a runtime warning
// ("Cannot queue arguments of type ...") and the slot never fires.
Q_DECLARE_METATYPE(sensor_msgs::msg::Image::ConstSharedPtr)

namespace rqt_camera_grid
{

class CameraPaneWidget : public QFrame
{
  Q_OBJECT

public:
  CameraPaneWidget(
    rclcpp::Node::SharedPtr node,
    std::shared_ptr<image_transport::ImageTransport> it,
    const PaneConfig & config,
    QWidget * parent = nullptr);

  ~CameraPaneWidget() override;

  // Called by CameraGridWidget every ~1 Hz to update the staleness border.
  void tick();

  // Returns observed image aspect (width/height) after the first frame;
  // returns 0.0 if no frame has been seen yet.
  double observed_aspect() const;

  const PaneConfig & pane_config() const {return config_;}

  // Set the image render rect inside this widget (cell minus outer padding).
  // Staleness border wraps this rect, not the full widget. Called by the
  // parent on every resizeEvent / relayout.
  void set_image_rect(const QRect & rect);

signals:
  // Emitted from the ROS callback thread with a refcounted message payload.
  // The slot-connected receiver runs on the Qt main thread.
  void imageReceived(sensor_msgs::msg::Image::ConstSharedPtr msg);

  // Emitted the first time a frame arrives, so the grid widget can update
  // its target aspect and relayout if the new aspect shifts the median.
  void firstFrameSeen();

private slots:
  void onImageReceived(sensor_msgs::msg::Image::ConstSharedPtr msg);

protected:
  void paintEvent(QPaintEvent * event) override;
  void resizeEvent(QResizeEvent * event) override;

private:
  void handleImage(const sensor_msgs::msg::Image::ConstSharedPtr & msg);
  void subscribe();
  void unsubscribe();
  void applyBorder(StalenessTracker::Level level);
  QImage toQImage(const sensor_msgs::msg::Image::ConstSharedPtr & msg);

  rclcpp::Node::SharedPtr node_;
  std::shared_ptr<image_transport::ImageTransport> it_;
  PaneConfig config_;

  image_transport::Subscriber sub_;
  StalenessTracker staleness_;
  StalenessTracker::Level current_level_{StalenessTracker::Level::Error};

  QPixmap pixmap_;
  QRect image_rect_;  // logical pixels inside this widget
  double observed_aspect_{0.0};
  bool first_frame_seen_{false};
  bool encoding_warned_{false};
  std::string last_warned_encoding_;

  QLabel * label_;
};

}  // namespace rqt_camera_grid

#endif  // RQT_CAMERA_GRID__CAMERA_PANE_WIDGET_HPP_
