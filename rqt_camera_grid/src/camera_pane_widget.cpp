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

#include "rqt_camera_grid/camera_pane_widget.hpp"

#include <QColor>
#include <QHBoxLayout>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>

#include <rmw/qos_profiles.h>

#include <algorithm>
#include <memory>
#include <utility>

#include <cv_bridge/cv_bridge.hpp>

namespace rqt_camera_grid
{

namespace
{
// Dark-until-problem palette — aligned with rqt_annunciator's indicator_widget.
const QColor kBgColor(30, 30, 30);
const QColor kBorderNeutral(60, 60, 60);   // dim gray, blends into background
const QColor kBorderWarn(255, 180, 0);     // amber
const QColor kBorderError(220, 40, 40);    // red
const QColor kLabelColor(200, 200, 200);   // soft white for legibility
}  // namespace

CameraPaneWidget::CameraPaneWidget(
  rclcpp::Node::SharedPtr node,
  std::shared_ptr<image_transport::ImageTransport> it,
  const PaneConfig & config,
  QWidget * parent)
: QFrame(parent),
  node_(std::move(node)),
  it_(std::move(it)),
  config_(config),
  staleness_(config.warn_s, config.error_s)
{
  // Register the ROS message shared-ptr type with Qt's meta-object system
  // exactly once per process. Without this the auto-queued connection
  // from the ROS executor thread drops signals with a runtime warning.
  static bool s_meta_registered = []() {
      qRegisterMetaType<sensor_msgs::msg::Image::ConstSharedPtr>(
        "sensor_msgs::msg::Image::ConstSharedPtr");
      return true;
    }();
  (void)s_meta_registered;

  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, kBgColor);
  setPalette(pal);
  setFrameShape(QFrame::NoFrame);

  label_ = new QLabel(QString::fromStdString(config_.base), this);
  label_->setStyleSheet(
    "QLabel { color: rgb(200,200,200); background: rgba(0,0,0,128); "
    "padding: 2px 6px; }");
  label_->move(4, 4);
  label_->adjustSize();
  label_->raise();

  // Cross-thread marshaling: ROS executor emits imageReceived; Qt slot
  // runs on the main thread via Qt::QueuedConnection (auto when emitter
  // and receiver are on different threads).
  connect(this, &CameraPaneWidget::imageReceived,
          this, &CameraPaneWidget::onImageReceived);

  if (!config_.base.empty()) {
    subscribe();
  }
  applyBorder(StalenessTracker::Level::Error);
}

CameraPaneWidget::~CameraPaneWidget()
{
  unsubscribe();
}

void CameraPaneWidget::subscribe()
{
  if (!it_ || config_.base.empty()) {return;}
  rmw_qos_profile_t qos = rmw_qos_profile_sensor_data;
  qos.depth = 1;  // we only paint the latest frame

  image_transport::TransportHints hints(node_.get(), config_.transport);

  // Copy base to avoid the subscriber capturing a reference into config_
  // (config_ is stable for the widget's lifetime, but being explicit).
  sub_ = it_->subscribe(
    config_.base,
    qos,
    [this](const sensor_msgs::msg::Image::ConstSharedPtr & msg) {
      this->handleImage(msg);
    },
    image_transport::ImageTransport::VoidPtr(),
    &hints,
    rclcpp::SubscriptionOptions());
}

void CameraPaneWidget::unsubscribe()
{
  sub_.shutdown();
}

