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
#include <QPointer>
#include <QResizeEvent>

#include <rmw/qos_profiles.h>

#include <algorithm>
#include <limits>
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

  // Cross-thread marshaling: ROS executor emits imageReceived; slot runs
  // on the Qt main thread. AutoConnection already resolves to Queued
  // because the emitting thread (ROS executor) differs from the
  // receiver's thread affinity (GUI main), but we pass Qt::QueuedConnection
  // explicitly so the intent survives any future moveToThread refactor.
  connect(this, &CameraPaneWidget::imageReceived,
          this, &CameraPaneWidget::onImageReceived,
          Qt::QueuedConnection);

  if (!config_.base.empty()) {
    subscribe();
  }
  // current_level_ defaults to Error in the header — no frame yet.
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

  // image_transport::subscribe forwards into rclcpp which validates the
  // topic name and can throw (InvalidTopicNameError, plus fastcdr / dds
  // exceptions for malformed strings). TransportHints itself can also
  // throw — its constructor declares the `image_transport` parameter
  // and surfaces ParameterAlreadyDeclared / InvalidParameterType — so
  // it lives inside the try block too. Catch here so a single bad pane
  // config can't terminate() the whole rqt process — the staleness
  // tracker will surface the missing-data state visually.
  //
  // Capture a QPointer rather than raw `this`: the ROS spin thread can
  // deliver an in-flight message to this lambda after the widget has
  // been destroyed (e.g. a config-apply tears down old panes while
  // frames are still arriving on the wire). image_transport's shutdown
  // is not a synchronous drain of pending callbacks — without this
  // guard the lambda dereferences a freed QObject and segfaults inside
  // the Qt signal-emit machinery (QObjectPrivate::maybeSignalConnected).
  // QPointer auto-nulls in ~QObject's clearGuards() so the lambda can
  // early-return. A narrow residual race remains between the null
  // check and the emit if destruction begins concurrently; for full
  // safety the destructor would need to synchronize with the callback
  // path. Acceptable in practice; standard Qt+ROS idiom.
  QPointer<CameraPaneWidget> self(this);
  try {
    image_transport::TransportHints hints(node_.get(), config_.transport);
    sub_ = it_->subscribe(
      config_.base,
      qos,
      [self](const sensor_msgs::msg::Image::ConstSharedPtr & msg) {
        if (!self) {return;}
        self->handleImage(msg);
      },
      image_transport::ImageTransport::VoidPtr(),
      &hints,
      rclcpp::SubscriptionOptions());
  } catch (const std::exception & e) {
    RCLCPP_ERROR(
      node_->get_logger(),
      "rqt_camera_grid: failed to subscribe to '%s' (transport='%s'): %s",
      config_.base.c_str(), config_.transport.c_str(), e.what());
  }
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
  const auto clock_type = node_->get_clock()->get_clock_type();
  const rclcpp::Time now = node_->get_clock()->now();
  // Construct the header stamp with the node's clock type so that
  // (hdr - now) does not throw rclcpp::exceptions::InvalidClockType.
  // builtin_interfaces::msg::Time carries no clock_type tag of its own.
  const rclcpp::Time hdr(msg->header.stamp, clock_type);

  // EWMA inter-arrival interval for the rate label.
  if (first_frame_seen_) {
    const double dt = (now - last_frame_time_).seconds();
    if (dt > 0.0 && dt < 60.0) {  // guard against clock jumps / long pauses
      constexpr double alpha = 0.3;  // ~1s window at 10 Hz
      ewma_interval_s_ = rate_ready_ ?
        (alpha * dt + (1.0 - alpha) * ewma_interval_s_) :
        dt;
      rate_ready_ = true;
    }
  }
  last_frame_time_ = now;

  // Validate header.stamp. Two broken-publisher modes we can detect:
  //   (a) zero stamp — uninitialized builtin_interfaces::msg::Time{}
  //   (b) far-future stamp — publisher clock is unsynchronized with ours,
  //       so stamp_age would be meaningless (or negative forever)
  // Small negative skew (stamp a fraction of a second ahead of us)
  // is fine and commonly caused by normal network delay vs. local
  // clock drift; only flag stamps more than kMaxFutureSkewS ahead.
  constexpr double kMaxFutureSkewS = 60.0;
  const bool stamp_zero =
    (msg->header.stamp.sec == 0 && msg->header.stamp.nanosec == 0);
  double future_skew_s = 0.0;
  if (!stamp_zero) {
    future_skew_s = (hdr - now).seconds();
  }
  const bool stamp_valid = !stamp_zero && future_skew_s < kMaxFutureSkewS;

  staleness_.mark_frame(now, hdr, stamp_valid);

  // Drop to Neutral immediately on frame arrival instead of waiting up to
  // 1s for the next QTimer tick. Matches operator expectation that a pane
  // returning to Neutral (recovery) tracks the frame, not the wall clock.
  current_level_ = staleness_.tick(now);

  pixmap_ = toPixmap(msg);
  // Source changed; invalidate the cached scaled pixmap.
  cached_scaled_ = QPixmap();

  update_label(now);

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

