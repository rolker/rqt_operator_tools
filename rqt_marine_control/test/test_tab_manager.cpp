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

// Node-free tab-lifecycle tests (#92 must-fix). TabManager creates each tab's
// subscription/publisher through an injected TabTransportFactory; a counting
// fake stands in for the rclcpp transport, so we can prove: N tabs create N
// subscriptions; closing a tab destroys exactly its subscription (no leak); and
// an edit in one tab publishes only to that tab's transport (no cross-talk).
// Runs under QT_QPA_PLATFORM=offscreen (set in CMakeLists).

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QScrollArea>
#include <QTabWidget>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <marine_control_interfaces/msg/control_item.hpp>
#include <marine_control_interfaces/msg/control_set.hpp>

#include "marine_control_widgets/control_set_widget.hpp"
#include "rqt_marine_control/tab_manager.hpp"

namespace
{
using marine_control_interfaces::msg::ControlItem;
using marine_control_interfaces::msg::ControlSet;
using rqt_marine_control::TabManager;
using rqt_marine_control::TabTransport;
using rqt_marine_control::TabTransportFactory;

// Per-topic record the fake transport writes through, so the test can observe a
// transport's lifetime (alive) and its publishes after the transport object
// itself is owned away inside the TabManager.
struct TransportRecord
{
  bool alive = false;
  int create_count = 0;
  std::function<void(const ControlSet &)> on_set;
  std::vector<std::pair<std::string, std::string>> published;
};

// alive flips true on construction, false on destruction — so a leaked (never
// destroyed) transport is directly detectable.
class FakeTransport : public TabTransport
{
public:
  explicit FakeTransport(std::shared_ptr<TransportRecord> rec)
  : rec_(std::move(rec))
  {
    rec_->alive = true;
    rec_->create_count++;
  }
  ~FakeTransport() override {rec_->alive = false;}
  void publishChange(const std::string & name, const std::string & value) override
  {
    rec_->published.emplace_back(name, value);
  }

private:
  std::shared_ptr<TransportRecord> rec_;
};

// Records persist across a topic's open/close cycles (so a test can assert a
// closed tab's transport was destroyed), and total_created counts every
// construction.
struct Registry
{
  std::map<std::string, std::shared_ptr<TransportRecord>> records;
  int total_created = 0;

  std::shared_ptr<TransportRecord> get(const std::string & topic)
  {
    auto & r = records[topic];
    if (!r) {
      r = std::make_shared<TransportRecord>();
    }
    return r;
  }
};

TabTransportFactory makeFactory(std::shared_ptr<Registry> reg)
{
  return [reg](const std::string & topic, std::function<void(const ControlSet &)> on_set)
         -> std::shared_ptr<TabTransport> {
           auto rec = reg->get(topic);
           rec->on_set = std::move(on_set);
           reg->total_created++;
           return std::make_shared<FakeTransport>(rec);
         };
}

ControlItem boolItem(const char * name, const char * value)
{
  ControlItem it;
  it.name = name;
  it.label = name;
  it.value = value;
  it.type = ControlItem::TYPE_BOOL;
  return it;
}

class TabManagerTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "test_tab_manager";
      static char * argv[] = {arg0, nullptr};
      app_ = std::make_unique<QApplication>(argc, argv);
    }
    reg_ = std::make_shared<Registry>();
  }
  std::unique_ptr<QApplication> app_;
  std::shared_ptr<Registry> reg_;
};
}  // namespace

TEST_F(TabManagerTest, OpeningTabsCreatesOneSubscriptionEach)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, makeFactory(reg_));

  mgr.openTab("/a/state");
  mgr.openTab("/b/state");
  mgr.openTab("/c/state");

  EXPECT_EQ(mgr.tabCount(), 3);
  EXPECT_EQ(tabs.count(), 3);
  EXPECT_EQ(reg_->total_created, 3);   // exactly one transport per device
  EXPECT_TRUE(reg_->get("/a/state")->alive);
  EXPECT_TRUE(reg_->get("/b/state")->alive);
  EXPECT_TRUE(reg_->get("/c/state")->alive);
}

TEST_F(TabManagerTest, ReopeningSameTopicReusesTabAndSubscription)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, makeFactory(reg_));

  mgr.openTab("/a/state");
  mgr.openTab("/a/state");   // same topic -> focus, not a second tab

  EXPECT_EQ(mgr.tabCount(), 1);
  EXPECT_EQ(reg_->total_created, 1);
}

