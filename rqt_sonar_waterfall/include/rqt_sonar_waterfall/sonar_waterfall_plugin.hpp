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

#ifndef RQT_SONAR_WATERFALL__SONAR_WATERFALL_PLUGIN_HPP_
#define RQT_SONAR_WATERFALL__SONAR_WATERFALL_PLUGIN_HPP_

#include <QObject>  // NOLINT(build/include_order)
#include <QPointer>
#include <QString>

#include <rqt_gui_cpp/plugin.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include <marine_acoustic_msgs/msg/raw_sonar_image.hpp>
#include <marine_interfaces/msg/contact.hpp>
#include <marine_radar_control_msgs/msg/radar_control_set.hpp>
#include <marine_radar_control_msgs/msg/radar_control_value.hpp>
#include <sensor_msgs/msg/range.hpp>
#include <rclcpp/rclcpp.hpp>
#include <tf2_ros/buffer.h>  // NOLINT(build/include_order)
#include <tf2_ros/transform_listener.h>  // NOLINT(build/include_order)

#include "rqt_sonar_waterfall/ping_pairer.hpp"
#include "rqt_sonar_waterfall/row_extractor.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QWidget;

namespace rqt_sonar_waterfall
{

class WaterfallWidget;
class ControlPanel;
struct MarkBox;

/// rqt plugin entry point for the sonar backscatter waterfall viewer.
///
/// Hosts a topic-selection toolbar (live RawSonarImage discovery) over the
/// WaterfallWidget, subscribes to a port and/or starboard topic, and feeds the
/// pings through SingleBeamExtractor + PingPairer into the widget. Subscription
/// callbacks run on rqt's executor thread; rows are marshaled to the GUI thread
/// before touching the widget. The optional control panel (issue #39, later
/// step) and multibeam extractor (#40) attach to the same toolbar.
class SonarWaterfallPlugin : public rqt_gui_cpp::Plugin
{
  Q_OBJECT

public:
  SonarWaterfallPlugin();
  ~SonarWaterfallPlugin() override;

  void initPlugin(qt_gui_cpp::PluginContext & context) override;
  void shutdownPlugin() override;
  void saveSettings(
    qt_gui_cpp::Settings & plugin_settings,
    qt_gui_cpp::Settings & instance_settings) const override;
  void restoreSettings(
    const qt_gui_cpp::Settings & plugin_settings,
    const qt_gui_cpp::Settings & instance_settings) override;

private:
  QWidget * build_controls_bar(QWidget * parent);
  void apply_view_settings();
  void refresh_topics();
  void on_port_topic_changed(const QString & topic);
  void on_starboard_topic_changed(const QString & topic);
  void on_control_topic_changed(const QString & topic);
  void on_control_set(
    marine_radar_control_msgs::msg::RadarControlSet::ConstSharedPtr msg);
  void on_depth_topic_changed(const QString & topic);
  void on_depth_msg(sensor_msgs::msg::Range::ConstSharedPtr msg);
  void publish_control(const QString & key, const QString & value);
  void subscribe(
    rclcpp::Subscription<marine_acoustic_msgs::msg::RawSonarImage>::SharedPtr & sub,
    const std::string & topic, bool is_port);
  void on_port_msg(
    marine_acoustic_msgs::msg::RawSonarImage::ConstSharedPtr msg, uint64_t sub_id);
  void on_starboard_msg(
    marine_acoustic_msgs::msg::RawSonarImage::ConstSharedPtr msg, uint64_t sub_id);
  void post_row(const WaterfallRow & row);
  void maybe_seed_manual_range(uint32_t dtype);
  void update_active_sides();
  /// Georeference an operator-marked box and publish a marine_interfaces/Contact
  /// (ORIGIN_HUMAN, STATUS_PROPOSED, BOX) for the operator bag (issue #86).
  void on_box_marked(const MarkBox & box);
  /// Update the world TF frame from the toolbar field (issue #86). Trims blanks
  /// and falls back to "earth" so the pose lookup never targets an empty frame.
  void set_world_frame(const QString & frame);

  QPointer<WaterfallWidget> widget_;
  QComboBox * port_combo_ = nullptr;
  QComboBox * starboard_combo_ = nullptr;
  QComboBox * control_combo_ = nullptr;
  QComboBox * depth_combo_ = nullptr;
  QTimer * refresh_timer_ = nullptr;

