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

#ifndef RQT_MARINE_SONAR__MARINE_ECHOGRAM_PLUGIN_HPP_
#define RQT_MARINE_SONAR__MARINE_ECHOGRAM_PLUGIN_HPP_

#include <QString>

#include <rqt_gui_cpp/plugin.h>
#include <ui_marine_echogram_plugin.h>

#include <mutex>
#include <vector>

#include <marine_acoustic_msgs/msg/raw_sonar_image.hpp>
#include <rclcpp/rclcpp.hpp>

namespace rqt_marine_sonar
{

/// rqt plugin entry point for the water-column echogram viewer. Hosts a
/// topic-selection toolbar (live RawSonarImage discovery) and the dB / ping-
/// spacing controls over an EchogramWidget, subscribes to the selected topic,
/// and marshals incoming pings from the executor thread onto the GUI thread.
class MarineEchogramPlugin : public rqt_gui_cpp::Plugin
{
  Q_OBJECT

public:
  MarineEchogramPlugin();

  void initPlugin(qt_gui_cpp::PluginContext & context) override;
  void shutdownPlugin() override;
  void saveSettings(
    qt_gui_cpp::Settings & plugin_settings,
    qt_gui_cpp::Settings & instance_settings) const override;
  void restoreSettings(
    const qt_gui_cpp::Settings & plugin_settings,
    const qt_gui_cpp::Settings & instance_settings) override;

  virtual void dataCallback(
    marine_acoustic_msgs::msg::RawSonarImage::ConstSharedPtr message);

protected slots:
  virtual void updateTopicList();
  virtual void selectTopic(const QString & topic);
  virtual void onTopicChanged(int index);
  void newPings();

  void on_minDbDoubleSpinBox_valueChanged(double value);
  void on_maxDbDoubleSpinBox_valueChanged(double value);
  void on_pingSpacingDoubleSpinBox_valueChanged(double value);

private:
  Ui::MarineEchogramWidget ui_;
  QWidget * widget_ = nullptr;

  QString arg_topic_;

  rclcpp::Subscription<marine_acoustic_msgs::msg::RawSonarImage>::SharedPtr
    data_subscriber_;

  std::vector<marine_acoustic_msgs::msg::RawSonarImage> new_pings_;
  std::mutex new_pings_mutex_;
};

}  // namespace rqt_marine_sonar

#endif  // RQT_MARINE_SONAR__MARINE_ECHOGRAM_PLUGIN_HPP_
