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

#include "rqt_marine_control/connections_hub_widget.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLayoutItem>
#include <QMetaObject>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QVariant>

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <qt_gui_cpp/settings.h>  // NOLINT(build/include_order)

namespace rqt_marine_control
{

namespace
{
// Identity for a remote device: the (remote, state_topic) pair. Mirrors
// BridgeControlClient::deviceKey (same '\n' separator, not a valid ROS-name char)
// so the hub's desired set and per-tab ownership use the exact keying the bridge
// client does — two remotes offering the same state-topic name stay distinct.
std::string deviceKey(const std::string & remote, const std::string & state_topic)
{
  return remote + '\n' + state_topic;
}

// Delete every item (and its widget) from a layout so a section can be rebuilt.
// Safe here because rebuildSections never runs inside a checkbox's own signal:
// the toggle handlers marshal the rebuild through a queued onDevicesChanged, so
// no widget is deleted while its slot is on the stack.
void clearLayout(QVBoxLayout * layout)
{
  if (layout == nullptr) {
    return;
  }
  while (QLayoutItem * item = layout->takeAt(0)) {
    if (QWidget * w = item->widget()) {
      delete w;
    }
    delete item;
  }
}
}  // namespace

ConnectionsHubWidget::ConnectionsHubWidget(
  TabManager * tab_manager, BridgeControlHooks hooks, LocalTopicsProvider local_topics,
  QWidget * parent)
: QWidget(parent),
  tab_manager_(tab_manager),
  hooks_(std::move(hooks)),
  local_topics_(std::move(local_topics))
{
  buildUi();

  // Register the devices-changed callback so it marshals onto THIS widget — the
  // stable QObject that outlives every bridge rebuild and layout reparent (the hub
  // instance is never destroyed by either). A callback in flight during a rebuild
  // or reparent therefore can never target a freed object.
  if (hooks_.set_devices_changed_callback) {
    hooks_.set_devices_changed_callback(
      [this]() {QMetaObject::invokeMethod(this, "onDevicesChanged", Qt::QueuedConnection);});
  }
}

ConnectionsHubWidget::~ConnectionsHubWidget() = default;

void ConnectionsHubWidget::buildUi()
{
  auto * outer = new QVBoxLayout(this);

  auto * scroll = new QScrollArea();
  scroll->setWidgetResizable(true);
  auto * content = new QWidget();
  auto * content_layout = new QVBoxLayout(content);

  auto * local_group = new QGroupBox(tr("Local devices"));
  local_layout_ = new QVBoxLayout(local_group);
  content_layout->addWidget(local_group);

  auto * bridge_group = new QGroupBox(tr("Bridge"));
  auto * bridge_layout = new QVBoxLayout(bridge_group);
  bridge_combo_ = new QComboBox();
  bridge_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  bridge_combo_->setToolTip(tr("Select the local udp_bridge node to control remotes through"));
  bridge_layout->addWidget(bridge_combo_);
  content_layout->addWidget(bridge_group);

  auto * remote_group = new QGroupBox(tr("Remote devices"));
  remote_layout_ = new QVBoxLayout(remote_group);
  content_layout->addWidget(remote_group);

  content_layout->addStretch(1);
  scroll->setWidget(content);
  outer->addWidget(scroll);

  connect(
    bridge_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &ConnectionsHubWidget::onBridgeComboChanged);
}

void ConnectionsHubWidget::setBridgeNodesProvider(BridgeNodesProvider provider)
{
  bridge_nodes_ = std::move(provider);
}

void ConnectionsHubWidget::setBridgeSelectedCallback(BridgeSelectedCallback callback)
{
  on_bridge_selected_ = std::move(callback);
}

void ConnectionsHubWidget::setDesiredRemotes(
  const std::vector<std::pair<std::string, std::string>> & remotes)
{
  for (const auto & [remote, state_topic] : remotes) {
    desired_.insert(deviceKey(remote, state_topic));
  }
}

void ConnectionsHubWidget::refresh()
{
  rebuildBridgeCombo();
  onDevicesChanged();
}

void ConnectionsHubWidget::rebuildBridgeCombo()
{
  if (bridge_combo_ == nullptr) {
    return;
  }
  const QString selected = bridge_combo_->currentText();

  QStringList nodes;
  nodes.append("");   // the "no bridge" entry
  if (bridge_nodes_) {
    for (const auto & name : bridge_nodes_()) {
      nodes.append(QString::fromStdString(name));
    }
  }
  // Keep the currently-selected bridge in the list even if it has (transiently)
  // dropped out of discovery. Silently falling back to "no bridge" here would strand
  // a still-live client: the combo would read "no bridge" while the plugin keeps
  // controlling remotes through the old client (state/UI desync). Preserving the
  // selection keeps the combo and the live client consistent; the operator can
  // switch to "no bridge" explicitly if the bridge is truly gone.
  if (!selected.isEmpty() && !nodes.contains(selected)) {
    nodes.append(selected);
  }

  // Block signals so a plain refresh that leaves the selection unchanged does not
  // tear down and rebuild the active client (mirrors the old updateBridgeList).
  const QSignalBlocker blocker(bridge_combo_);
  bridge_combo_->clear();
  for (const auto & node : nodes) {
    bridge_combo_->addItem(node);
  }
  const int index = bridge_combo_->findText(selected);
  bridge_combo_->setCurrentIndex(index >= 0 ? index : 0);
}

void ConnectionsHubWidget::onBridgeComboChanged(int index)
{
  (void)index;
  if (on_bridge_selected_) {
    on_bridge_selected_(bridge_combo_->currentText().toStdString());
  }
}

void ConnectionsHubWidget::onDevicesChanged()
{
  std::vector<marine_control_bridge_client::ControlDevice> devices;
  if (hooks_.available_devices) {
    devices = hooks_.available_devices();
  }

  // Reconcile the desired set: any device the operator asked for that is now
  // discovered but not yet connected/open is (re)connected and opened. This is
  // what lets a device checked (or restored) before its bridge_info arrived come
  // up automatically, and re-establishes one whose bridge restarted.
  for (const auto & device : devices) {
    if (desired_.count(deviceKey(device.remote, device.state_topic)) == 0) {
      continue;
    }
    const bool connected = hooks_.is_connected &&
      hooks_.is_connected(device.remote, device.state_topic);
    if (!connected && hooks_.connect) {
      hooks_.connect(device);
    }
    if (!tab_manager_->hasTab(device.state_topic)) {
      tab_manager_->openTab(device.state_topic);
    }
    // Record which remote owns this tab so a later close disconnects the right one.
    open_remote_tabs_[device.state_topic] = device;
  }

  rebuildSections(devices);
}

void ConnectionsHubWidget::rebuildSections(
  const std::vector<marine_control_bridge_client::ControlDevice> & devices)
{
  clearLayout(local_layout_);
  clearLayout(remote_layout_);
  local_boxes_.clear();
  remote_boxes_.clear();

  // Topics already bridged in from a connected remote device — hidden from the
  // Local section so the same device is never listed twice.
  std::set<std::string> connected_remote_topics;
  for (const auto & device : devices) {
    if (hooks_.is_connected && hooks_.is_connected(device.remote, device.state_topic)) {
      connected_remote_topics.insert(device.state_topic);
    }
  }

  // Local section: one box per local ControlSet topic (minus the de-duped ones).
  std::vector<std::string> local_topics;
  if (local_topics_) {
    local_topics = local_topics_();
  }
  for (const auto & topic : local_topics) {
    if (connected_remote_topics.count(topic) != 0) {
      continue;
    }
    auto * box = new QCheckBox(QString::fromStdString(topic));
    box->setChecked(tab_manager_->hasTab(topic));   // set before connecting: no spurious toggle
    connect(
      box, &QCheckBox::toggled, this,
      [this, topic](bool checked) {onLocalToggled(topic, checked);});
    local_layout_->addWidget(box);
    local_boxes_.emplace(topic, box);
  }
  if (local_boxes_.empty()) {
    local_layout_->addWidget(new QLabel(tr("(no local control devices)")));
  }

  // Remote section: one box per discovered device, grouped by remote. Devices are
  // sorted by (remote, state topic) upstream, so a header per remote-change gives
  // stable, grouped output.
  std::string current_remote;
  for (const auto & device : devices) {
    if (device.remote != current_remote) {
      current_remote = device.remote;
      auto * header = new QLabel(QStringLiteral("<b>%1</b>").arg(
          QString::fromStdString(current_remote)));
      remote_layout_->addWidget(header);
    }
    auto * box = new QCheckBox(QString::fromStdString(device.state_topic));
    const bool connected = hooks_.is_connected &&
      hooks_.is_connected(device.remote, device.state_topic);
    box->setChecked(
      connected || desired_.count(deviceKey(device.remote, device.state_topic)) != 0);
    connect(
      box, &QCheckBox::toggled, this,
      [this, device](bool checked) {onRemoteToggled(device, checked);});
    remote_layout_->addWidget(box);
    remote_boxes_.push_back({box, device});
  }
  if (remote_boxes_.empty()) {
    remote_layout_->addWidget(new QLabel(tr("(no remote devices discovered)")));
  }
}

void ConnectionsHubWidget::onLocalToggled(const std::string & state_topic, bool checked)
{
  if (checked) {
    tab_manager_->openTab(state_topic);
  } else {
    tab_manager_->closeTab(state_topic);
  }
}

void ConnectionsHubWidget::onRemoteToggled(
  const marine_control_bridge_client::ControlDevice & device, bool checked)
{
  const std::string key = deviceKey(device.remote, device.state_topic);
  if (checked) {
    desired_.insert(key);
    if (hooks_.connect) {
      hooks_.connect(device);
    }
    tab_manager_->openTab(device.state_topic);
    open_remote_tabs_[device.state_topic] = device;
  } else {
    desired_.erase(key);
    if (hooks_.disconnect) {
      hooks_.disconnect(device);
    }
    tab_manager_->closeTab(device.state_topic);
    open_remote_tabs_.erase(device.state_topic);
  }
  // Refresh de-dup and checkbox state after the connection change. Queued so the
  // rebuild (which deletes and recreates these boxes) never runs while this box's
  // toggled slot is still on the stack.
  QMetaObject::invokeMethod(this, "onDevicesChanged", Qt::QueuedConnection);
}

void ConnectionsHubWidget::onTabClosed(const std::string & state_topic)
{
  // Resolve the remote device this tab belongs to from the authoritative open-tab
  // map — NOT by first-match on the state topic, which would disconnect the wrong
  // remote when several remotes share a state-topic name. A topic with no entry is
  // a local-only tab (nothing to disconnect over the bridge).
  const auto it = open_remote_tabs_.find(state_topic);
  const bool is_remote = it != open_remote_tabs_.end();
  marine_control_bridge_client::ControlDevice device;
  if (is_remote) {
    device = it->second;
  }

  // A closed tab means the operator no longer wants this device. Drop it from the
  // desired set (so reconcile does not reopen it) and, if it is a remote device,
  // disconnect that exact remote over the bridge — immediately, no confirm dialog.
  if (is_remote) {
    desired_.erase(deviceKey(device.remote, device.state_topic));
    if (hooks_.disconnect) {
      hooks_.disconnect(device);
    }
    open_remote_tabs_.erase(it);
  }

  // Uncheck the matching box without re-entering its toggle handler (that would
  // close the tab a second time / double-disconnect). For a remote match, uncheck
  // only the box for THIS remote, not every box that shares the state topic.
  if (auto lit = local_boxes_.find(state_topic); lit != local_boxes_.end()) {
    const QSignalBlocker blocker(lit->second);
    lit->second->setChecked(false);
  }
  if (is_remote) {
    for (auto & remote : remote_boxes_) {
      if (remote.device.remote == device.remote &&
        remote.device.state_topic == device.state_topic)
      {
        const QSignalBlocker blocker(remote.box);
        remote.box->setChecked(false);
      }
    }
  }

  tab_manager_->closeTab(state_topic);
  // Reflect the disconnect (a de-duped local box may reappear). Queued for the
  // same reason as onRemoteToggled.
  QMetaObject::invokeMethod(this, "onDevicesChanged", Qt::QueuedConnection);
}

void ConnectionsHubWidget::onTabCloseRequested(int index)
{
  const std::string state_topic = tab_manager_->topicForIndex(index);
  if (state_topic.empty()) {
    return;   // the hub's own tab (when docked as tab 0) or an unknown widget
  }
  onTabClosed(state_topic);
}

void ConnectionsHubWidget::saveSettings(qt_gui_cpp::Settings & settings) const
{
  QStringList locals;
  for (const auto & [topic, box] : local_boxes_) {
    if (box->isChecked()) {
      locals.append(QString::fromStdString(topic));
    }
  }
  // Persist the desired remotes as index-aligned (node, topic) lists so the
  // (remote, state_topic) identity round-trips — a bare topic list could not tell
  // two same-named topics on different remotes apart on restore.
  QStringList remote_nodes;
  QStringList remote_topics;
  for (const auto & key : desired_) {
    const auto nl = key.find('\n');
    if (nl == std::string::npos) {
      continue;
    }
    remote_nodes.append(QString::fromStdString(key.substr(0, nl)));
    remote_topics.append(QString::fromStdString(key.substr(nl + 1)));
  }
  settings.setValue("hub_local_topics", locals);
  settings.setValue("hub_desired_remote_nodes", remote_nodes);
  settings.setValue("hub_desired_remote_topics", remote_topics);
  if (bridge_combo_ != nullptr) {
    settings.setValue("hub_bridge_node", bridge_combo_->currentText());
  }
}

void ConnectionsHubWidget::restoreSettings(const qt_gui_cpp::Settings & settings)
{
  const QStringList remote_nodes = settings.value("hub_desired_remote_nodes").toStringList();
  const QStringList remote_topics = settings.value("hub_desired_remote_topics").toStringList();
  std::vector<std::pair<std::string, std::string>> desired;
  for (int i = 0; i < remote_nodes.size() && i < remote_topics.size(); ++i) {
    desired.emplace_back(remote_nodes.at(i).toStdString(), remote_topics.at(i).toStdString());
  }
  setDesiredRemotes(desired);

  const QStringList locals = settings.value("hub_local_topics").toStringList();
  for (const auto & topic : locals) {
    tab_manager_->openTab(topic.toStdString());
  }

  // Reselect the bridge; the change fires onBridgeComboChanged, which asks the
  // owner to rebuild the client. The deferred refresh() (queued by the owner off
  // the load path) then reconciles the restored desired set once devices appear.
  // Selecting a not-yet-discovered bridge here is deliberate — discovery has not run
  // at restore time, so the persisted selection is trusted and reconciled by that
  // refresh; it is the same persist-the-selection policy rebuildBridgeCombo applies
  // to a vanished bridge, not a stray phantom (a client for an absent bridge simply
  // discovers nothing until it appears).
  const QString bridge = settings.value("hub_bridge_node", "").toString();
  if (bridge_combo_ != nullptr && !bridge.isEmpty()) {
    int index = bridge_combo_->findText(bridge);
    if (index < 0) {
      bridge_combo_->addItem(bridge);
      index = bridge_combo_->findText(bridge);
    }
    bridge_combo_->setCurrentIndex(index);
  }
}

}  // namespace rqt_marine_control
