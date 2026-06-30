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

// Offscreen tests for ControlSetWidget: one input per ControlItem type, value
// labels carry units, read-only items have no input, repeated apply() refreshes
// without adding rows, and a user edit emits controlChanged. Runs under
// QT_QPA_PLATFORM=offscreen (set in CMakeLists).

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QString>

#include <memory>
#include <string>
#include <vector>

#include <marine_control_interfaces/msg/control_item.hpp>
#include <marine_control_interfaces/msg/control_set.hpp>

#include "marine_control_widgets/control_set_widget.hpp"

namespace
{
using marine_control_interfaces::msg::ControlItem;
using marine_control_interfaces::msg::ControlSet;
using marine_control_widgets::ControlSetWidget;

ControlItem item(const char * name, uint8_t type, const char * value)
{
  ControlItem it;
  it.name = name;
  it.label = name;
  it.value = value;
  it.type = type;
  return it;
}

class ControlSetWidgetTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "test_control_set_widget";
      static char * argv[] = {arg0, nullptr};
      app_ = std::make_unique<QApplication>(argc, argv);
    }
  }
  std::unique_ptr<QApplication> app_;
};
}  // namespace

TEST_F(ControlSetWidgetTest, OneInputWidgetPerType)
{
  ControlSetWidget w;
  ControlSet set;
  set.items.push_back(item("gain", ControlItem::TYPE_FLOAT, "5.0"));
  set.items.push_back(item("bins", ControlItem::TYPE_INT, "512"));
  set.items.push_back(item("enabled", ControlItem::TYPE_BOOL, "true"));
  set.items.push_back(item("label", ControlItem::TYPE_STRING, "hello"));
  auto en = item("mode", ControlItem::TYPE_ENUM, "auto");
  en.enums = {"auto", "manual"};
  set.items.push_back(en);
  w.apply(set);

  EXPECT_EQ(w.rowCount(), 5);
  EXPECT_NE(dynamic_cast<QDoubleSpinBox *>(w.inputFor("gain")), nullptr);
  EXPECT_NE(dynamic_cast<QSpinBox *>(w.inputFor("bins")), nullptr);
  EXPECT_NE(dynamic_cast<QCheckBox *>(w.inputFor("enabled")), nullptr);
  EXPECT_NE(dynamic_cast<QLineEdit *>(w.inputFor("label")), nullptr);
  EXPECT_NE(dynamic_cast<QComboBox *>(w.inputFor("mode")), nullptr);
}

TEST_F(ControlSetWidgetTest, ReadOnlyHasNoInputAndShowsUnits)
{
  ControlSetWidget w;
  ControlSet set;
  auto ro = item("depth", ControlItem::TYPE_FLOAT, "12.3");
  ro.read_only = true;
  ro.units = "m";
  set.items.push_back(ro);
  w.apply(set);

  EXPECT_EQ(w.inputFor("depth"), nullptr);          // no editable widget
  EXPECT_EQ(w.valueText("depth"), QStringLiteral("12.3 m"));   // value carries units
}

TEST_F(ControlSetWidgetTest, RepeatedApplyRefreshesWithoutAddingRows)
{
  ControlSetWidget w;
  ControlSet set;
  set.items.push_back(item("gain", ControlItem::TYPE_FLOAT, "5.0"));
  w.apply(set);
  ASSERT_EQ(w.rowCount(), 1);

  set.items[0].value = "7.5";
  w.apply(set);
  EXPECT_EQ(w.rowCount(), 1);                        // updated, not duplicated
  EXPECT_EQ(w.valueText("gain"), QStringLiteral("7.5"));
}

TEST_F(ControlSetWidgetTest, CheckboxEditEmitsControlChanged)
{
  ControlSetWidget w;
  ControlSet set;
  set.items.push_back(item("enabled", ControlItem::TYPE_BOOL, "false"));
  w.apply(set);

  QString got_name;
  QString got_value;
  int count = 0;
  QObject::connect(
    &w, &ControlSetWidget::controlChanged,
    [&](const QString & n, const QString & v) {got_name = n; got_value = v; ++count;});

  auto * check = dynamic_cast<QCheckBox *>(w.inputFor("enabled"));
  ASSERT_NE(check, nullptr);
  check->click();   // user toggles false -> true

  EXPECT_EQ(count, 1);
  EXPECT_EQ(got_name, QStringLiteral("enabled"));
  EXPECT_EQ(got_value, QStringLiteral("true"));
}

TEST_F(ControlSetWidgetTest, RefreshDoesNotReEmitChange)
{
  // A device state refresh (apply) must not look like a user edit — setChecked
  // is signal-blocked, so controlChanged stays silent.
  ControlSetWidget w;
  ControlSet set;
  set.items.push_back(item("enabled", ControlItem::TYPE_BOOL, "false"));
  w.apply(set);

  int count = 0;
  QObject::connect(
    &w, &ControlSetWidget::controlChanged, [&](const QString &, const QString &) {++count;});

  set.items[0].value = "true";
  w.apply(set);   // device says it's now on

  EXPECT_EQ(count, 0);
  EXPECT_TRUE(dynamic_cast<QCheckBox *>(w.inputFor("enabled"))->isChecked());
}

