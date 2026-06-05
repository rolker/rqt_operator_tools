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

#include <mutex>
#include <string>

#include <marine_acoustic_msgs/msg/raw_sonar_image.hpp>
#include <marine_radar_control_msgs/msg/radar_control_set.hpp>
#include <marine_radar_control_msgs/msg/radar_control_value.hpp>
#include <rclcpp/rclcpp.hpp>

#include "rqt_sonar_waterfall/ping_pairer.hpp"
#include "rqt_sonar_waterfall/row_extractor.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QTimer;
class QWidget;

namespace rqt_sonar_waterfall
{

class WaterfallWidget;
class ControlPanel;

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
  void publish_control(const QString & key, const QString & value);
  void subscribe(
    rclcpp::Subscription<marine_acoustic_msgs::msg::RawSonarImage>::SharedPtr & sub,
    const std::string & topic, bool is_port);
  void on_port_msg(marine_acoustic_msgs::msg::RawSonarImage::ConstSharedPtr msg);
  void on_starboard_msg(marine_acoustic_msgs::msg::RawSonarImage::ConstSharedPtr msg);
  void post_row(const WaterfallRow & row);
  void update_active_sides();

  QPointer<WaterfallWidget> widget_;
  QComboBox * port_combo_ = nullptr;
  QComboBox * starboard_combo_ = nullptr;
  QComboBox * control_combo_ = nullptr;
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

  rclcpp::Subscription<marine_acoustic_msgs::msg::RawSonarImage>::SharedPtr port_sub_;
  rclcpp::Subscription<marine_acoustic_msgs::msg::RawSonarImage>::SharedPtr
    starboard_sub_;

  SingleBeamExtractor extractor_;
  PingPairer pairer_;
  std::mutex pairer_mutex_;  ///< guards pairer_ across the executor/GUI threads
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__SONAR_WATERFALL_PLUGIN_HPP_
