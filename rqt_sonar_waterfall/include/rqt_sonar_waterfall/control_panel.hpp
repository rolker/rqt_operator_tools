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

#ifndef RQT_SONAR_WATERFALL__CONTROL_PANEL_HPP_
#define RQT_SONAR_WATERFALL__CONTROL_PANEL_HPP_

#include <QString>  // NOLINT(build/include_order)
#include <QWidget>

#include <functional>
#include <map>
#include <string>

#include <marine_radar_control_msgs/msg/radar_control_set.hpp>

class QGridLayout;
class QLabel;

namespace rqt_sonar_waterfall
{

/// Dynamic control panel for a sonar that advertises a RadarControlSet.
///
/// Ported from rqt_marine_radar: a widget is created the first time a control
/// name is seen (FLOAT -> line edit, FLOAT_WITH_AUTO -> line edit + auto button,
/// ENUM -> combo box); subsequent updates refresh the displayed value and the
/// input widget unless it is focused, so a periodic state stream never stomps a
/// control the operator is editing. Editing
/// a control emits controlChanged(key, value); the owner turns that into a
/// RadarControlValue on the device's change-state topic. The panel itself does
/// no ROS I/O, so it is unit-testable without a node.
class ControlPanel : public QWidget
{
  Q_OBJECT

public:
  explicit ControlPanel(QWidget * parent = nullptr);
  ~ControlPanel() override;

  /// Create missing controls and refresh the displayed values for the rest.
  void apply(const marine_radar_control_msgs::msg::RadarControlSet & set);

  /// Remove all controls (e.g. when the selected control topic changes).
  void clear();

  // --- introspection (for tests) ---
  int row_count() const;
  QString value_text(const std::string & name) const;
  QWidget * input_for(const std::string & name) const;

signals:
  void controlChanged(const QString & key, const QString & value);

private:
  struct Row
  {
    QLabel * name = nullptr;
    QLabel * value = nullptr;
    QWidget * input = nullptr;
    // Refreshes the input widget from a device value, skipping when the widget
    // is focused so an in-progress edit is never stomped. Set by make_input().
    std::function<void(const std::string &)> set_value;
  };

  void make_input(
    const marine_radar_control_msgs::msg::RadarControlItem & item, Row & row);

  QGridLayout * grid_;
  std::map<std::string, Row> rows_;
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__CONTROL_PANEL_HPP_
