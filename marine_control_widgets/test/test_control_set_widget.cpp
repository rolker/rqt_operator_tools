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
  EXPECT_EQ(w.inputFor("a"), nullptr);
}
