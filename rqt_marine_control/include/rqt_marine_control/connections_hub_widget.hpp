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

#ifndef RQT_MARINE_CONTROL__CONNECTIONS_HUB_WIDGET_HPP_
#define RQT_MARINE_CONTROL__CONNECTIONS_HUB_WIDGET_HPP_

#include <QWidget>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "marine_control_bridge_client/bridge_control_discovery.hpp"
#include "rqt_marine_control/tab_manager.hpp"

class QCheckBox;
class QComboBox;
class QVBoxLayout;

namespace qt_gui_cpp
{
class Settings;
}  // namespace qt_gui_cpp

namespace rqt_marine_control
{

/// Injected functor seam standing in for a live BridgeControlClient. The concrete
/// client is non-virtual and rclcpp-node-bound (it cannot be faked directly), so
/// the hub depends only on these std::functions: the plugin wires them to the
/// real client, tests wire them to in-memory fakes. Mirrors the TabTransportFactory
/// injection TabManager already uses. `set_devices_changed_callback` stores the
/// hub's marshalling callback so it survives — and is re-registered on — a bridge
/// rebuild; `available_devices`/`is_connected` read the plugin's current client,
/// so the plugin never re-hands a fresh struct when the bridge changes.
struct BridgeControlHooks
{
  std::function<std::vector<marine_control_bridge_client::ControlDevice>()> available_devices;
  std::function<void(const marine_control_bridge_client::ControlDevice &)> connect;
  std::function<void(const marine_control_bridge_client::ControlDevice &)> disconnect;
  std::function<bool(const std::string & remote, const std::string & state_topic)> is_connected;
  std::function<void(std::function<void()>)> set_devices_changed_callback;
};

/// Supplies the local ControlSet state topics (the plugin wraps the node graph; a
/// test supplies a fixed list).
using LocalTopicsProvider = std::function<std::vector<std::string>()>;
/// Supplies the discoverable udp_bridge node names for the bridge selector.
using BridgeNodesProvider = std::function<std::vector<std::string>()>;
/// Notifies the owner that the operator picked a different bridge node, so it can
/// rebuild the BridgeControlClient behind the hooks. Empty string == "no bridge".
using BridgeSelectedCallback = std::function<void(const std::string &)>;

/// The Connections hub: a scrollable checklist of every local and remote control
/// device. Toggling a checkbox opens/closes that device's tab (via TabManager)
/// and, for a remote device, connects/disconnects it over the bridge (via the
/// injected BridgeControlHooks). Three sections:
///   - Local: one box per local ControlSet topic, minus any topic already bridged
///     in from a connected remote device (de-dup).
///   - Bridge: a selector of udp_bridge nodes; changing it asks the owner to
///     rebuild the client behind the hooks.
///   - Remote: one box per discovered remote device, grouped by remote.
///
/// A desired set (remote state topics the operator has asked for) is reconciled on
/// every devices-changed refresh: a desired device that is not yet connected is
/// connected and opened as soon as it is discovered, so checking a device before
/// its bridge_info arrives still brings it up. The devices-changed callback is
/// marshalled onto this widget itself (the stable QObject that outlives every
/// bridge rebuild and layout reparent), so a callback in flight can never target
/// a freed object.
class ConnectionsHubWidget : public QWidget
{
  Q_OBJECT

public:
  ConnectionsHubWidget(
    TabManager * tab_manager, BridgeControlHooks hooks, LocalTopicsProvider local_topics,
    QWidget * parent = nullptr);
  ~ConnectionsHubWidget() override;

  /// Wire the bridge selector: where its options come from, and who to tell when
  /// the operator changes it. Optional — without them the Bridge section is inert.
  void setBridgeNodesProvider(BridgeNodesProvider provider);
  void setBridgeSelectedCallback(BridgeSelectedCallback callback);

  /// Seed the remote devices the operator wants connected, each as a
  /// (remote node, state topic) pair — the same (remote, state_topic) identity the
  /// bridge client uses, so two remotes offering the same state-topic name stay
  /// distinct. Each is auto-connected and opened as it is discovered (reconciled on
  /// the next devices-changed). Used by restoreSettings.
  void setDesiredRemotes(const std::vector<std::pair<std::string, std::string>> & remotes);

