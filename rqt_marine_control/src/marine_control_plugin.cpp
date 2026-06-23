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
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <pluginlib/class_list_macros.hpp>

#include "rqt_marine_control/topic_filter.hpp"

namespace rqt_marine_control
{

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
  connect_button_->setCheckable(true);
  connect_button_->setToolTip(tr("Connect/disconnect the selected device over the bridge"));
  bridge_bar->addWidget(connect_button_);
  // Connection-status indicator: starts "Disconnected" and is driven from the
  // client's actual connection state in onDeviceChanged (#78 acceptance: surface
  // state as an indicator, never a frozen GUI).
  status_label_ = new QLabel(tr("Disconnected"));
  bridge_bar->addWidget(status_label_);
  layout->addLayout(bridge_bar);

  // The dynamic control panel lives in a scroll area so a device with many
  // controls stays usable in a small dock.
  auto * scroll = new QScrollArea();
  scroll->setWidgetResizable(true);
  control_widget_ = new marine_control_widgets::ControlSetWidget();
  scroll->setWidget(control_widget_);
  layout->addWidget(scroll, 1);

  widget_->setWindowTitle(
    QStringLiteral("Marine Control (") + QString::number(context.serialNumber()) + ")");
  context.addWidget(widget_);

  connect(refresh, &QPushButton::clicked, this, &MarineControlPlugin::updateTopicList);
  connect(
    control_widget_.data(), &marine_control_widgets::ControlSetWidget::controlChanged,
    this, &MarineControlPlugin::publishChange);

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
  state_sub_.reset();
  change_pub_.reset();
}

void MarineControlPlugin::saveSettings(
  qt_gui_cpp::Settings & plugin_settings,
  qt_gui_cpp::Settings & instance_settings) const
{
  (void)plugin_settings;
  instance_settings.setValue("topic", topic_combo_->currentText());
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
  state_sub_.reset();
  change_pub_.reset();
  if (control_widget_) {
    control_widget_->clear();
  }

  const QString topic = topic_combo_->itemText(index);
  if (topic.isEmpty() || !node_) {
    return;
  }

  const std::string state_topic = topic.toStdString();
  // State QoS RELIABLE + VOLATILE (ADR-0003 D5) to match the device library's
  // publisher; depth 10 absorbs a heartbeat burst.
  state_sub_ = node_->create_subscription<marine_control_interfaces::msg::ControlSet>(
    state_topic, rclcpp::QoS(10),
    [this](marine_control_interfaces::msg::ControlSet::ConstSharedPtr msg) {
      controlSetCallback(msg);
    });
  change_pub_ = node_->create_publisher<marine_control_interfaces::msg::ControlValue>(
    derive_change_topic(state_topic), rclcpp::QoS(10));
}

void MarineControlPlugin::controlSetCallback(
  marine_control_interfaces::msg::ControlSet::ConstSharedPtr message)
{
  {
    std::lock_guard<std::mutex> lock(latest_mutex_);
    latest_ = *message;
    have_latest_ = true;
  }
  // Render on the GUI thread; coalesces to the most recent set.
  QMetaObject::invokeMethod(this, "applyLatest", Qt::QueuedConnection);
}

void MarineControlPlugin::applyLatest()
{
  if (!control_widget_) {
    return;
  }
  marine_control_interfaces::msg::ControlSet set;
  {
    std::lock_guard<std::mutex> lock(latest_mutex_);
    if (!have_latest_) {
      return;
    }
    set = latest_;
  }
  control_widget_->apply(set);
}

void MarineControlPlugin::publishChange(const QString & name, const QString & value)
{
  if (!change_pub_ || !node_) {
    return;
  }
  marine_control_interfaces::msg::ControlValue command;
  command.header.stamp = node_->now();
  command.name = name.toStdString();
  command.value = value.toStdString();
  change_pub_->publish(command);
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
  const QSignalBlocker blocker(connect_button_);
  connect_button_->setChecked(connected);
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
  if (connect_button_->isChecked()) {
    bridge_client_->connect(device);
    // Render the device; its state topic appears locally once the bridge wires
    // it (subscribing before it exists is fine — it waits for the publisher).
    selectTopic(QString::fromStdString(device.state_topic));
  } else {
    bridge_client_->disconnect(device);
    selectTopic("");   // clear the panel
  }
  // Re-sync the button text and status label through the single source of
  // truth. Note isConnected() reflects operator intent (the connection was
  // requested) — connect()/disconnect() set the client's connected_ set
  // synchronously, ahead of the fire-and-forget bridge service call — not
  // bridge-confirmed delivery; confirmed state would require established_.
  onDeviceChanged(index);
}

}  // namespace rqt_marine_control

PLUGINLIB_EXPORT_CLASS(rqt_marine_control::MarineControlPlugin, rqt_gui_cpp::Plugin)