TEST_F(ControlSetWidgetTest, CaseInsensitiveBoolParse)
{
  // A device echoing "TRUE"/"On" must read as checked (not flip to false).
  ControlSetWidget w;
  ControlSet set;
  set.items.push_back(item("enabled", ControlItem::TYPE_BOOL, "TRUE"));
  w.apply(set);
  EXPECT_TRUE(dynamic_cast<QCheckBox *>(w.inputFor("enabled"))->isChecked());
}

TEST_F(ControlSetWidgetTest, EnumShowsValueOutsideChoices)
{
  // A device value not in the advertised enum list is still shown (combo and
  // value label must not disagree).
  ControlSetWidget w;
  ControlSet set;
  auto en = item("mode", ControlItem::TYPE_ENUM, "custom");
  en.enums = {"auto", "manual"};
  set.items.push_back(en);
  w.apply(set);
  auto * combo = dynamic_cast<QComboBox *>(w.inputFor("mode"));
  ASSERT_NE(combo, nullptr);
  EXPECT_EQ(combo->currentText(), QStringLiteral("custom"));
}

TEST_F(ControlSetWidgetTest, NoOpToggleBackEmitsOnlyDistinctValues)
{
  // Toggling true then back to false emits each distinct value once; the
  // change-suppression only blocks a re-emit of the *same* value.
  ControlSetWidget w;
  ControlSet set;
  set.items.push_back(item("enabled", ControlItem::TYPE_BOOL, "false"));
  w.apply(set);
  int count = 0;
  QObject::connect(
    &w, &ControlSetWidget::controlChanged, [&](const QString &, const QString &) {++count;});
  auto * check = dynamic_cast<QCheckBox *>(w.inputFor("enabled"));
  check->click();   // -> true
  check->click();   // -> false
  EXPECT_EQ(count, 2);
}

TEST_F(ControlSetWidgetTest, ClearRemovesAllRows)
{
  ControlSetWidget w;
  ControlSet set;
  set.items.push_back(item("a", ControlItem::TYPE_FLOAT, "1.0"));
  set.items.push_back(item("b", ControlItem::TYPE_INT, "2"));
  w.apply(set);
  ASSERT_EQ(w.rowCount(), 2);
  w.clear();
  EXPECT_EQ(w.rowCount(), 0);
  EXPECT_EQ(w.sectionCount(), 0);
  EXPECT_EQ(w.inputFor("a"), nullptr);
}

namespace
{
// An item carrying a group, for the grouping tests.
ControlItem grouped(const char * name, const char * group)
{
  ControlItem it = item(name, ControlItem::TYPE_FLOAT, "0.0");
  it.group = group;
  return it;
}
}  // namespace

TEST_F(ControlSetWidgetTest, GroupedItemsRenderInSections)
{
  ControlSetWidget w;
  ControlSet set;
  set.items.push_back(grouped("gain", "Display"));
  set.items.push_back(grouped("range", "Transmit"));
  w.apply(set);

  EXPECT_EQ(w.sectionCount(), 2);
  EXPECT_EQ(w.rowCount(), 2);
  // Both controls are still reachable regardless of which section they landed in.
  EXPECT_NE(w.inputFor("gain"), nullptr);
  EXPECT_NE(w.inputFor("range"), nullptr);
}

TEST_F(ControlSetWidgetTest, UngroupedItemsGoToDefaultSection)
{
  ControlSetWidget w;
  ControlSet set;
  set.items.push_back(item("gain", ControlItem::TYPE_FLOAT, "1.0"));   // no group
  set.items.push_back(item("bins", ControlItem::TYPE_INT, "2"));       // no group
  w.apply(set);

  // Both fall into the single default section.
  ASSERT_EQ(w.sectionCount(), 1);
  EXPECT_EQ(w.sectionOrder().front(), std::string());   // "" == General
}

TEST_F(ControlSetWidgetTest, GroupsPreserveFirstSeenOrder)
{
  ControlSetWidget w;
  ControlSet set;
  set.items.push_back(grouped("a1", "Alpha"));
  set.items.push_back(grouped("b1", "Beta"));
  set.items.push_back(grouped("a2", "Alpha"));   // back to an existing group
  set.items.push_back(grouped("b2", "Beta"));
  w.apply(set);

  ASSERT_EQ(w.sectionCount(), 2);
  const std::vector<std::string> order = w.sectionOrder();
  ASSERT_EQ(order.size(), 2u);
  EXPECT_EQ(order[0], "Alpha");   // first seen leads
  EXPECT_EQ(order[1], "Beta");
}

