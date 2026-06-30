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

#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QList>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <pluginlib/class_list_macros.hpp>

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
  widget_ = new QWidget();
  auto * layout = new QVBoxLayout(widget_);

  auto * toolbar = new QHBoxLayout();
  toolbar->addWidget(new QLabel(tr("Control topic:")));
  topic_combo_ = new QComboBox();
  topic_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolbar->addWidget(topic_combo_, 1);
  auto * refresh = new QPushButton();
  refresh->setIcon(QIcon::fromTheme("view-refresh"));
  refresh->setToolTip(tr("Refresh the list of control topics"));
  toolbar->addWidget(refresh);
  layout->addLayout(toolbar);

  // Dynamic-bridge row (ADR-0003 D7-dyn): pick a local udp_bridge, see the
  // devices discovered on its remotes, and connect/disconnect them explicitly.
  auto * bridge_bar = new QHBoxLayout();
  bridge_bar->addWidget(new QLabel(tr("Bridge:")));
  bridge_combo_ = new QComboBox();
  bridge_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  bridge_bar->addWidget(bridge_combo_, 1);
  auto * bridge_refresh = new QPushButton();
  bridge_refresh->setIcon(QIcon::fromTheme("view-refresh"));
  bridge_refresh->setToolTip(tr("Refresh the list of udp_bridge nodes"));
  bridge_bar->addWidget(bridge_refresh);
  bridge_bar->addWidget(new QLabel(tr("Device:")));
  device_combo_ = new QComboBox();
  device_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  bridge_bar->addWidget(device_combo_, 1);
  connect_button_ = new QPushButton(tr("Connect"));
  // Momentary push button (not checkable): it must visibly release on click. The
  // connected/disconnected state is carried by the button text and the status
  // label, not by a sunken "checked" look (which read as a stuck/pressed button).
  connect_button_->setToolTip(tr("Connect/disconnect the selected device over the bridge"));
  bridge_bar->addWidget(connect_button_);
  // Connection-status indicator: starts "Disconnected" and is driven from the
  // client's actual connection state in onDeviceChanged (#78 acceptance: surface
  // state as an indicator, never a frozen GUI).
  status_label_ = new QLabel(tr("Disconnected"));
  bridge_bar->addWidget(status_label_);
  layout->addLayout(bridge_bar);

  // One closable tab per connected device / selected topic. With zero tabs the
  // widget simply shows an empty tab bar (no crash, no placeholder needed).
  tab_widget_ = new QTabWidget();
  tab_widget_->setTabsClosable(true);
  tab_widget_->setMovable(true);
  layout->addWidget(tab_widget_, 1);
  tab_manager_ = std::make_unique<TabManager>(tab_widget_, makeTransportFactory());
  connect(
    tab_widget_, &QTabWidget::tabCloseRequested,
    this, &MarineControlPlugin::onTabCloseRequested);

  widget_->setWindowTitle(
    QStringLiteral("Marine Control (") + QString::number(context.serialNumber()) + ")");
  context.addWidget(widget_);

  connect(refresh, &QPushButton::clicked, this, &MarineControlPlugin::updateTopicList);

  // Defer the initial DDS-graph queries off the plugin-load path. Both
  // updateTopicList() and updateBridgeList() call get_*_names_and_types(), which
  // can stall under a degraded/mid-discovery link; running them synchronously in
  // initPlugin froze the whole rqt instance (#78). QTimer::singleShot(0) runs
  // them on the GUI thread once the event loop starts, so initPlugin returns
  // immediately and the GUI comes up responsive (showing "Disconnected").
  QTimer::singleShot(0, this, &MarineControlPlugin::updateTopicList);
  connect(
    topic_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &MarineControlPlugin::onTopicChanged);

  // The bridge-list populate is likewise deferred. updateBridgeList() already
  // blocks bridge_combo_'s signals while repopulating, so it won't spuriously
  // build a client when it runs after onBridgeChanged is connected below.
  connect(bridge_refresh, &QPushButton::clicked, this, &MarineControlPlugin::updateBridgeList);
  connect(connect_button_, &QPushButton::clicked, this, &MarineControlPlugin::onConnectClicked);
  QTimer::singleShot(0, this, &MarineControlPlugin::updateBridgeList);
  connect(
    bridge_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &MarineControlPlugin::onBridgeChanged);
  connect(
    device_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &MarineControlPlugin::onDeviceChanged);

  const QList<QString> & argv = context.argv();
  if (!argv.empty()) {
    arg_topic_ = argv[0];
    selectTopic(arg_topic_);
  }
}

