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

#include "rqt_sonar_waterfall/control_panel.hpp"

#include <QComboBox>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

#include <map>
#include <string>

#include <marine_radar_control_msgs/msg/radar_control_item.hpp>

namespace rqt_sonar_waterfall
{

ControlPanel::ControlPanel(QWidget * parent)
: QWidget(parent)
{
  grid_ = new QGridLayout(this);
  grid_->setContentsMargins(4, 2, 4, 2);
}

ControlPanel::~ControlPanel() = default;

QWidget * ControlPanel::make_input(
  const marine_radar_control_msgs::msg::RadarControlItem & item)
{
  using Item = marine_radar_control_msgs::msg::RadarControlItem;
  const QString key = QString::fromStdString(item.name);

  switch (item.type) {
    case Item::CONTROL_TYPE_FLOAT: {
        auto * edit = new QLineEdit();
        edit->setMaximumWidth(100);
        auto * validator = new QDoubleValidator(edit);
        if (item.max_value > item.min_value) {
          validator->setRange(item.min_value, item.max_value, 2);
        }
        edit->setValidator(validator);
        edit->setToolTip(
          tr("Range: %1 to %2").arg(item.min_value).arg(item.max_value));
        // Seed with the current value so the field reflects device state and a
        // stray editingFinished can't publish an empty string.
        edit->setText(QString::fromStdString(item.value));
        connect(
          edit, &QLineEdit::editingFinished, this,
          [this, key, edit]() {emit controlChanged(key, edit->text());});
        return edit;
      }
    case Item::CONTROL_TYPE_FLOAT_WITH_AUTO: {
        auto * container = new QWidget();
        auto * layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        auto * edit = new QLineEdit();
        edit->setMaximumWidth(60);
        auto * validator = new QDoubleValidator(edit);
        if (item.max_value > item.min_value) {
          validator->setRange(item.min_value, item.max_value, 2);
        }
        edit->setValidator(validator);
        // Seed numeric state; an "auto" value lives on the button, not the field.
        if (item.value != "auto") {
          edit->setText(QString::fromStdString(item.value));
        }
        connect(
          edit, &QLineEdit::editingFinished, this,
          [this, key, edit]() {emit controlChanged(key, edit->text());});
        layout->addWidget(edit);
        auto * auto_button = new QPushButton(tr("auto"));
        auto_button->setMaximumWidth(40);
        connect(
          auto_button, &QPushButton::clicked, this,
          [this, key]() {emit controlChanged(key, QStringLiteral("auto"));});
        layout->addWidget(auto_button);
        return container;
      }
    case Item::CONTROL_TYPE_ENUM: {
        auto * combo = new QComboBox();
        for (const auto & value : item.enums) {
          combo->addItem(QString::fromStdString(value));
        }
        // Select the current value (no-op if absent on a non-editable combo);
        // does not emit activated(), so it can't trigger a spurious publish.
        combo->setCurrentText(QString::fromStdString(item.value));
        combo->setMaximumWidth(120);
        connect(
          combo, QOverload<int>::of(&QComboBox::activated), this,
          [this, key, combo](int index) {emit controlChanged(key, combo->itemText(index));});
        return combo;
      }
    default:
      // Unknown control type: show the value read-only.
      return new QLabel(QString::fromStdString(item.value));
  }
}

void ControlPanel::apply(
  const marine_radar_control_msgs::msg::RadarControlSet & set)
{
  for (const auto & item : set.items) {
    auto it = rows_.find(item.name);
    if (it != rows_.end()) {
      it->second.value->setText(QString::fromStdString(item.value));
      continue;
    }
    Row row;
    const QString label =
      QString::fromStdString(item.label.empty() ? item.name : item.label);
    row.name = new QLabel(label);
    row.value = new QLabel(QString::fromStdString(item.value));
    row.input = make_input(item);
    // Rows are only appended (or cleared wholesale), so size == next free row.
    const int r = static_cast<int>(rows_.size());
    grid_->addWidget(row.name, r, 0);
    grid_->addWidget(row.value, r, 1);
    grid_->addWidget(row.input, r, 2);
    rows_[item.name] = row;
  }
}

void ControlPanel::clear()
{
  for (auto & [name, row] : rows_) {
    delete row.name;
    delete row.value;
    delete row.input;
  }
  rows_.clear();
}

int ControlPanel::row_count() const
{
  return static_cast<int>(rows_.size());
}

QString ControlPanel::value_text(const std::string & name) const
{
  auto it = rows_.find(name);
  return it == rows_.end() ? QString() : it->second.value->text();
}

QWidget * ControlPanel::input_for(const std::string & name) const
{
  auto it = rows_.find(name);
  return it == rows_.end() ? nullptr : it->second.input;
}

}  // namespace rqt_sonar_waterfall
