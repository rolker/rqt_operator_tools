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

#include "marine_control_widgets/control_set_widget.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>

#include <limits>
#include <map>
#include <string>

namespace marine_control_widgets
{

namespace
{
using Item = marine_control_interfaces::msg::ControlItem;

// A stringified value parses to "true" for the common truthy spellings.
bool parseBool(const std::string & value)
{
  return value == "true" || value == "1" || value == "on" || value == "True";
}
}  // namespace

ControlSetWidget::ControlSetWidget(QWidget * parent)
: QWidget(parent)
{
  grid_ = new QGridLayout(this);
  grid_->setContentsMargins(4, 2, 4, 2);
}

ControlSetWidget::~ControlSetWidget() = default;

QString ControlSetWidget::displayValue(const Item & item)
{
  QString text = QString::fromStdString(item.value);
  if (!item.units.empty()) {
    text += QStringLiteral(" ") + QString::fromStdString(item.units);
  }
  return text;
}

void ControlSetWidget::makeInput(const Item & item, Row & row)
{
  const QString name = QString::fromStdString(item.name);

  // Read-only (or unknown type): no input widget; the value label carries it.
  if (item.read_only) {
    row.input = nullptr;
    row.set_value = nullptr;
    return;
  }

  switch (item.type) {
    case Item::TYPE_FLOAT: {
        auto * spin = new QDoubleSpinBox();
        if (item.max_value > item.min_value) {
          spin->setRange(item.min_value, item.max_value);
        } else {
          spin->setRange(-1.0e9, 1.0e9);   // unbounded control: wide range
        }
        spin->setSingleStep(item.step > 0.0 ? item.step : 0.1);
        // Decimals from the step's magnitude so the field can represent it.
        spin->setDecimals(item.step > 0.0 && item.step < 1.0 ? 3 : 2);
        if (!item.units.empty()) {
          spin->setSuffix(QStringLiteral(" ") + QString::fromStdString(item.units));
        }
        spin->setKeyboardTracking(false);   // emit only on commit, not each digit
        spin->setValue(QString::fromStdString(item.value).toDouble());
        connect(
          spin, &QAbstractSpinBox::editingFinished, this,
          [this, name, spin]() {emit controlChanged(name, QString::number(spin->value()));});
        row.input = spin;
        row.set_value = [spin](const std::string & value) {
            if (!spin->hasFocus()) {
              const QSignalBlocker block(spin);
              spin->setValue(QString::fromStdString(value).toDouble());
            }
          };
        return;
      }
    case Item::TYPE_INT: {
        auto * spin = new QSpinBox();
        if (item.max_value > item.min_value) {
          spin->setRange(static_cast<int>(item.min_value), static_cast<int>(item.max_value));
        } else {
          spin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        }
        spin->setSingleStep(item.step > 0.0 ? static_cast<int>(item.step) : 1);
        if (!item.units.empty()) {
          spin->setSuffix(QStringLiteral(" ") + QString::fromStdString(item.units));
        }
        spin->setKeyboardTracking(false);
        spin->setValue(QString::fromStdString(item.value).toInt());
        connect(
          spin, &QAbstractSpinBox::editingFinished, this,
          [this, name, spin]() {emit controlChanged(name, QString::number(spin->value()));});
        row.input = spin;
        row.set_value = [spin](const std::string & value) {
            if (!spin->hasFocus()) {
              const QSignalBlocker block(spin);
              spin->setValue(QString::fromStdString(value).toInt());
            }
          };
        return;
      }
    case Item::TYPE_BOOL: {
        auto * check = new QCheckBox();
        check->setChecked(parseBool(item.value));
        connect(
          check, &QCheckBox::toggled, this,
          [this, name](bool checked) {
            emit controlChanged(name, checked ? QStringLiteral("true") : QStringLiteral("false"));
          });
        row.input = check;
        row.set_value = [check](const std::string & value) {
            // setChecked emits toggled(); block it so a device refresh never
            // re-publishes the value back as a change.
            if (!check->hasFocus()) {
              const QSignalBlocker block(check);
              check->setChecked(parseBool(value));
            }
          };
        return;
      }
    case Item::TYPE_ENUM: {
        auto * combo = new QComboBox();
        for (const auto & choice : item.enums) {
          combo->addItem(QString::fromStdString(choice));
        }
        combo->setCurrentText(QString::fromStdString(item.value));
        connect(
          combo, QOverload<int>::of(&QComboBox::activated), this,
          [this, name, combo](int index) {emit controlChanged(name, combo->itemText(index));});
        row.input = combo;
        row.set_value = [combo](const std::string & value) {
            // setCurrentText does not emit activated(), so no spurious publish.
            if (!combo->hasFocus()) {
              combo->setCurrentText(QString::fromStdString(value));
            }
          };
        return;
      }
    case Item::TYPE_STRING:
    default: {
        auto * edit = new QLineEdit();
        edit->setText(QString::fromStdString(item.value));
        connect(
          edit, &QLineEdit::editingFinished, this,
          [this, name, edit]() {emit controlChanged(name, edit->text());});
        row.input = edit;
        row.set_value = [edit](const std::string & value) {
            if (!edit->hasFocus()) {
              edit->setText(QString::fromStdString(value));
            }
          };
        return;
      }
  }
}

void ControlSetWidget::apply(const marine_control_interfaces::msg::ControlSet & set)
{
  for (const auto & item : set.items) {
    auto it = rows_.find(item.name);
    if (it != rows_.end()) {
      it->second.value->setText(displayValue(item));
      if (it->second.set_value) {
        it->second.set_value(item.value);
      }
      continue;
    }
    Row row;
    const QString label =
      QString::fromStdString(item.label.empty() ? item.name : item.label);
    row.name = new QLabel(label);
    if (!item.description.empty()) {
      row.name->setToolTip(QString::fromStdString(item.description));
    }
    row.value = new QLabel(displayValue(item));
    makeInput(item, row);
    // Rows are only appended (or cleared wholesale), so size == next free row.
    const int r = static_cast<int>(rows_.size());
    grid_->addWidget(row.name, r, 0);
    grid_->addWidget(row.value, r, 1);
    if (row.input) {
      grid_->addWidget(row.input, r, 2);
    }
    rows_[item.name] = row;
  }
}

void ControlSetWidget::clear()
{
  for (auto & [name, row] : rows_) {
    delete row.name;
    delete row.value;
    delete row.input;
  }
  rows_.clear();
}

int ControlSetWidget::rowCount() const
{
  return static_cast<int>(rows_.size());
}

QString ControlSetWidget::valueText(const std::string & name) const
{
  auto it = rows_.find(name);
  return it == rows_.end() ? QString() : it->second.value->text();
}

QWidget * ControlSetWidget::inputFor(const std::string & name) const
{
  auto it = rows_.find(name);
  return it == rows_.end() ? nullptr : it->second.input;
}

}  // namespace marine_control_widgets