void MarineControlPlugin::shutdownPlugin()
{
  // Suppress any deferred populate still queued from initPlugin. The receiver-
  // context QTimer::singleShot auto-cancels when `this` is destroyed, but rqt
  // calls shutdownPlugin() before destruction — this flag closes that window so
  // a late updateTopicList()/updateBridgeList() can't run mid-teardown (#78).
  shutting_down_ = true;
  bridge_client_.reset();
  // Tears down every tab's subscription/publisher (no leaked subs).
  if (tab_manager_) {
    tab_manager_->clear();
  }
}

void MarineControlPlugin::saveSettings(
  qt_gui_cpp::Settings & plugin_settings,
  qt_gui_cpp::Settings & instance_settings) const
{
  (void)plugin_settings;
  // Persist only the ACTIVE tab's topic (operator decision for #92): a minimal
  // extension of the prior single-"topic" persistence. Bridge tabs are transient
  // and reopen on the next bridge connect, so full multi-tab restore is
  // intentionally out of scope.
  QString active;
  if (tab_widget_ && tab_manager_) {
    active = QString::fromStdString(tab_manager_->topicForIndex(tab_widget_->currentIndex()));
  }
  instance_settings.setValue("topic", active);
}

void MarineControlPlugin::restoreSettings(
  const qt_gui_cpp::Settings & plugin_settings,
  const qt_gui_cpp::Settings & instance_settings)
{
  (void)plugin_settings;
  const QString topic = instance_settings.value("topic", "").toString();
  if (!arg_topic_.isEmpty()) {
    arg_topic_ = "";   // don't override a topic passed on the command line
  } else {
    selectTopic(topic);
  }
}

void MarineControlPlugin::updateTopicList()
{
  if (shutting_down_) {
    return;
  }
  const QString selected = topic_combo_->currentText();

  QList<QString> topics;
  topics.append("");   // the "no topic" entry
  if (node_) {
    for (const auto & name : control_set_topics(node_->get_topic_names_and_types())) {
      topics.append(QString::fromStdString(name));
    }
  }

  // Block signals while repopulating so the clear()/addItem churn doesn't emit
  // currentIndexChanged -> onTopicChanged, which would tear down and rebuild the
  // active subscription on every refresh. selectTopic() preserves the prior
  // selection, so an unchanged topic keeps its subscription. (Mirrors the
  // QSignalBlocker pattern in updateBridgeList(); load-bearing now that the
  // initial populate is deferred to run after onTopicChanged is connected, #78.)
  const QSignalBlocker blocker(topic_combo_);
  topic_combo_->clear();
  for (const auto & topic : topics) {
    topic_combo_->addItem(topic);
  }
  selectTopic(selected);
}

void MarineControlPlugin::selectTopic(const QString & topic)
{
  int index = topic_combo_->findText(topic);
  if (index == -1) {
    topic_combo_->addItem(topic);
    index = topic_combo_->findText(topic);
  }
  topic_combo_->setCurrentIndex(index);
}

