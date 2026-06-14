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
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string>

namespace marine_control_widgets
{

namespace
{
using Item = marine_control_interfaces::msg::ControlItem;

// A stringified value parses to "true" for the common truthy spellings,
// case-insensitively (a device may echo "TRUE"/"On"/"Yes").
bool parseBool(const std::string & value)
{
  QString v = QString::fromStdString(value).trimmed().toLower();
  return v == "true" || v == "1" || v == "on" || v == "yes";
}

// Decimal places a QDoubleSpinBox needs so it can represent values without
// silently rounding (which would round-trip a coarser value back to the
// device). Prefer the control's step; otherwise match the current value's own
// precision. Clamped to a sane [2, 6].
int decimalsFor(const Item & item)
{
  if (item.step > 0.0 && item.step < 1.0) {
    return std::clamp(static_cast<int>(std::ceil(-std::log10(item.step))), 2, 6);
  }
  const std::string & v = item.value;
  const auto dot = v.find('.');
  const int from_value = (dot == std::string::npos) ?
    0 : static_cast<int>(v.size() - dot - 1);
  return std::clamp(from_value, 2, 6);
}

// Clamp a float64 bound to the int range before narrowing (a device could
// advertise INT bounds beyond INT_MAX; the raw cast would be UB).
int toIntBound(double v)
{
  const double lo = static_cast<double>(std::numeric_limits<int>::min());
  const double hi = static_cast<double>(std::numeric_limits<int>::max());
  return static_cast<int>(std::clamp(v, lo, hi));
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

  // Last value applied from the device (or last published). An edit publishes
  // only when it differs, so a no-op focus-out / re-select doesn't spam the
  // change topic (and the ADR-0003 D8.3 audit) with redundant commands.
  auto last = std::make_shared<QString>(QString::fromStdString(item.value));

  switch (item.type) {
    case Item::TYPE_FLOAT: {
        auto * spin = new QDoubleSpinBox();
        if (item.max_value > item.min_value) {
          spin->setRange(item.min_value, item.max_value);
        } else {
          spin->setRange(-1.0e9, 1.0e9);   // unbounded control: wide range
        }
        spin->setDecimals(decimalsFor(item));
        spin->setSingleStep(item.step > 0.0 ? item.step : 0.1);
        if (!item.units.empty()) {
          spin->setSuffix(QStringLiteral(" ") + QString::fromStdString(item.units));
        }
        spin->setKeyboardTracking(false);   // emit only on commit, not each digit
        spin->setValue(QString::fromStdString(item.value).toDouble());
        connect(
          spin, &QAbstractSpinBox::editingFinished, this,
          [this, name, spin, last]() {
            const QString v = QString::number(spin->value());
            if (v != *last) {*last = v; emit controlChanged(name, v);}
          });
        row.input = spin;
        row.set_value = [spin, last](const std::string & value) {
            *last = QString::fromStdString(value);
            if (!spin->hasFocus()) {
              const QSignalBlocker block(spin);
              spin->setValue(last->toDouble());
            }
          };
        return;
      }
    case Item::TYPE_INT: {
        auto * spin = new QSpinBox();
        if (item.max_value > item.min_value) {
          spin->setRange(toIntBound(item.min_value), toIntBound(item.max_value));
        } else {
          spin->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        }
        spin->setSingleStep(item.step > 0.0 ? toIntBound(item.step) : 1);
        if (!item.units.empty()) {
          spin->setSuffix(QStringLiteral(" ") + QString::fromStdString(item.units));
        }
        spin->setKeyboardTracking(false);
        spin->setValue(QString::fromStdString(item.value).toInt());
        connect(
          spin, &QAbstractSpinBox::editingFinished, this,
          [this, name, spin, last]() {
            const QString v = QString::number(spin->value());
            if (v != *last) {*last = v; emit controlChanged(name, v);}
          });
        row.input = spin;
        row.set_value = [spin, last](const std::string & value) {
            *last = QString::fromStdString(value);
            if (!spin->hasFocus()) {
              const QSignalBlocker block(spin);
              spin->setValue(last->toInt());
            }
          };
        return;
      }
    case Item::TYPE_BOOL: {
        auto * check = new QCheckBox();
        check->setChecked(parseBool(item.value));
        connect(
          check, &QCheckBox::toggled, this,
          [this, name, last](bool checked) {
            const QString v = checked ? QStringLiteral("true") : QStringLiteral("false");
            if (v != *last) {*last = v; emit controlChanged(name, v);}
          });
        row.input = check;
        row.set_value = [check, last](const std::string & value) {
            // Canonicalize so an echoed "True"/"on" doesn't read as a change.
            *last = parseBool(value) ? QStringLiteral("true") : QStringLiteral("false");
            // setChecked emits toggled(); block it so a device refresh never
            // re-publishes the value back as a change.
            if (!check->hasFocus()) {
              const QSignalBlocker block(check);
              check->setChecked(*last == QStringLiteral("true"));
            }
          };
        return;
      }
    case Item::TYPE_ENUM: {
        auto * combo = new QComboBox();
        for (const auto & choice : item.enums) {
          combo->addItem(QString::fromStdString(choice));
        }
        // Show the device value even if it's not one of the advertised choices,
        // so the combo never silently disagrees with the value label.
        if (combo->findText(QString::fromStdString(item.value)) < 0) {
          combo->addItem(QString::fromStdString(item.value));
        }
        combo->setCurrentText(QString::fromStdString(item.value));
        connect(
          combo, QOverload<int>::of(&QComboBox::activated), this,
          [this, name, combo, last](int index) {
            const QString v = combo->itemText(index);
            if (v != *last) {*last = v; emit controlChanged(name, v);}
          });
        row.input = combo;
        row.set_value = [combo, last](const std::string & value) {
            *last = QString::fromStdString(value);
            if (!combo->hasFocus()) {
              if (combo->findText(*last) < 0) {
                combo->addItem(*last);
              }
              // setCurrentText does not emit activated(), so no spurious publish.
              combo->setCurrentText(*last);
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
          [this, name, edit, last]() {
            const QString v = edit->text();
            if (v != *last) {*last = v; emit controlChanged(name, v);}
          });
        row.input = edit;
        row.set_value = [edit, last](const std::string & value) {
            *last = QString::fromStdString(value);
            if (!edit->hasFocus()) {
              edit->setText(*last);
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
