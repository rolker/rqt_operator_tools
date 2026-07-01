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

// Node-free hub tests (#97). ConnectionsHubWidget drives the bridge through an
// injected BridgeControlHooks and tabs through an injected TabTransportFactory, so
// in-memory fakes prove: checking a box opens the tab (and connects a remote
// device) and unchecking closes it (and disconnects); a connected remote device's
// topic drops out of the Local section (de-dup); and a device desired before it is
// discovered auto-connects and opens on the next devices-changed (reconcile).
// Runs under QT_QPA_PLATFORM=offscreen (set in CMakeLists). No live ROS node, no
// real BridgeControlClient.

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QList>
#include <QTabWidget>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <marine_control_interfaces/msg/control_set.hpp>

#include "marine_control_bridge_client/bridge_control_discovery.hpp"
#include "rqt_marine_control/connections_hub_widget.hpp"
#include "rqt_marine_control/tab_manager.hpp"

namespace
{
using marine_control_bridge_client::ControlDevice;
using marine_control_interfaces::msg::ControlSet;
using rqt_marine_control::BridgeControlHooks;
using rqt_marine_control::ConnectionsHubWidget;
using rqt_marine_control::TabManager;
using rqt_marine_control::TabTransport;
using rqt_marine_control::TabTransportFactory;

// In-memory stand-in for a live BridgeControlClient: a device list, a connected
// set, and counters the hooks write through so a test can observe connect/
// disconnect. The devices-changed callback the hub registers is captured but not
// auto-fired (the fake has no discovery thread); tests fire onDevicesChanged
// directly to model a discovery tick.
struct FakeBridge
{
  std::vector<ControlDevice> devices;
  std::set<std::string> connected;
  int connect_calls = 0;
  int disconnect_calls = 0;
  std::function<void()> devices_changed;

  static std::string key(const std::string & remote, const std::string & state_topic)
  {
    return remote + "|" + state_topic;
  }
};

BridgeControlHooks makeHooks(std::shared_ptr<FakeBridge> bridge)
{
  BridgeControlHooks hooks;
  hooks.available_devices = [bridge]() {return bridge->devices;};
  hooks.connect = [bridge](const ControlDevice & device) {
      bridge->connect_calls++;
      bridge->connected.insert(FakeBridge::key(device.remote, device.state_topic));
    };
  hooks.disconnect = [bridge](const ControlDevice & device) {
      bridge->disconnect_calls++;
      bridge->connected.erase(FakeBridge::key(device.remote, device.state_topic));
    };
  hooks.is_connected = [bridge](const std::string & remote, const std::string & state_topic) {
      return bridge->connected.count(FakeBridge::key(remote, state_topic)) != 0;
    };
  hooks.set_devices_changed_callback = [bridge](std::function<void()> callback) {
      bridge->devices_changed = std::move(callback);
    };
  return hooks;
}

// Minimal transport: the hub/TabManager only need tabs to open and close here; no
// ROS I/O is exercised.
class NullTransport : public TabTransport
{
public:
  void publishChange(const std::string &, const std::string &) override {}
};

TabTransportFactory nullFactory()
{
  return [](const std::string &, std::function<void(const ControlSet &)>)
         -> std::shared_ptr<TabTransport> {return std::make_shared<NullTransport>();};
}

QCheckBox * findCheckBox(const QWidget * parent, const std::string & text)
{
  const QString wanted = QString::fromStdString(text);
  for (QCheckBox * box : parent->findChildren<QCheckBox *>()) {
    if (box->text() == wanted) {
      return box;
    }
  }
  return nullptr;
}

int countCheckBoxes(const QWidget * parent, const std::string & text)
{
  const QString wanted = QString::fromStdString(text);
  int count = 0;
  for (QCheckBox * box : parent->findChildren<QCheckBox *>()) {
    if (box->text() == wanted) {
      count++;
    }
  }
  return count;
}

ControlDevice device(const char * remote, const char * state_topic)
{
  ControlDevice d;
  d.remote = remote;
  d.state_topic = state_topic;
  d.change_topic = marine_control_bridge_client::deriveChangeTopic(state_topic);
  return d;
}

class ConnectionsHubTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "test_connections_hub";
      static char * argv[] = {arg0, nullptr};
      app_ = std::make_unique<QApplication>(argc, argv);
    }
    bridge_ = std::make_shared<FakeBridge>();
  }
  std::unique_ptr<QApplication> app_;
  std::shared_ptr<FakeBridge> bridge_;
};
}  // namespace

