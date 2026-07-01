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

#include "rqt_marine_control/marine_control_plugin.hpp"

#include <QList>
#include <QMetaObject>
#include <QString>
#include <QTabWidget>
#include <QTimer>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <marine_control_interfaces/msg/control_set.hpp>
#include <marine_control_interfaces/msg/control_value.hpp>
#include <pluginlib/class_list_macros.hpp>

#include "rqt_marine_control/responsive_hub_layout.hpp"
#include "rqt_marine_control/topic_filter.hpp"

namespace rqt_marine_control
{

namespace
{
// The rclcpp-backed transport for one tab: owns the state subscription and the
// change publisher for a single device topic. Constructed on the GUI thread.
//
// Dangle safety: the subscription callback captures only self-contained copies
// (the delivery std::function and a stable QObject* marshalling target), never
// this transport, and the queued GUI-thread lambda captures only a message
// snapshot + that std::function. So tearing this transport down (tab close)
// while a callback is in flight or a delivery is queued can never touch a freed
// object — mirrors the #78-proven marshal-to-a-stable-QObject pattern.
class RclcppTabTransport : public TabTransport
{
public:
  RclcppTabTransport(
    rclcpp::Node * node, QObject * gui_target, const std::string & state_topic,
    std::function<void(const marine_control_interfaces::msg::ControlSet &)> on_set)
  : node_(node)
  {
    if (node_ == nullptr) {
      return;
    }
    auto deliver = std::move(on_set);
    // State QoS RELIABLE + VOLATILE (ADR-0003 D5) to match the device library's
    // publisher; depth 10 absorbs a heartbeat burst.
    state_sub_ = node_->create_subscription<marine_control_interfaces::msg::ControlSet>(
      state_topic, rclcpp::QoS(10),
      [deliver, gui_target](marine_control_interfaces::msg::ControlSet::ConstSharedPtr msg) {
        // Render on the GUI thread. Capture only the snapshot + the delivery fn.
        // No QPointer guard on gui_target is needed: Qt flushes/discards any
        // queued events targeting a QObject when it is destroyed, so a delivery
        // queued just before the plugin QObject dies is dropped, never invoked
        // on freed memory. And if the plugin outlives the tab, deliver ->
        // TabManager::applySet looks the tab up by topic and is a no-op when the
        // tab has already closed — so a late message for a closed tab is
        // harmlessly discarded.
        QMetaObject::invokeMethod(
          gui_target, [deliver, msg]() {deliver(*msg);}, Qt::QueuedConnection);
      });
    change_pub_ = node_->create_publisher<marine_control_interfaces::msg::ControlValue>(
      derive_change_topic(state_topic), rclcpp::QoS(10));
  }