TEST_F(TabManagerTest, ClosingTabDestroysOnlyItsSubscription)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, makeFactory(reg_));

  mgr.openTab("/a/state");
  mgr.openTab("/b/state");
  ASSERT_TRUE(reg_->get("/a/state")->alive);
  ASSERT_TRUE(reg_->get("/b/state")->alive);

  mgr.closeTab("/a/state");

  EXPECT_FALSE(reg_->get("/a/state")->alive);   // its transport destroyed (no leak)
  EXPECT_TRUE(reg_->get("/b/state")->alive);    // the other survives
  EXPECT_FALSE(mgr.hasTab("/a/state"));
  EXPECT_TRUE(mgr.hasTab("/b/state"));
  EXPECT_EQ(mgr.tabCount(), 1);
  EXPECT_EQ(tabs.count(), 1);
}

TEST_F(TabManagerTest, ClearDestroysAllSubscriptions)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, makeFactory(reg_));
  mgr.openTab("/a/state");
  mgr.openTab("/b/state");

  mgr.clear();

  EXPECT_EQ(mgr.tabCount(), 0);
  EXPECT_EQ(tabs.count(), 0);
  EXPECT_FALSE(reg_->get("/a/state")->alive);
  EXPECT_FALSE(reg_->get("/b/state")->alive);
}

TEST_F(TabManagerTest, EditPublishesOnlyToThatTabsTransport)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, makeFactory(reg_));
  auto * widget_a = mgr.openTab("/a/state");
  mgr.openTab("/b/state");

  // Give tab A a control, then simulate the operator toggling it.
  ControlSet set;
  set.items.push_back(boolItem("enabled", "false"));
  widget_a->apply(set);
  auto * check = dynamic_cast<QCheckBox *>(widget_a->inputFor("enabled"));
  ASSERT_NE(check, nullptr);
  check->click();   // false -> true, emits controlChanged on widget A

  // The edit reaches A's transport and nowhere else.
  ASSERT_EQ(reg_->get("/a/state")->published.size(), 1u);
  EXPECT_EQ(reg_->get("/a/state")->published[0].first, "enabled");
  EXPECT_EQ(reg_->get("/a/state")->published[0].second, "true");
  EXPECT_TRUE(reg_->get("/b/state")->published.empty());   // no cross-talk
}

TEST_F(TabManagerTest, TabContentIsWrappedInAResizableScrollArea)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, makeFactory(reg_));
  auto * widget = mgr.openTab("/a/state");

  // The tab's page is a scroll area (so a tall control set scrolls instead of
  // growing the plugin), and the ControlSetWidget is its scrollable content.
  auto * page = dynamic_cast<QScrollArea *>(tabs.widget(0));
  ASSERT_NE(page, nullptr);
  EXPECT_TRUE(page->widgetResizable());
  EXPECT_EQ(page->widget(), widget);
}

TEST_F(TabManagerTest, TabIndexForTracksTabPositionAndClose)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, makeFactory(reg_));
  mgr.openTab("/a/state");
  mgr.openTab("/b/state");

  EXPECT_EQ(mgr.tabIndexFor("/a/state"), 0);
  EXPECT_EQ(mgr.tabIndexFor("/b/state"), 1);
  EXPECT_EQ(mgr.tabIndexFor("/missing/state"), -1);

  mgr.closeTab("/a/state");
  EXPECT_EQ(mgr.tabIndexFor("/a/state"), -1);   // gone
  EXPECT_EQ(mgr.tabIndexFor("/b/state"), 0);    // shifted down into the freed slot
}

TEST_F(TabManagerTest, DeliveredSetRendersAndTitlesTabWithDeviceName)
{
  QTabWidget tabs;
  TabManager mgr(&tabs, makeFactory(reg_));
  auto * widget = mgr.openTab("/a/state");
  // Before any set, the tab is titled with the topic string.
  EXPECT_EQ(tabs.tabText(0), QStringLiteral("/a/state"));

  // Deliver a set the way the real transport would (on the GUI thread).
  ControlSet set;
  set.device_name = "Forward Sonar";
  set.items.push_back(boolItem("enabled", "true"));
  reg_->get("/a/state")->on_set(set);

  EXPECT_EQ(widget->rowCount(), 1);
  EXPECT_EQ(tabs.tabText(0), QStringLiteral("Forward Sonar"));   // device_name title
}