void CameraPaneWidget::update_label(const rclcpp::Time & now)
{
  if (!staleness_.has_frames()) {
    // Keep the base-only label the constructor set until a frame arrives.
    return;
  }

  constexpr double kLabelPeriodS = 1.0;
  const double since_update =
    (last_label_update_.nanoseconds() == 0) ?
    std::numeric_limits<double>::infinity() :
    (now - last_label_update_).seconds();
  // If the clock jumps backwards (e.g. use_sim_time reset), since_update
  // goes negative — force a refresh rather than throttle indefinitely
  // against a "future" last_label_update_.
  if (since_update >= 0.0 && since_update < kLabelPeriodS) {
    return;
  }

  QString text = QString::fromStdString(config_.base);
  if (rate_ready_ && ewma_interval_s_ > 0.0) {
    const double rate_hz = 1.0 / ewma_interval_s_;
    text += QString("  %1 Hz").arg(rate_hz, 0, 'f', 1);
  }

  // Clamp negative ages (small skew / clock jitter) to 0 for display; the
  // tracker still reports Error for truly negative worst-of-both ages, so
  // the border already conveys the discontinuity.
  const double arr_age = std::max(0.0, staleness_.arrival_age(now));
  if (staleness_.last_stamp_valid()) {
    const double hdr_age = std::max(0.0, staleness_.stamp_age(now));
    text += QString("  rx %1s | hdr %2s")
      .arg(arr_age, 0, 'f', 1)
      .arg(hdr_age, 0, 'f', 1);
  } else {
    text += QString("  rx %1s | [no stamp]").arg(arr_age, 0, 'f', 1);
  }

  if (text != last_label_text_) {
    label_->setText(text);
    label_->adjustSize();
    last_label_text_ = text;
  }
  last_label_update_ = now;
}

QPixmap CameraPaneWidget::toPixmap(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
{
  // rgb8: wrap the ROS buffer in a QImage view and convert directly to
  // QPixmap. QPixmap::fromImage copies pixels into the display-native
  // format, so the returned pixmap is independent of msg. No view.copy()
  // needed — previous form did one full-frame copy via QImage::copy()
  // followed by another via QPixmap::fromImage(). One copy per frame now.
  if (msg->encoding == "rgb8") {
    QImage view(
      msg->data.data(), msg->width, msg->height,
      static_cast<int>(msg->step), QImage::Format_RGB888);
    return QPixmap::fromImage(view);
  }

  // cv_bridge fallback for common encodings.
  if (msg->encoding == "bgr8" || msg->encoding == "mono8") {
    try {
      // toCvCopy already allocates for the encoding conversion. Wrap the
      // converted cv::Mat in a QImage view and hand directly to
      // QPixmap::fromImage — still inside the try scope so cv_ptr is
      // alive through the conversion.
      auto cv_ptr = cv_bridge::toCvCopy(msg, "rgb8");
      QImage view(
        cv_ptr->image.data, cv_ptr->image.cols, cv_ptr->image.rows,
        static_cast<int>(cv_ptr->image.step), QImage::Format_RGB888);
      return QPixmap::fromImage(view);
    } catch (const cv_bridge::Exception & e) {
      // Fall through to unsupported-encoding handling below.
      RCLCPP_WARN(
        node_->get_logger(), "cv_bridge failed on pane '%s' (%s): %s",
        config_.base.c_str(), msg->encoding.c_str(), e.what());
      return QPixmap();
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
  return QPixmap();
}

void CameraPaneWidget::tick()
{
  const auto now = node_->get_clock()->now();
  auto level = staleness_.tick(now);
  if (level != current_level_) {
    current_level_ = level;
    update();
  }
  // Refresh the label so the arrival/header ages keep climbing visibly
  // when frames stop arriving — otherwise the last onImageReceived write
  // would freeze on screen and defeat the purpose of the age readout.
  update_label(now);
}

double CameraPaneWidget::observed_aspect() const
{
  return observed_aspect_;
}

void CameraPaneWidget::set_image_rect(const QRect & rect)
{
  if (rect.size() != image_rect_.size()) {
    // Size changed — drop the scaled cache so paintEvent rebuilds it.
    cached_scaled_ = QPixmap();
  }
  image_rect_ = rect;
  image_rect_explicit_ = true;
  // CameraGridWidget::relayout() calls setGeometry() (which fires
  // resizeEvent → label move) BEFORE calling set_image_rect(), so the
  // label above was positioned using the previous image_rect_. Redo
  // the label positioning here so it follows image_rect_'s latest
  // value after every reconfigure.
  label_->move(image_rect_.x() + 4, image_rect_.y() + 4);
  label_->adjustSize();
  label_->raise();
  update();
}

void CameraPaneWidget::resizeEvent(QResizeEvent * event)
{
  QFrame::resizeEvent(event);
  // Auto-track the full widget area unless an owner has explicitly
  // laid us out via set_image_rect. CameraGridWidget does that on every
  // relayout; ThumbnailCell (and the lifecycle test) does not, and
  // relies on this path so the image scales to match the widget on
  // every resize, not just the first one.
  if (!image_rect_explicit_) {
    const QRect full(0, 0, width(), height());
    if (full.size() != image_rect_.size()) {
      cached_scaled_ = QPixmap();
    }
    image_rect_ = full;
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
    // Rebuild the scaled cache only when the source frame or image rect
    // changed; otherwise reuse. paintEvent fires on exposure/overdraw even
    // when nothing meaningful has changed — SmoothTransformation rescale is
    // too expensive to do unconditionally with multiple panes.
    if (cached_scaled_.isNull() || cached_scaled_size_ != image_rect.size()) {
      cached_scaled_ = pixmap_.scaled(
        image_rect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
      cached_scaled_size_ = image_rect.size();
    }
    // Center within image_rect (letterbox for mismatched aspects).
    QRect target = cached_scaled_.rect();
    target.moveCenter(image_rect.center());
    painter.drawPixmap(target.topLeft(), cached_scaled_);
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