  void publishChange(const std::string & name, const std::string & value) override
  {
    if (!change_pub_ || node_ == nullptr) {
      return;
    }
    marine_control_interfaces::msg::ControlValue command;
    command.header.stamp = node_->now();
    command.name = name;
    command.value = value;
    change_pub_->publish(command);
  }

private:
  // Non-owning. The plugin owns the rclcpp::Node and destroys every tab/transport
  // (TabManager::clear in shutdownPlugin, or closeTab) before the node is torn
  // down, so this raw pointer — used by publishChange and the subscription — is
  // never dereferenced after the node dies. Node-outlives-transport is the
  // invariant; a weak/shared_ptr would also work but is unnecessary given it.
  rclcpp::Node * node_;
  rclcpp::Subscription<marine_control_interfaces::msg::ControlSet>::SharedPtr state_sub_;
  rclcpp::Publisher<marine_control_interfaces::msg::ControlValue>::SharedPtr change_pub_;
};
}  // namespace

MarineControlPlugin::MarineControlPlugin()
: rqt_gui_cpp::Plugin()
{
  setObjectName("MarineControl");
}

void MarineControlPlugin::initPlugin(qt_gui_cpp::PluginContext & context)
{
  // The responsive layout owns the device tab widget; the TabManager drives it.
  layout_ = new ResponsiveHubLayout();
  tab_manager_ = std::make_unique<TabManager>(layout_->tabWidget(), makeTransportFactory());
  layout_->setTabManager(tab_manager_.get());

  // The hub depends only on the injected BridgeControlHooks (over the real client)
  // and on node-graph providers for its Local and Bridge sections.
  hub_ = new ConnectionsHubWidget(
    tab_manager_.get(), makeBridgeHooks(),
    [this]() {return localControlTopics();});
  hub_->setBridgeNodesProvider([this]() {return bridgeNodes();});
  hub_->setBridgeSelectedCallback([this](const std::string & bridge) {onBridgeSelected(bridge);});
  layout_->setHub(hub_);

  // Closing a device tab routes through the hub so it unchecks the box and, for a
  // remote device, disconnects it over the bridge (operator decision #97:
  // immediate, no confirm dialog).
  connect(
    layout_->tabWidget(), &QTabWidget::tabCloseRequested,
    hub_, &ConnectionsHubWidget::onTabCloseRequested);

  layout_->setWindowTitle(
    QStringLiteral("Marine Control (") + QString::number(context.serialNumber()) + ")");
  context.addWidget(layout_);

  // Defer the initial DDS-graph queries off the plugin-load path. The hub's
  // providers call get_*_names_and_types(), which can stall under a degraded/
  // mid-discovery link; running them synchronously in initPlugin froze the whole
  // rqt instance (#78). QTimer::singleShot(0) runs the populate on the GUI thread
  // once the event loop starts, so initPlugin returns immediately.
  QTimer::singleShot(0, hub_, &ConnectionsHubWidget::refresh);

  // A topic passed on the command line opens directly as a tab; the hub checks its
  // box on the next refresh if it is a known local topic.
  const QList<QString> & argv = context.argv();
  if (!argv.empty() && !argv[0].isEmpty()) {
    tab_manager_->openTab(argv[0].toStdString());
  }
}

void MarineControlPlugin::shutdownPlugin()
{
  // Suppress any deferred populate still queued from initPlugin. The receiver-
  // context QTimer::singleShot auto-cancels when the hub is destroyed, but rqt
  // calls shutdownPlugin() first — this flag makes a late localControlTopics()/
  // bridgeNodes() return empty rather than query a torn-down node (#78).
  shutting_down_ = true;
  // Neutralize the hub's hooks lambdas: any devices-changed marshalled to the hub
  // after this point (or if the hub widget outlives this plugin) sees a dead flag
  // and no-ops instead of dereferencing this half-torn-down plugin.
  if (alive_) {
    *alive_ = false;
  }
  // Stop routing tab-close signals into the hub, and drop the layout's borrowed
  // TabManager pointer, before the TabManager is destroyed below — so neither a
  // late tabCloseRequested nor a settling resize can dereference a freed manager.
  if (layout_ != nullptr && hub_ != nullptr) {
    disconnect(
      layout_->tabWidget(), &QTabWidget::tabCloseRequested,
      hub_, &ConnectionsHubWidget::onTabCloseRequested);
  }
  if (layout_ != nullptr) {
    layout_->setTabManager(nullptr);
  }
  bridge_client_.reset();
  // Tear down every tab's subscription/publisher (no leaked subs), then destroy the
  // TabManager here in a controlled order — while the borrowed node still exists —
  // rather than at plugin destruction, when ordering against the base node is less
  // certain. Post-teardown hub callbacks are already inert (guards above), so the
  // hub's borrowed TabManager pointer is not dereferenced after this reset.
  if (tab_manager_) {
    tab_manager_->clear();
    tab_manager_.reset();
  }
}

void MarineControlPlugin::saveSettings(
  qt_gui_cpp::Settings & plugin_settings,
  qt_gui_cpp::Settings & instance_settings) const
{
  (void)plugin_settings;
  // The hub persists its checked local topics + desired remotes + selected bridge;
  // the layout persists its splitter divider. Bridge tabs reopen via the desired-
  // set reconcile once the restored bridge rediscovers them.
  if (hub_ != nullptr) {
    hub_->saveSettings(instance_settings);
  }
  if (layout_ != nullptr) {
    layout_->saveSettings(instance_settings);
  }
}

void MarineControlPlugin::restoreSettings(
  const qt_gui_cpp::Settings & plugin_settings,
  const qt_gui_cpp::Settings & instance_settings)
{
  (void)plugin_settings;
  if (hub_ != nullptr) {
    hub_->restoreSettings(instance_settings);
  }
  if (layout_ != nullptr) {
    layout_->restoreSettings(instance_settings);
  }
}

TabTransportFactory MarineControlPlugin::makeTransportFactory()
{
  return [this](
    const std::string & state_topic,
    std::function<void(const marine_control_interfaces::msg::ControlSet &)> on_set)
         -> std::shared_ptr<TabTransport> {
           // `this` is the stable GUI-thread QObject the transport marshals onto.
           return std::make_shared<RclcppTabTransport>(
             node_.get(), this, state_topic, std::move(on_set));
         };
}

BridgeControlHooks MarineControlPlugin::makeBridgeHooks()
{
  BridgeControlHooks hooks;
  // Each lambda holds a copy of the shared liveness flag so it can guard against
  // this plugin having been torn down (shutdownPlugin) or freed before the hub
  // widget: a dead flag short-circuits to a safe default rather than touching
  // freed plugin state. The flag outlives the plugin because the hub keeps a copy.
  auto alive = alive_;
  // availableDevices() reflects the CURRENT client, and caches into devices_ so the
  // plugin keeps a GUI-thread snapshot of what the hub last saw.
  hooks.available_devices = [this, alive]() {
      if (!*alive) {
        return std::vector<marine_control_bridge_client::ControlDevice>{};
      }
      devices_ = bridge_client_ ? bridge_client_->availableDevices() :
        std::vector<marine_control_bridge_client::ControlDevice>{};
      return devices_;
    };
  hooks.connect = [this, alive](const marine_control_bridge_client::ControlDevice & device) {
      if (*alive && bridge_client_) {
        bridge_client_->connect(device);
      }
    };
  hooks.disconnect = [this, alive](const marine_control_bridge_client::ControlDevice & device) {
      if (*alive && bridge_client_) {
        bridge_client_->disconnect(device);
      }
    };
  hooks.is_connected =
    [this, alive](const std::string & remote, const std::string & state_topic) {
      return *alive && bridge_client_ && bridge_client_->isConnected(remote, state_topic);
    };
  // Store the hub's marshalling callback and register it on the current client;
  // onBridgeSelected re-registers it on a freshly built one.
  hooks.set_devices_changed_callback = [this, alive](std::function<void()> callback) {
      if (!*alive) {
        return;
      }
      devices_changed_cb_ = std::move(callback);
      if (bridge_client_) {
        bridge_client_->setDevicesChangedCallback(devices_changed_cb_);
      }
    };
  return hooks;
}

std::vector<std::string> MarineControlPlugin::localControlTopics()
{
  if (shutting_down_ || !node_) {
    return {};
  }
  return control_set_topics(node_->get_topic_names_and_types());
}

std::vector<std::string> MarineControlPlugin::bridgeNodes()
{
  if (shutting_down_ || !node_) {
    return {};
  }
  return bridge_nodes_from_services(node_->get_service_names_and_types());
}

void MarineControlPlugin::onBridgeSelected(const std::string & bridge_node)
{
  // Tear the old client down and build one for the newly selected bridge. The hub
  // instance is unchanged, so its marshalling callback (kept in devices_changed_cb_)
  // is simply re-registered on the fresh client.
  bridge_client_.reset();
  devices_.clear();
  if (!bridge_node.empty() && node_) {
    bridge_client_ = std::make_unique<marine_control_bridge_client::BridgeControlClient>(
      node_.get(), bridge_node);
    if (devices_changed_cb_) {
      bridge_client_->setDevicesChangedCallback(devices_changed_cb_);
    }
  }
  // Reflect the new client's device list (or the cleared one) in the hub now.
  if (hub_ != nullptr) {
    hub_->onDevicesChanged();
  }
}

}  // namespace rqt_marine_control

PLUGINLIB_EXPORT_CLASS(rqt_marine_control::MarineControlPlugin, rqt_gui_cpp::Plugin)