TEST_F(ConnectionsHubTest, LocalCheckboxOpensAndClosesItsTab)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, nullFactory());
  std::vector<std::string> local = {"/a/state", "/b/state"};
  ConnectionsHubWidget hub(&mgr, makeHooks(bridge_), [&local]() {return local;});
  hub.refresh();

  QCheckBox * box = findCheckBox(&hub, "/a/state");
  ASSERT_NE(box, nullptr);
  EXPECT_FALSE(box->isChecked());

  box->click();   // check -> open the tab
  EXPECT_TRUE(mgr.hasTab("/a/state"));
  EXPECT_FALSE(mgr.hasTab("/b/state"));

  box->click();   // uncheck -> close the tab
  EXPECT_FALSE(mgr.hasTab("/a/state"));
}

TEST_F(ConnectionsHubTest, RemoteCheckboxConnectsOpensDisconnectsCloses)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, nullFactory());
  std::vector<std::string> local;   // no local devices
  bridge_->devices = {device("/boat/bridge", "/thruster/state")};
  ConnectionsHubWidget hub(&mgr, makeHooks(bridge_), [&local]() {return local;});
  hub.refresh();

  QCheckBox * box = findCheckBox(&hub, "/thruster/state");
  ASSERT_NE(box, nullptr);
  EXPECT_FALSE(box->isChecked());

  box->click();   // check -> connect over the bridge + open the tab
  EXPECT_EQ(bridge_->connect_calls, 1);
  EXPECT_TRUE(mgr.hasTab("/thruster/state"));

  // The post-toggle reconcile is marshalled through a queued call; run it, then
  // the rebuilt box reflects the now-connected state.
  QCoreApplication::processEvents();
  box = findCheckBox(&hub, "/thruster/state");
  ASSERT_NE(box, nullptr);
  EXPECT_TRUE(box->isChecked());

  box->click();   // uncheck -> disconnect + close the tab
  EXPECT_EQ(bridge_->disconnect_calls, 1);
  EXPECT_FALSE(mgr.hasTab("/thruster/state"));
}

TEST_F(ConnectionsHubTest, ConnectedRemoteTopicDropsOutOfLocalSection)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, nullFactory());
  // A device is offered both locally and by a remote (same state topic), plus a
  // local-only device.
  std::vector<std::string> local = {"/shared/state", "/local_only/state"};
  bridge_->devices = {device("/boat/bridge", "/shared/state")};
  ConnectionsHubWidget hub(&mgr, makeHooks(bridge_), [&local]() {return local;});
  hub.refresh();

  // Not connected yet: the shared topic shows in BOTH the Local and Remote
  // sections (two boxes).
  EXPECT_EQ(countCheckBoxes(&hub, "/shared/state"), 2);
  EXPECT_EQ(countCheckBoxes(&hub, "/local_only/state"), 1);

  // Connect it over the bridge, then a discovery tick rebuilds the sections.
  bridge_->connected.insert(FakeBridge::key("/boat/bridge", "/shared/state"));
  hub.onDevicesChanged();

  // De-dup: the Local duplicate is gone; only the Remote box for the shared topic
  // remains. The local-only device is untouched.
  EXPECT_EQ(countCheckBoxes(&hub, "/shared/state"), 1);
  EXPECT_EQ(countCheckBoxes(&hub, "/local_only/state"), 1);
}

TEST_F(ConnectionsHubTest, DesiredDeviceAutoConnectsAndOpensWhenDiscovered)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, nullFactory());
  std::vector<std::string> local;
  ConnectionsHubWidget hub(&mgr, makeHooks(bridge_), [&local]() {return local;});

  // Desire a device before it has been discovered (as restoreSettings would),
  // keyed by its (remote, state_topic) identity.
  hub.setDesiredRemotes({{"/boat/bridge", "/late/state"}});
  hub.onDevicesChanged();   // nothing discovered yet -> no connect, no tab
  EXPECT_EQ(bridge_->connect_calls, 0);
  EXPECT_FALSE(mgr.hasTab("/late/state"));

  // The device appears; the next devices-changed reconciles the desired set.
  bridge_->devices = {device("/boat/bridge", "/late/state")};
  hub.onDevicesChanged();

  EXPECT_EQ(bridge_->connect_calls, 1);
  EXPECT_TRUE(mgr.hasTab("/late/state"));
  QCheckBox * box = findCheckBox(&hub, "/late/state");
  ASSERT_NE(box, nullptr);
  EXPECT_TRUE(box->isChecked());
}

