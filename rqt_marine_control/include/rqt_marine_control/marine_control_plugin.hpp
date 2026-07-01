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

#include <rqt_gui_cpp/plugin.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include "marine_control_bridge_client/bridge_control_client.hpp"
#include "rqt_marine_control/connections_hub_widget.hpp"
#include "rqt_marine_control/tab_manager.hpp"

namespace rqt_marine_control
{

class ResponsiveHubLayout;

/// Generic rqt plugin for the bridgeable device-control contract (ADR-0003):
/// present a Connections hub — a checklist of every local and remote control
/// device — and open one tab per checked device, each rendered with a
/// ControlSetWidget that publishes a ControlValue to the matching change topic on
/// edit. State is subscribed RELIABLE + VOLATILE (ADR-0003 D5) to match the device
/// library's publisher; the change is fire-and-forget, confirmed by the next state
/// echo. Per-tab subscriptions are owned by a TabManager; incoming sets are
/// marshalled from the executor thread onto the GUI thread (see TabManager's
/// threading note). The hub and its responsive tab/side-panel arrangement are the
/// ConnectionsHubWidget and ResponsiveHubLayout; this plugin owns the rclcpp
/// plumbing (node, BridgeControlClient, transport factory) and wires it to the hub
/// through the injected BridgeControlHooks seam.
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

private:
  // Build the real (rclcpp-backed) transport factory the TabManager uses to wire
  // each tab's subscription/publisher.
  TabTransportFactory makeTransportFactory();
  // Build the injected functor seam the hub drives the bridge through. The hooks
  // read the plugin's CURRENT bridge_client_, so they stay valid across a bridge
  // rebuild without being re-handed to the hub.
  BridgeControlHooks makeBridgeHooks();
  // Node-graph queries for the hub's Local and Bridge sections (GUI thread; empty
  // during teardown or before the node exists).
  std::vector<std::string> localControlTopics();
  std::vector<std::string> bridgeNodes();
  // Rebuild the BridgeControlClient for a newly selected bridge node (GUI thread),
  // re-registering the hub's devices-changed callback on the fresh client.
  void onBridgeSelected(const std::string & bridge_node);

  // Top-level widget added to the rqt container; owns the hub + tab widget. Owned
  // by rqt once added to the context.
  ResponsiveHubLayout * layout_ = nullptr;
  ConnectionsHubWidget * hub_ = nullptr;   // owned by layout_ via the Qt parent
  std::unique_ptr<marine_control_bridge_client::BridgeControlClient> bridge_client_;
  std::vector<marine_control_bridge_client::ControlDevice> devices_;  // GUI thread cache
  // Owns one (widget + subscription + publisher) per device/topic tab. All
  // accesses are on the GUI thread.
  std::unique_ptr<TabManager> tab_manager_;
  // The hub's marshalling devices-changed callback, kept so it can be re-registered
  // on a freshly built client when the operator switches bridges.
  std::function<void()> devices_changed_cb_;
  /// Set in shutdownPlugin() so a deferred populate queued in initPlugin
  /// (QTimer::singleShot) becomes a no-op if it fires during teardown.
  bool shutting_down_ = false;
  /// Liveness flag shared (by value) with the hub's BridgeControlHooks lambdas.
  /// Cleared in shutdownPlugin() so a hooks call marshalled to the hub after this
  /// plugin is torn down — or if the hub widget outlives the plugin — becomes a safe
  /// no-op instead of dereferencing a freed plugin. GUI-thread only, so a plain bool
  /// behind a shared_ptr (which the lambdas keep alive) suffices; no atomic needed.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace rqt_marine_control

#endif  // RQT_MARINE_CONTROL__MARINE_CONTROL_PLUGIN_HPP_
