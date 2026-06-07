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

// Offscreen test for ControlPanel: the radar-derived dynamic control widgets
// build once per control name, refresh values thereafter, and emit
// controlChanged on edit. Runs under QT_QPA_PLATFORM=offscreen (CMakeLists).

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QLineEdit>
#include <QString>

#include <memory>

#include <marine_radar_control_msgs/msg/radar_control_item.hpp>
#include <marine_radar_control_msgs/msg/radar_control_set.hpp>

#include "rqt_sonar_waterfall/control_panel.hpp"

namespace
{

using marine_radar_control_msgs::msg::RadarControlItem;
using marine_radar_control_msgs::msg::RadarControlSet;
using rqt_sonar_waterfall::ControlPanel;

RadarControlItem float_item(const char * name, const char * value)
{
  RadarControlItem item;
  item.name = name;
  item.label = name;
  item.value = value;
  item.type = RadarControlItem::CONTROL_TYPE_FLOAT;
  item.min_value = 0.0f;
  item.max_value = 10.0f;
  return item;
}

RadarControlItem enum_item(const char * name, const char * value)
{
  RadarControlItem item;
  item.name = name;
  item.label = name;
  item.value = value;
  item.type = RadarControlItem::CONTROL_TYPE_ENUM;
  item.enums = {"off", "on", "auto"};
  return item;
}

class ControlPanelTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    if (QCoreApplication::instance() == nullptr) {
      static int argc = 1;
      static char arg0[] = "test_control_panel";
      static char * argv[] = {arg0, nullptr};
      app_ = std::make_unique<QApplication>(argc, argv);
    }
  }

  std::unique_ptr<QApplication> app_;
};

}  // namespace

TEST_F(ControlPanelTest, BuildsOneRowPerControl)
{
  ControlPanel panel;
  RadarControlSet set;
  set.items = {float_item("gain", "5"), enum_item("mode", "off")};
  panel.apply(set);
  EXPECT_EQ(panel.row_count(), 2);
  EXPECT_EQ(panel.value_text("gain"), QString("5"));
  EXPECT_EQ(panel.value_text("mode"), QString("off"));
}

TEST_F(ControlPanelTest, ReapplyUpdatesValuesWithoutNewRows)
{
  ControlPanel panel;
  RadarControlSet set;
  set.items = {float_item("gain", "5")};
  panel.apply(set);
  QWidget * input_before = panel.input_for("gain");

  set.items = {float_item("gain", "8")};
  panel.apply(set);

  EXPECT_EQ(panel.row_count(), 1);                  // no duplicate row
  EXPECT_EQ(panel.value_text("gain"), QString("8"));  // value refreshed
  EXPECT_EQ(panel.input_for("gain"), input_before);   // same widget reused
}

TEST_F(ControlPanelTest, ReapplyRefreshesUnfocusedInput)
{
  // An unfocused input widget should track device state across re-applies (it is
  // not focused in the offscreen test), so it doesn't show stale values.
  ControlPanel panel;
  RadarControlSet set;
  set.items = {float_item("gain", "5")};
  panel.apply(set);
  auto * edit = qobject_cast<QLineEdit *>(panel.input_for("gain"));
  ASSERT_NE(edit, nullptr);
  EXPECT_EQ(edit->text(), QString("5"));

  set.items = {float_item("gain", "8")};
  panel.apply(set);
  EXPECT_EQ(edit->text(), QString("8"));  // input followed the device update
}

TEST_F(ControlPanelTest, FloatEditEmitsControlChanged)
{
  ControlPanel panel;
  RadarControlSet set;
  set.items = {float_item("gain", "5")};
  panel.apply(set);

  QString key, value;
  QObject::connect(
    &panel, &ControlPanel::controlChanged,
    [&](const QString & k, const QString & v) {key = k; value = v;});

  auto * edit = qobject_cast<QLineEdit *>(panel.input_for("gain"));
  ASSERT_NE(edit, nullptr);
  edit->setText("7.5");
  QMetaObject::invokeMethod(edit, "editingFinished");

  EXPECT_EQ(key, QString("gain"));
  EXPECT_EQ(value, QString("7.5"));
}

TEST_F(ControlPanelTest, EnumActivationEmitsItemText)
{
  ControlPanel panel;
  RadarControlSet set;
  set.items = {enum_item("mode", "off")};
  panel.apply(set);

  QString key, value;
  QObject::connect(
    &panel, &ControlPanel::controlChanged,
    [&](const QString & k, const QString & v) {key = k; value = v;});

  auto * combo = qobject_cast<QComboBox *>(panel.input_for("mode"));
  ASSERT_NE(combo, nullptr);
  QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 1));

  EXPECT_EQ(key, QString("mode"));
  EXPECT_EQ(value, QString("on"));
}

TEST_F(ControlPanelTest, ClearRemovesAllRows)
{
  ControlPanel panel;
  RadarControlSet set;
  set.items = {float_item("gain", "5"), enum_item("mode", "off")};
  panel.apply(set);
  panel.clear();
  EXPECT_EQ(panel.row_count(), 0);
  EXPECT_EQ(panel.input_for("gain"), nullptr);
}