// Two remotes offering the SAME state topic must stay distinct: the hub keys remote
// devices by (remote, state_topic), so checking one connects only that remote, its
// box (not the other's) reflects the state, and closing the shared tab disconnects
// the remote that actually owns it — not the first discovered match on the topic.
// First-match (topic-only) keying would have connected/disconnected the wrong remote.
TEST_F(ConnectionsHubTest, DuplicateStateTopicAcrossRemotesKeysByRemote)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, nullFactory());
  std::vector<std::string> local;
  const ControlDevice alpha = device("/alpha/bridge", "/thruster/state");
  const ControlDevice bravo = device("/bravo/bridge", "/thruster/state");
  bridge_->devices = {alpha, bravo};   // iteration order: alpha then bravo
  ConnectionsHubWidget hub(&mgr, makeHooks(bridge_), [&local]() {return local;});
  hub.refresh();

  // Two remote boxes share the "/thruster/state" label, one per remote, in device
  // order (alpha first, bravo second) — the only checkboxes present (no locals).
  auto sharedBoxes = [&hub]() {
      QList<QCheckBox *> found;
      for (QCheckBox * box : hub.findChildren<QCheckBox *>()) {
        if (box->text() == "/thruster/state") {
          found.append(box);
        }
      }
      return found;
    };
  QList<QCheckBox *> boxes = sharedBoxes();
  ASSERT_EQ(boxes.size(), 2);

  // Check the SECOND remote (bravo): only bravo connects; alpha is untouched.
  boxes[1]->click();
  EXPECT_EQ(bridge_->connect_calls, 1);
  EXPECT_NE(bridge_->connected.count(FakeBridge::key("/bravo/bridge", "/thruster/state")), 0u);
  EXPECT_EQ(bridge_->connected.count(FakeBridge::key("/alpha/bridge", "/thruster/state")), 0u);

  // After the queued reconcile rebuilds the sections, only bravo's box is checked.
  QCoreApplication::processEvents();
  boxes = sharedBoxes();
  ASSERT_EQ(boxes.size(), 2);
  EXPECT_FALSE(boxes[0]->isChecked());   // alpha
  EXPECT_TRUE(boxes[1]->isChecked());    // bravo

  // Closing the (single, shared) tab disconnects the remote that actually owns it —
  // bravo — not the first-by-topic match (alpha).
  ASSERT_TRUE(mgr.hasTab("/thruster/state"));
  hub.onTabClosed("/thruster/state");
  EXPECT_EQ(bridge_->disconnect_calls, 1);
  EXPECT_EQ(bridge_->connected.count(FakeBridge::key("/bravo/bridge", "/thruster/state")), 0u);
  EXPECT_FALSE(mgr.hasTab("/thruster/state"));
}

// The hub borrows the TabManager. After the owner tears the manager down it must
// call detachTabManager() first; every runtime slot then no-ops on the hub's own
// null guard instead of dereferencing freed memory — post-shutdown safety local to
// the hub, not gated on the owner disconnecting signals. Here the manager is
// heap-allocated and deleted after detach, so any surviving deref would be a
// use-after-free the slots below must not commit.
TEST_F(ConnectionsHubTest, SlotsNoOpAfterTabManagerDetached)
{
  auto tabs = std::make_unique<QTabWidget>();
  auto * mgr = new TabManager(tabs.get(), nullFactory());
  std::vector<std::string> local = {"/a/state"};
  bridge_->devices = {device("/boat/bridge", "/thruster/state")};
  ConnectionsHubWidget hub(mgr, makeHooks(bridge_), [&local]() {return local;});
  hub.refresh();

  const int connect_before = bridge_->connect_calls;
  const int disconnect_before = bridge_->disconnect_calls;

  // Owner teardown order: detach the borrowed pointer, THEN destroy the manager.
  hub.detachTabManager();
  delete mgr;

  // None of these may touch the freed manager, and none may drive bridge activity.
  hub.onDevicesChanged();
  hub.onTabClosed("/thruster/state");
  hub.onTabCloseRequested(0);
  QCoreApplication::processEvents();   // drain any queued marshalled reconcile

  EXPECT_EQ(bridge_->connect_calls, connect_before);
  EXPECT_EQ(bridge_->disconnect_calls, disconnect_before);
}