void MarineControlPlugin::onTopicChanged(int index)
{
  const QString topic = topic_combo_->itemText(index);
  const std::string state_topic = topic.toStdString();

  // The manual selector owns at most one tab. If it moved off its previous
  // topic, close that tab — unless it is also a connected bridge device, whose
  // tab outlives the combo selection.
  if (!manual_topic_.empty() && manual_topic_ != state_topic) {
    if (connectedDeviceForTopic(manual_topic_) == nullptr) {
      tab_manager_->closeTab(manual_topic_);
    }
    manual_topic_.clear();
  }

  if (topic.isEmpty() || !node_) {
    return;
  }

  // Opens a new tab, or focuses the existing one if the topic already has a tab
  // (e.g. a bridge device the operator also selected manually).
  tab_manager_->openTab(state_topic);
  manual_topic_ = state_topic;
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

const marine_control_bridge_client::ControlDevice *
MarineControlPlugin::connectedDeviceForTopic(const std::string & state_topic) const
{
  if (!bridge_client_) {
    return nullptr;
  }
  for (const auto & device : devices_) {
    if (device.state_topic == state_topic &&
      bridge_client_->isConnected(device.remote, device.state_topic))
    {
      return &device;
    }
  }
  return nullptr;
}

void MarineControlPlugin::updateBridgeList()
{
  if (shutting_down_) {
    return;
  }
  const QString selected = bridge_combo_->currentText();

  QList<QString> bridges;
  bridges.append("");   // the "no bridge" entry
  if (node_) {
    for (const auto & name : bridge_nodes_from_services(node_->get_service_names_and_types())) {
      bridges.append(QString::fromStdString(name));
    }
  }

  // Block signals so repopulating doesn't tear down an active client when the
  // selection is unchanged (a plain refresh shouldn't disconnect devices).
  const QSignalBlocker blocker(bridge_combo_);
  bridge_combo_->clear();
  for (const auto & bridge : bridges) {
    bridge_combo_->addItem(bridge);
  }
  const int index = bridge_combo_->findText(selected);
  bridge_combo_->setCurrentIndex(index >= 0 ? index : 0);
}

void MarineControlPlugin::onBridgeChanged(int index)
{
  (void)index;
  bridge_client_.reset();
  devices_.clear();
  {
    const QSignalBlocker blocker(device_combo_);
    device_combo_->clear();
  }
  onDeviceChanged(-1);

  const QString bridge = bridge_combo_->currentText();
  if (bridge.isEmpty() || !node_) {
    return;
  }
  bridge_client_ = std::make_unique<marine_control_bridge_client::BridgeControlClient>(
    node_.get(), bridge.toStdString());
  // The client's changed-callback fires on the executor thread; marshal the
  // device-list refresh onto the GUI thread.
  bridge_client_->setDevicesChangedCallback(
    [this]() {QMetaObject::invokeMethod(this, "refreshDevices", Qt::QueuedConnection);});
  refreshDevices();
}

void MarineControlPlugin::refreshDevices()
{
  if (!bridge_client_) {
    return;
  }
  const QString selected = device_combo_->currentText();
  devices_ = bridge_client_->availableDevices();
  {
    const QSignalBlocker blocker(device_combo_);
    device_combo_->clear();
    for (const auto & device : devices_) {
      device_combo_->addItem(
        QString::fromStdString(device.remote) + ": " +
        QString::fromStdString(device.state_topic));
    }
    const int index = device_combo_->findText(selected);
    device_combo_->setCurrentIndex(index >= 0 ? index : (devices_.empty() ? -1 : 0));
  }
  onDeviceChanged(device_combo_->currentIndex());
}

void MarineControlPlugin::onDeviceChanged(int index)
{
  const bool valid = bridge_client_ && index >= 0 &&
    index < static_cast<int>(devices_.size());
  connect_button_->setEnabled(valid);

  bool connected = false;
  if (valid) {
    const auto & device = devices_[index];
    connected = bridge_client_->isConnected(device.remote, device.state_topic);
  }
  connect_button_->setText(connected ? tr("Disconnect") : tr("Connect"));
  // Single source of truth for the status indicator: driven from the client's
  // actual connection state, and refreshed here on every device/bridge switch
  // and after connect/disconnect, so it never goes stale (#78).
  if (status_label_) {
    status_label_->setText(connected ? tr("Connected") : tr("Disconnected"));
  }
}

void MarineControlPlugin::onConnectClicked()
{
  const int index = device_combo_->currentIndex();
  if (!bridge_client_ || index < 0 || index >= static_cast<int>(devices_.size())) {
    return;
  }
  const auto device = devices_[index];
  // Toggle on the actual connection state rather than a checkable-button state,
  // since the button is now momentary.
  if (!bridge_client_->isConnected(device.remote, device.state_topic)) {
    bridge_client_->connect(device);
    // Open the device's tab; its state topic appears locally once the bridge
    // wires it (subscribing before it exists is fine — it waits for the
    // publisher).
    tab_manager_->openTab(device.state_topic);
  } else {
    bridge_client_->disconnect(device);
    tab_manager_->closeTab(device.state_topic);
  }
  // Re-sync the button text and status label through the single source of
  // truth. Note isConnected() reflects operator intent (the connection was
  // requested) — connect()/disconnect() set the client's connected_ set
  // synchronously, ahead of the fire-and-forget bridge service call — not
  // bridge-confirmed delivery; confirmed state would require established_.
  onDeviceChanged(index);
}

void MarineControlPlugin::onTabCloseRequested(int index)
{
  const std::string state_topic = tab_manager_->topicForIndex(index);
  if (state_topic.empty()) {
    return;
  }

  // Operator decision (#92): a tab represents device presence, so closing a
  // connected bridge device's tab disconnects the device over the bridge.
  const marine_control_bridge_client::ControlDevice * device =
    connectedDeviceForTopic(state_topic);
  if (device != nullptr) {
    bridge_client_->disconnect(*device);
  }

  // If this was the manual tab, clear the combo's selection bookkeeping without
  // re-triggering onTopicChanged (which would otherwise try to close it again).
  if (manual_topic_ == state_topic) {
    manual_topic_.clear();
    const QSignalBlocker blocker(topic_combo_);
    const int empty_index = topic_combo_->findText("");
    topic_combo_->setCurrentIndex(empty_index >= 0 ? empty_index : -1);
  }

  tab_manager_->closeTab(state_topic);

  // Refresh the connect button / status indicator if a device's state changed.
  if (device != nullptr) {
    onDeviceChanged(device_combo_->currentIndex());
  }
}

}  // namespace rqt_marine_control

PLUGINLIB_EXPORT_CLASS(rqt_marine_control::MarineControlPlugin, rqt_gui_cpp::Plugin)