TEST_F(ControlSetWidgetTest, ApplyReconcilesDroppedControlsAndEmptySections)
{
  // A control dropped from a later heartbeat must not linger as a stale row, and
  // a group emptied by that removal must not leave an orphaned section header.
  ControlSetWidget w;
  ControlSet first;
  first.items.push_back(grouped("gain", "Display"));
  first.items.push_back(grouped("range", "Transmit"));
  w.apply(first);
  ASSERT_EQ(w.rowCount(), 2);
  ASSERT_EQ(w.sectionCount(), 2);

  // The next set drops "range" (and with it the whole "Transmit" group).
  ControlSet smaller;
  smaller.items.push_back(grouped("gain", "Display"));
  w.apply(smaller);

  EXPECT_EQ(w.rowCount(), 1);
  EXPECT_EQ(w.inputFor("range"), nullptr);            // dropped row is gone
  EXPECT_NE(w.inputFor("gain"), nullptr);             // surviving row stays
  ASSERT_EQ(w.sectionCount(), 1);                     // empty "Transmit" removed
  const std::vector<std::string> order = w.sectionOrder();
  ASSERT_EQ(order.size(), 1u);
  EXPECT_EQ(order.front(), "Display");
}

TEST_F(ControlSetWidgetTest, ReAddedGroupReturnsToFirstSeenSlot)
{
  // A group emptied by reconciliation and later re-added must come back to its
  // original first-seen slot, not land at the end (persistent first-seen order).
  ControlSetWidget w;
  ControlSet both;
  both.items.push_back(grouped("a1", "Alpha"));
  both.items.push_back(grouped("b1", "Beta"));
  w.apply(both);
  ASSERT_EQ(w.sectionOrder(), (std::vector<std::string>{"Alpha", "Beta"}));

  // Drop everything in Alpha -> the whole "Alpha" section is removed.
  ControlSet only_beta;
  only_beta.items.push_back(grouped("b1", "Beta"));
  w.apply(only_beta);
  ASSERT_EQ(w.sectionCount(), 1);
  ASSERT_EQ(w.sectionOrder(), (std::vector<std::string>{"Beta"}));

  // Alpha reappears; it must return ahead of Beta (its first-seen slot), not after.
  w.apply(both);
  EXPECT_EQ(w.sectionOrder(), (std::vector<std::string>{"Alpha", "Beta"}));
}

TEST_F(ControlSetWidgetTest, FlappingControlReclaimsGridRowsAndKeepsOrder)
{
  // A control that repeatedly drops then re-appears in a SURVIVING section must
  // not accumulate blank grid rows: the section's row count tracks the live row
  // count (freed rows reclaimed) and surviving rows keep their order.
  ControlSetWidget w;
  ControlSet both;
  both.items.push_back(grouped("alpha", "Shared"));
  both.items.push_back(grouped("beta", "Shared"));
  ControlSet only_beta;
  only_beta.items.push_back(grouped("beta", "Shared"));

  w.apply(both);
  ASSERT_EQ(w.rowCount(), 2);
  ASSERT_EQ(w.sectionCount(), 1);
  ASSERT_EQ(w.sectionRowCount("Shared"), 2);

  for (int cycle = 0; cycle < 5; ++cycle) {
    w.apply(only_beta);                             // alpha drops
    EXPECT_EQ(w.rowCount(), 1);
    EXPECT_EQ(w.sectionRowCount("Shared"), 1);      // freed row reclaimed, not monotonic
    EXPECT_EQ(w.inputFor("alpha"), nullptr);
    EXPECT_EQ(w.gridRowOf("beta"), 0);              // survivor packed to the top

    w.apply(both);                                  // alpha re-appears
    EXPECT_EQ(w.rowCount(), 2);
    EXPECT_EQ(w.sectionRowCount("Shared"), 2);      // bounded: never grows past 2
  }

  // After all the flapping the grid is packed into exactly rows 0 and 1 (no blank
  // rows piled up). The survivor (beta) keeps the top slot; re-added alpha appends
  // below it.
  EXPECT_EQ(w.gridRowOf("beta"), 0);
  EXPECT_EQ(w.gridRowOf("alpha"), 1);
}

TEST_F(ControlSetWidgetTest, RangeHintAppearsForBoundedFloat)
{
  ControlSetWidget w;
  ControlSet set;
  auto bounded = item("speed", ControlItem::TYPE_FLOAT, "5.0");
  bounded.min_value = 0.0;
  bounded.max_value = 10.0;
  bounded.units = "m";
  set.items.push_back(bounded);
  // An unbounded float (max == min) gets no hint.
  set.items.push_back(item("trim", ControlItem::TYPE_FLOAT, "0.0"));
  w.apply(set);

  const QString hint = w.rangeHintText("speed");
  EXPECT_TRUE(hint.contains("0"));
  EXPECT_TRUE(hint.contains("10"));
  EXPECT_TRUE(hint.contains("m"));
  EXPECT_TRUE(w.rangeHintText("trim").isEmpty());
}