  ControlPanel * control_panel_ = nullptr;
  QWidget * control_section_ = nullptr;  ///< scroll area shown only when controllable
  rclcpp::Subscription<marine_radar_control_msgs::msg::RadarControlSet>::SharedPtr
    control_sub_;
  rclcpp::Publisher<marine_radar_control_msgs::msg::RadarControlValue>::SharedPtr
    control_pub_;

  // View-knob controls (wired to WaterfallWidget setters).
  QComboBox * colormap_combo_ = nullptr;
  QDoubleSpinBox * gain_spin_ = nullptr;
  QDoubleSpinBox * contrast_spin_ = nullptr;
  QSpinBox * history_spin_ = nullptr;
  QCheckBox * auto_range_check_ = nullptr;
  QDoubleSpinBox * range_min_spin_ = nullptr;
  QDoubleSpinBox * range_max_spin_ = nullptr;
  QPushButton * freeze_button_ = nullptr;

  // Geometry / correction controls (issue #58), wired to WaterfallWidget setters.
  QCheckBox * ground_check_ = nullptr;
  QCheckBox * uniform_check_ = nullptr;
  QCheckBox * range_lines_check_ = nullptr;
  QDoubleSpinBox * density_spin_ = nullptr;
  QCheckBox * tvg_check_ = nullptr;
  QDoubleSpinBox * tvg_slope_spin_ = nullptr;

  // Target marking (issue #86): a toggle button drives the widget's mark mode;
  // marked boxes are georeferenced via TF and published as Contacts.
  QPushButton * mark_button_ = nullptr;
  std::shared_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
  rclcpp::Publisher<marine_interfaces::msg::Contact>::SharedPtr contact_pub_;
  /// Relative topic the marked Contacts publish on (resolves under the rqt node's
  /// namespace). The operator bag must record the resolved absolute topic.
  std::string contact_topic_ = "sonar_waterfall/contacts";
  /// Toolbar field that sets `world_frame_`.
  QLineEdit * frame_edit_ = nullptr;
  /// World/earth TF frame the marked-target pose lookup resolves against
  /// (REP-105 ECEF by default). Operator-configurable via the toolbar Frame
  /// field and persisted per perspective. Written on the GUI thread (the line
  /// edit) and read on the executor thread (post_row's TF lookup), so both ends
  /// hold world_frame_mutex_; an empty field falls back to "earth".
  std::string world_frame_ = "earth";
  mutable std::mutex world_frame_mutex_;
  /// Monotonic counter for unique human-readable Contact ids within a session.
  std::uint64_t mark_counter_ = 0;

  rclcpp::Subscription<marine_acoustic_msgs::msg::RawSonarImage>::SharedPtr port_sub_;
  rclcpp::Subscription<marine_acoustic_msgs::msg::RawSonarImage>::SharedPtr
    starboard_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Range>::SharedPtr depth_sub_;

  /// Latest sonar altitude (nadir depth), metres, stamped onto each row at
  /// post_row. Written by the depth callback and read by the image callbacks —
  /// all on rqt's single executor thread, so no lock; atomic guards against a
  /// future multi-threaded executor.
  std::atomic<double> latest_altitude_{0.0};

  SingleBeamExtractor extractor_;
  PingPairer pairer_;
  std::mutex pairer_mutex_;  ///< guards pairer_ across the executor/GUI threads

  /// Set once the manual-range spin default has been seeded from the first
  /// message's dtype since the last (re)subscribe (see maybe_seed_manual_range).
  /// Reset in subscribe() so a newly selected source re-seeds. Atomic because
  /// the port and starboard callbacks both touch it from the executor thread.
  std::atomic<bool> range_seeded_{false};

  /// Monotonic per-side subscription identifiers. subscribe() bumps the side's
  /// id whenever it (re)creates or clears that side's subscription, and stamps
  /// the new id into the message callback. A callback whose id no longer matches
  /// is from a replaced subscription (an in-flight message that outlived a topic
  /// switch) and is ignored — without this, such a stray message could seed the
  /// manual range from the wrong source. Atomic: written on the GUI thread
  /// (subscribe), read on the executor thread (callbacks).
  std::atomic<uint64_t> port_sub_id_{0};
  std::atomic<uint64_t> starboard_sub_id_{0};
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__SONAR_WATERFALL_PLUGIN_HPP_
