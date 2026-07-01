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

// Reparent/hysteresis tests for ResponsiveHubLayout (#97). The layout switches the
// hub between a side panel (wide) and a non-closable tab 0 (narrow). These tests
// prove the hub instance — and thus its checkbox state — survives the reparent;
// that a width in the dead band between the two breakpoints does not re-reparent
// (no thrash); and that the selected device tab is preserved by topic across the
// index shift the hub-as-tab-0 introduces. Driven through updateForWidth() so no
// resize timer is pumped. Runs under QT_QPA_PLATFORM=offscreen (set in CMakeLists).

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <memory>
#include <string>

#include <marine_control_interfaces/msg/control_set.hpp>

#include "marine_control_widgets/control_set_widget.hpp"
#include "rqt_marine_control/responsive_hub_layout.hpp"
#include "rqt_marine_control/tab_manager.hpp"

namespace
{
using marine_control_interfaces::msg::ControlSet;
using rqt_marine_control::ResponsiveHubLayout;
using rqt_marine_control::TabManager;
using rqt_marine_control::TabTransport;
using rqt_marine_control::TabTransportFactory;

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

// A hub stand-in carrying a single checkbox whose state we track across reparents.
QWidget * makeHubWithCheckbox(QCheckBox ** out_box)
{
  auto * hub = new QWidget();
  auto * layout = new QVBoxLayout(hub);
  auto * box = new QCheckBox("armed");
  box->setChecked(true);
  layout->addWidget(box);
  *out_box = box;
  return hub;
}

class ResponsiveHubLayoutTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "test_responsive_hub_layout";
      static char * argv[] = {arg0, nullptr};
      app_ = std::make_unique<QApplication>(argc, argv);
    }
  }
  std::unique_ptr<QApplication> app_;
};
}  // namespace

TEST_F(ResponsiveHubLayoutTest, HubStateSurvivesWideNarrowReparent)
{
  ResponsiveHubLayout layout;
  QCheckBox * box = nullptr;
  QWidget * hub = makeHubWithCheckbox(&box);
  layout.setHub(hub);

  // Default wide: the hub is in the side panel, not a tab.
  EXPECT_EQ(layout.mode(), ResponsiveHubLayout::Mode::Wide);
  EXPECT_EQ(layout.tabWidget()->indexOf(hub), -1);

  // Narrow: the hub docks as (non-closable) tab 0, its checkbox state intact.
  layout.updateForWidth(600);
  EXPECT_EQ(layout.mode(), ResponsiveHubLayout::Mode::Narrow);
  EXPECT_EQ(layout.tabWidget()->indexOf(hub), 0);
  EXPECT_TRUE(box->isChecked());
  EXPECT_EQ(layout.tabWidget()->tabBar()->tabButton(0, QTabBar::RightSide), nullptr);

  // Back to wide: the hub leaves the tab bar, still holding its state.
  layout.updateForWidth(1000);
  EXPECT_EQ(layout.mode(), ResponsiveHubLayout::Mode::Wide);
  EXPECT_EQ(layout.tabWidget()->indexOf(hub), -1);
  EXPECT_TRUE(box->isChecked());
}

TEST_F(ResponsiveHubLayoutTest, WidthInDeadBandDoesNotReparent)
{
  ResponsiveHubLayout layout;
  QCheckBox * box = nullptr;
  QWidget * hub = makeHubWithCheckbox(&box);
  layout.setHub(hub);

  layout.updateForWidth(1000);   // clearly wide
  EXPECT_EQ(layout.mode(), ResponsiveHubLayout::Mode::Wide);

  // A width inside [kHubPanelWidthLo, kHubPanelWidthHi] must not flip the mode.
  layout.updateForWidth(780);
  EXPECT_EQ(layout.mode(), ResponsiveHubLayout::Mode::Wide);
  EXPECT_EQ(layout.tabWidget()->indexOf(hub), -1);

  layout.updateForWidth(650);    // below the low breakpoint -> narrow
  EXPECT_EQ(layout.mode(), ResponsiveHubLayout::Mode::Narrow);

  // Same dead-band width, now approached from narrow: must stay narrow (hysteresis).
  layout.updateForWidth(780);
  EXPECT_EQ(layout.mode(), ResponsiveHubLayout::Mode::Narrow);
  EXPECT_EQ(layout.tabWidget()->indexOf(hub), 0);
  EXPECT_TRUE(box->isChecked());
}

TEST_F(ResponsiveHubLayoutTest, PreservesSelectedTabByTopicAcrossReparent)
{
  ResponsiveHubLayout layout;
  TabManager mgr(layout.tabWidget(), nullFactory());
  layout.setTabManager(&mgr);
  QCheckBox * box = nullptr;
  layout.setHub(makeHubWithCheckbox(&box));

  mgr.openTab("/a/state");
  mgr.openTab("/b/state");
  const int b_index = layout.tabWidget()->indexOf(mgr.widgetFor("/b/state"));
  layout.tabWidget()->setCurrentIndex(b_index);
  ASSERT_EQ(mgr.topicForIndex(layout.tabWidget()->currentIndex()), "/b/state");

  // Docking the hub as tab 0 shifts every device index by one; the selection must
  // still track /b/state, not whatever now sits at the old index.
  layout.updateForWidth(600);
  EXPECT_EQ(mgr.topicForIndex(layout.tabWidget()->currentIndex()), "/b/state");
}
