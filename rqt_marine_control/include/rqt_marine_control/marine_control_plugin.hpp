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

#ifndef RQT_MARINE_CONTROL__MARINE_CONTROL_PLUGIN_HPP_
#define RQT_MARINE_CONTROL__MARINE_CONTROL_PLUGIN_HPP_

#include <QPointer>
#include <QString>

#include <rqt_gui_cpp/plugin.h>

#include <mutex>

#include <memory>
#include <vector>

#include <marine_control_interfaces/msg/control_set.hpp>
#include <marine_control_interfaces/msg/control_value.hpp>
#include <rclcpp/rclcpp.hpp>

#include "marine_control_bridge_client/bridge_control_client.hpp"
#include "marine_control_widgets/control_set_widget.hpp"

class QComboBox;
class QLabel;
class QPushButton;
class QWidget;

namespace rqt_marine_control
{

/// Generic rqt plugin for the bridgeable device-control contract (ADR-0003):
/// pick a ControlSet state topic, render it with a ControlSetWidget, and publish
/// a ControlValue to the matching change topic on edit. State is subscribed
/// RELIABLE + VOLATILE (ADR-0003 D5) to match the device library's publisher;
/// the change is fire-and-forget, confirmed by the next state echo. Incoming
/// sets are marshalled from the executor thread onto the GUI thread.
class MarineControlPlugin : public rqt_gui_cpp::Plugin
{
  Q_OBJECT

public:
  MarineControlPlugin();

  void initPlugin(qt_gui_cpp::PluginContext & context) override;
  void shutdownPlugin() override;
  void saveSettings(
    qt_gui_cpp::Settings & plugin_settings,
    qt_gui_cpp::Settings & instance_settings) const override;
  void restoreSettings(
    const qt_gui_cpp::Settings & plugin_settings,
    const qt_gui_cpp::Settings & instance_settings) override;

  /// Subscription callback — runs on the executor thread.
  virtual void controlSetCallback(
    marine_control_interfaces::msg::ControlSet::ConstSharedPtr message);

protected slots:
  void updateTopicList();
  void selectTopic(const QString & topic);
  void onTopicChanged(int index);
  void applyLatest();   // GUI thread: render the most recent set
  void publishChange(const QString & name, const QString & value);
  // Dynamic-bridge (D7-dyn) UI, all GUI thread:
  void updateBridgeList();        // discover udp_bridge nodes -> bridge_combo_
  void onBridgeChanged(int index);  // (re)build the client for the selected bridge
  void refreshDevices();          // rebuild device_combo_ from discovered devices
  void onDeviceChanged(int index);  // sync the connect button to the device's state
  void onConnectClicked();        // explicit connect/disconnect of the selected device

private:
  QWidget * widget_ = nullptr;
  QComboBox * topic_combo_ = nullptr;
  QComboBox * bridge_combo_ = nullptr;
  QComboBox * device_combo_ = nullptr;
  QPushButton * connect_button_ = nullptr;
  QLabel * status_label_ = nullptr;
  std::unique_ptr<marine_control_bridge_client::BridgeControlClient> bridge_client_;
  std::vector<marine_control_bridge_client::ControlDevice> devices_;  // GUI thread
  /// QPointer auto-nulls on teardown so a queued applyLatest() can't touch a
  /// freed widget on the GUI thread.
  QPointer<marine_control_widgets::ControlSetWidget> control_widget_;
  QString arg_topic_;
  /// Set in shutdownPlugin() so a deferred populate queued in initPlugin
  /// (QTimer::singleShot) becomes a no-op if it fires during teardown.
  bool shutting_down_ = false;

  rclcpp::Subscription<marine_control_interfaces::msg::ControlSet>::SharedPtr state_sub_;
  rclcpp::Publisher<marine_control_interfaces::msg::ControlValue>::SharedPtr change_pub_;

  std::mutex latest_mutex_;
  marine_control_interfaces::msg::ControlSet latest_;
  bool have_latest_ = false;
};

}  // namespace rqt_marine_control

#endif  // RQT_MARINE_CONTROL__MARINE_CONTROL_PLUGIN_HPP_