  void saveSettings(qt_gui_cpp::Settings & settings) const;
  void restoreSettings(const qt_gui_cpp::Settings & settings);

  /// A device tab went away (operator clicked its close button, or it was closed
  /// programmatically): uncheck its box and, for a remote device, disconnect it.
  void onTabClosed(const std::string & state_topic);

  /// Drop the borrowed TabManager pointer. Call this from the owner's teardown
  /// BEFORE it destroys the TabManager the hub was handed at construction. After
  /// this, every runtime slot (onDevicesChanged / onTabClosed / onTabCloseRequested
  /// and the checkbox toggles) no-ops rather than dereferencing the freed manager,
  /// so the hub's post-shutdown safety is self-contained — it does not depend on the
  /// owner also disconnecting signals or gating its marshalled callbacks.
  void detachTabManager();

public slots:
  /// Re-query the providers and rebuild every section. Deferred off the plugin-load
  /// path (the providers touch the node graph, which can stall mid-discovery).
  void refresh();
  /// Marshalling target for the bridge's devices-changed callback: reconcile the
  /// desired set against the newly discovered devices, then rebuild the sections.
  void onDevicesChanged();
  /// QTabWidget::tabCloseRequested handler: map the index to a state topic and
  /// route to onTabClosed. Indices shift when the hub sits as tab 0, so this
  /// resolves through TabManager (by widget), never by a fixed offset.
  void onTabCloseRequested(int index);

private slots:
  void onBridgeComboChanged(int index);

private:
  void buildUi();
  void rebuildBridgeCombo();
  // Rebuild the Local + Remote checklists from a device snapshot (passed in so a
  // single availableDevices() query serves both reconcile and rebuild).
  void rebuildSections(const std::vector<marine_control_bridge_client::ControlDevice> & devices);
  void onLocalToggled(const std::string & state_topic, bool checked);
  void onRemoteToggled(const marine_control_bridge_client::ControlDevice & device, bool checked);

  TabManager * tab_manager_;
  BridgeControlHooks hooks_;
  LocalTopicsProvider local_topics_;
  BridgeNodesProvider bridge_nodes_;
  BridgeSelectedCallback on_bridge_selected_;

  QComboBox * bridge_combo_ = nullptr;
  QVBoxLayout * local_layout_ = nullptr;
  QVBoxLayout * remote_layout_ = nullptr;

  // Live checkboxes, rebuilt each refresh; keyed by state topic (local) or paired
  // with the owning device (remote). Cleared and recreated by rebuildSections.
  std::map<std::string, QCheckBox *> local_boxes_;
  struct RemoteBox
  {
    QCheckBox * box = nullptr;
    marine_control_bridge_client::ControlDevice device;
  };
  std::vector<RemoteBox> remote_boxes_;

  // Remote devices the operator wants connected, keyed by (remote, state_topic)
  // via deviceKey() in the .cpp — so two remotes sharing a state-topic name stay
  // distinct, matching BridgeControlClient's own keying. Drives reconcile and the
  // remote checkbox state; survives device-list churn so a dropped-then-restored
  // device is reconnected automatically.
  std::set<std::string> desired_;

  // The remote device currently occupying each open device tab, keyed by state
  // topic. TabManager keys tabs by state topic alone, so this is the authoritative
  // record of WHICH remote a tab belongs to when several remotes share a state-topic
  // name — it lets onTabClosed disconnect the exact remote instead of the first
  // discovered match. Local-only tabs have no entry.
  //
  // Limitation: because a tab is keyed by state topic, two remotes that share a
  // state-topic name collapse onto a single tab (the later one wins this map); only
  // one can be tabbed at a time. Disambiguating the tab layer itself would require
  // reworking TabManager's key/subscription split and is out of scope here (two
  // remotes bridged to one local state topic is already a bridge-level conflict).
  std::map<std::string, marine_control_bridge_client::ControlDevice> open_remote_tabs_;
};

}  // namespace rqt_marine_control

#endif  // RQT_MARINE_CONTROL__CONNECTIONS_HUB_WIDGET_HPP_
