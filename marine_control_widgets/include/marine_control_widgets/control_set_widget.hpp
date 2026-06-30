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

#ifndef MARINE_CONTROL_WIDGETS__CONTROL_SET_WIDGET_HPP_
#define MARINE_CONTROL_WIDGETS__CONTROL_SET_WIDGET_HPP_

#include <QString>  // NOLINT(build/include_order)
#include <QWidget>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <marine_control_interfaces/msg/control_set.hpp>
#include <marine_control_interfaces/msg/control_item.hpp>

class QGridLayout;
class QLabel;
class QVBoxLayout;

namespace marine_control_widgets
{

/// Generic, ROS-free panel that renders any marine_control_interfaces/ControlSet.
///
/// Generalizes rqt_sonar_waterfall::ControlPanel (which rendered the radar
/// RadarControlSet) to the marine_control contract: one input widget per
/// ControlItem type — FLOAT/INT -> spin box (min/max/step/units suffix),
/// BOOL -> check box, STRING -> line edit, ENUM -> combo box; a read_only item
/// (or an unknown type) shows a value label with no input. A widget is created
/// the first time a control name is seen; later updates refresh the displayed
/// value and the input, but skip an input that is focused so a periodic state
/// stream never stomps a control the operator is editing.
///
/// Editing a control emits controlChanged(name, value); the owner turns that
/// into a ControlValue on the device's change topic. The widget does no ROS I/O,
/// so it is unit-testable without a node.
///
/// Controls are grouped into labeled sections keyed by ControlItem.group; items
/// with an empty group fall into a "General" section. Sections appear in
/// first-seen order. When no item sets a group the single section's header is
/// hidden, so an ungrouped set renders as a flat grid (layout unchanged from
/// before grouping existed). FLOAT/INT controls with meaningful bounds
/// (max_value > min_value) also show a compact "[min – max units]" range hint
/// next to the input.
class ControlSetWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ControlSetWidget(QWidget * parent = nullptr);
  ~ControlSetWidget() override;

  /// Create missing controls and refresh the displayed values for the rest.
  void apply(const marine_control_interfaces::msg::ControlSet & set);

  /// Remove all controls (e.g. when the selected state topic changes).
  void clear();

  // --- introspection (for tests) ---
  int rowCount() const;
  int sectionCount() const;
  /// Section group keys in first-seen (layout) order; "" denotes the default
  /// "General" section.
  std::vector<std::string> sectionOrder() const;
  QString valueText(const std::string & name) const;
  QWidget * inputFor(const std::string & name) const;
  /// The compact "[min – max units]" hint text for a control, or empty if the
  /// control has no visible bounds.
  QString rangeHintText(const std::string & name) const;

signals:
  void controlChanged(const QString & name, const QString & value);

private:
  struct Row
  {
    QLabel * name = nullptr;
    QLabel * value = nullptr;
    QWidget * input = nullptr;     // nullptr for read-only / unknown-type rows
    QLabel * range_hint = nullptr;  // nullptr unless the control has visible bounds
    // Refreshes the input widget from a device value, skipping when the widget
    // is focused so an in-progress edit is never stomped. Unset for read-only.
    std::function<void(const std::string &)> set_value;
  };

  // One labeled section per ControlItem.group. The header is hidden while only
  // one section exists, so an ungrouped set looks like the old flat grid.
  struct GroupSection
  {
    QLabel * header = nullptr;
    QGridLayout * grid = nullptr;
    int row_count = 0;     // next free row in this section's grid
  };

  void makeInput(const marine_control_interfaces::msg::ControlItem & item, Row & row);
  static QLabel * makeRangeHint(const marine_control_interfaces::msg::ControlItem & item);
  GroupSection & sectionFor(const std::string & group);
  static QString displayValue(const marine_control_interfaces::msg::ControlItem & item);

  QVBoxLayout * vbox_;
  // Sections keyed by the raw group string ("" -> the "General" section).
  std::map<std::string, GroupSection> sections_;
  std::vector<std::string> section_order_;   // first-seen order, drives layout order
  std::map<std::string, Row> rows_;
};

}  // namespace marine_control_widgets

#endif  // MARINE_CONTROL_WIDGETS__CONTROL_SET_WIDGET_HPP_