void CameraPaneWidget::handleImage(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
{
  // ROS executor thread. Forward to the Qt main thread via signal.
  emit imageReceived(msg);
}

void CameraPaneWidget::onImageReceived(sensor_msgs::msg::Image::ConstSharedPtr msg)
{
  // Qt main thread.
  if (!msg || msg->width == 0 || msg->height == 0) {
    return;
  }
  staleness_.mark_frame(node_->get_clock()->now());

  QImage qimg = toQImage(msg);
  if (!qimg.isNull()) {
    pixmap_ = QPixmap::fromImage(qimg);
  } else {
    pixmap_ = QPixmap();
  }

  // Track aspect for the grid widget to pick up in firstFrameSeen.
  const double aspect = (msg->height > 0) ?
    static_cast<double>(msg->width) / static_cast<double>(msg->height) :
    0.0;
  observed_aspect_ = aspect;
  if (!first_frame_seen_) {
    first_frame_seen_ = true;
    emit firstFrameSeen();
  }
  update();
}

QImage CameraPaneWidget::toQImage(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
{
  // Direct zero-copy for rgb8.
  if (msg->encoding == "rgb8") {
    QImage view(
      msg->data.data(), msg->width, msg->height,
      static_cast<int>(msg->step), QImage::Format_RGB888);
    return view.copy();  // detach from the message buffer before it goes away
  }

  // cv_bridge fallback for common encodings.
  if (msg->encoding == "bgr8" || msg->encoding == "mono8") {
    try {
      // Pass the ConstSharedPtr directly; avoids an extra full-frame copy.
      auto cv_ptr = cv_bridge::toCvCopy(msg, "rgb8");
      return QImage(
        cv_ptr->image.data, cv_ptr->image.cols, cv_ptr->image.rows,
        static_cast<int>(cv_ptr->image.step), QImage::Format_RGB888).copy();
    } catch (const cv_bridge::Exception & e) {
      // Fall through to unsupported-encoding handling below.
      RCLCPP_WARN(
        node_->get_logger(), "cv_bridge failed on pane '%s' (%s): %s",
        config_.base.c_str(), msg->encoding.c_str(), e.what());
      return QImage();
    }
  }

  // Unsupported encoding: log once per distinct encoding, return null.
  if (!encoding_warned_ || last_warned_encoding_ != msg->encoding) {
    RCLCPP_WARN(
      node_->get_logger(),
      "pane '%s': unsupported encoding '%s' — rendering placeholder",
      config_.base.c_str(), msg->encoding.c_str());
    encoding_warned_ = true;
    last_warned_encoding_ = msg->encoding;
  }
  return QImage();
}

void CameraPaneWidget::tick()
{
  auto level = staleness_.tick(node_->get_clock()->now());
  if (level != current_level_) {
    current_level_ = level;
    applyBorder(level);
    update();
  }
}

void CameraPaneWidget::applyBorder(StalenessTracker::Level level)
{
  current_level_ = level;
}

double CameraPaneWidget::observed_aspect() const
{
  return observed_aspect_;
}

void CameraPaneWidget::set_image_rect(const QRect & rect)
{
  image_rect_ = rect;
  update();
}

void CameraPaneWidget::resizeEvent(QResizeEvent * event)
{
  QFrame::resizeEvent(event);
  // If the grid widget hasn't set an explicit image_rect, default to full
  // widget area (useful for the lifecycle test where the pane isn't parented
  // to a grid).
  if (image_rect_.isEmpty()) {
    image_rect_ = QRect(0, 0, width(), height());
  }
  label_->move(image_rect_.x() + 4, image_rect_.y() + 4);
  label_->adjustSize();
  label_->raise();
}

void CameraPaneWidget::paintEvent(QPaintEvent * event)
{
  QFrame::paintEvent(event);

  QPainter painter(this);
  painter.fillRect(rect(), kBgColor);

  const QRect image_rect = image_rect_.isEmpty() ?
    QRect(0, 0, width(), height()) :
    image_rect_;

  if (!pixmap_.isNull() && !image_rect.isEmpty()) {
    QPixmap scaled = pixmap_.scaled(
      image_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // Center within image_rect (letterbox for mismatched aspects).
    QRect target = scaled.rect();
    target.moveCenter(image_rect.center());
    painter.drawPixmap(target.topLeft(), scaled);
  }

  // Staleness border wraps image_rect.
  QColor border;
  switch (current_level_) {
    case StalenessTracker::Level::Warn:
      border = kBorderWarn;
      break;
    case StalenessTracker::Level::Error:
      border = kBorderError;
      break;
    case StalenessTracker::Level::Neutral:
    default:
      border = kBorderNeutral;
      break;
  }
  painter.setPen(QPen(border, 3));
  painter.setBrush(Qt::NoBrush);
  // Inset by 1 so the border is fully visible at the edges.
  QRect border_rect = image_rect.adjusted(1, 1, -1, -1);
  painter.drawRect(border_rect);
}

}  // namespace rqt_camera_grid
