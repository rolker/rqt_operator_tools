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
#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

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

// The "group" a control belongs to, with empty mapped to the default section.
const char * const kDefaultGroup = "General";

// Bounds are meaningful (worth showing) only for numeric controls whose advertised
// max exceeds min; an unbounded control leaves these unset.
bool hasMeaningfulBounds(const Item & item)
{
  return (item.type == Item::TYPE_FLOAT || item.type == Item::TYPE_INT) &&
         item.max_value > item.min_value;
}

// Format min/max (and step) using the control's natural precision: integers for
// INT, decimalsFor() places for FLOAT.
void formatBounds(const Item & item, QString & lo, QString & hi, QString & step)
{
  if (item.type == Item::TYPE_INT) {
    lo = QString::number(toIntBound(item.min_value));
    hi = QString::number(toIntBound(item.max_value));
    step = item.step > 0.0 ? QString::number(toIntBound(item.step)) : QString();
  } else {
    const int dec = decimalsFor(item);
    lo = QString::number(item.min_value, 'f', dec);
    hi = QString::number(item.max_value, 'f', dec);
    step = item.step > 0.0 ? QString::number(item.step, 'f', dec) : QString();
  }
}

// Compact inline hint, e.g. "[0.0 – 100.0 m]". Empty when bounds aren't meaningful.
QString rangeLabelText(const Item & item)
{
  if (!hasMeaningfulBounds(item)) {
    return QString();
  }
  QString lo, hi, step;
  formatBounds(item, lo, hi, step);
  const QChar dash(0x2013);   // en dash; kept out of the source as a literal
  QString text = QStringLiteral("[") + lo + QStringLiteral(" ") + dash +
    QStringLiteral(" ") + hi;
  if (!item.units.empty()) {
    text += QStringLiteral(" ") + QString::fromStdString(item.units);
  }
  return text + QStringLiteral("]");
}

// Full detail for a tooltip, e.g. "Range: 0.0 – 100.0 m, step 0.5". Empty when
// bounds aren't meaningful.
QString rangeDetailText(const Item & item)
{
  if (!hasMeaningfulBounds(item)) {
    return QString();
  }
  QString lo, hi, step;
  formatBounds(item, lo, hi, step);
  const QChar dash(0x2013);
  QString text = QStringLiteral("Range: ") + lo + QStringLiteral(" ") + dash +
    QStringLiteral(" ") + hi;
  if (!item.units.empty()) {
    text += QStringLiteral(" ") + QString::fromStdString(item.units);
  }
  if (!step.isEmpty()) {
    text += QStringLiteral(", step ") + step;
  }
  return text;
}
}  // namespace

ControlSetWidget::ControlSetWidget(QWidget * parent)
: QWidget(parent)
{
  vbox_ = new QVBoxLayout(this);
  vbox_->setContentsMargins(4, 2, 4, 2);
  // Sections stack from the top; the trailing stretch keeps them packed up so a
  // device with few controls doesn't spread its rows down a tall dock.
  vbox_->addStretch(1);
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
        if (const QString tip = rangeDetailText(item); !tip.isEmpty()) {
          spin->setToolTip(tip);
        }
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
        if (const QString tip = rangeDetailText(item); !tip.isEmpty()) {
          spin->setToolTip(tip);
        }
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

QLabel * ControlSetWidget::makeRangeHint(const Item & item)
{
  const QString text = rangeLabelText(item);
  if (text.isEmpty()) {
    return nullptr;     // unbounded control: no hint
  }
  auto * hint = new QLabel(text);
  if (const QString tip = rangeDetailText(item); !tip.isEmpty()) {
    hint->setToolTip(tip);   // full detail (incl. step) on hover
  }
  return hint;
}

ControlSetWidget::GroupSection & ControlSetWidget::sectionFor(const std::string & group)
{
  auto it = sections_.find(group);
  if (it != sections_.end()) {
    return it->second;
  }
  GroupSection section;
  const QString title =
    QString::fromStdString(group.empty() ? std::string(kDefaultGroup) : group);
  section.header = new QLabel(title);
  QFont f = section.header->font();
  f.setBold(true);
  section.header->setFont(f);
  section.grid = new QGridLayout();
  section.grid->setContentsMargins(0, 0, 0, 0);

  // Record the group in persistent first-seen order the first time it is ever
  // seen; it is never removed until clear(), so a group that empties and later
  // reappears keeps its original slot.
  if (std::find(group_first_seen_.begin(), group_first_seen_.end(), group) ==
    group_first_seen_.end())
  {
    group_first_seen_.push_back(group);
  }
  // Insert this section's header/grid pair at the layout slot dictated by the
  // persistent order: after every live section that precedes it in first-seen
  // order. Each live section contributes two layout items (header + grid); the
  // trailing stretch stays last. This is what keeps a re-created section from
  // landing at the end.
  int preceding = 0;
  for (const auto & g : group_first_seen_) {
    if (g == group) {
      break;
    }
    if (sections_.count(g) != 0) {
      ++preceding;
    }
  }
  const int slot = preceding * 2;
  vbox_->insertWidget(slot, section.header);
  vbox_->insertLayout(slot + 1, section.grid);
  auto [pos, inserted] = sections_.emplace(group, section);
  (void)inserted;
  return pos->second;
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
    row.range_hint = makeRangeHint(item);

    row.group = item.group;
    GroupSection & section = sectionFor(item.group);
    const int r = section.row_count++;
    row.grid_row = r;
    section.grid->addWidget(row.name, r, 0);
    section.grid->addWidget(row.value, r, 1);
    if (row.input) {
      section.grid->addWidget(row.input, r, 2);
    }
    if (row.range_hint) {
      section.grid->addWidget(row.range_hint, r, 3);
    }
    rows_[item.name] = row;
  }

  // Reconcile: a control dropped from this heartbeat must not linger as a stale
  // row. Delete any row whose name is absent from the incoming set (same widget
  // teardown as clear()).
  std::set<std::string> incoming;
  for (const auto & item : set.items) {
    incoming.insert(item.name);
  }
  bool removed_any = false;
  for (auto it = rows_.begin(); it != rows_.end(); ) {
    if (incoming.count(it->first) == 0) {
      delete it->second.name;
      delete it->second.value;
      delete it->second.input;
      delete it->second.range_hint;
      it = rows_.erase(it);
      removed_any = true;
    } else {
      ++it;
    }
  }

  if (removed_any) {
    // Re-pack each surviving section so freed grid rows are reclaimed: the
    // per-section row count then tracks the live row count instead of growing
    // monotonically, so a control that repeatedly drops and re-appears can't grow
    // the grid without bound. Surviving rows are gathered in their current grid
    // order and moved (never recreated) into consecutive rows 0..n-1, preserving
    // their relative order and keeping no-op-edit suppression / focus / read-only
    // state intact. A re-added control was appended at the bottom above, so it
    // sorts last and stays below the rows that survived.
    std::map<std::string, std::vector<std::pair<int, std::string>>> by_section;
    for (const auto & [name, row] : rows_) {
      by_section[row.group].push_back({row.grid_row, name});
    }
    for (auto & [group, ordered] : by_section) {
      std::sort(ordered.begin(), ordered.end());
      GroupSection & section = sections_.at(group);
      int r = 0;
      for (const auto & [old_row, name] : ordered) {
        Row & row = rows_.at(name);
        if (row.grid_row != r) {
          section.grid->addWidget(row.name, r, 0);
          section.grid->addWidget(row.value, r, 1);
          if (row.input) {
            section.grid->addWidget(row.input, r, 2);
          }
          if (row.range_hint) {
            section.grid->addWidget(row.range_hint, r, 3);
          }
          row.grid_row = r;
        }
        ++r;
      }
      section.row_count = r;
    }

    // Drop any section left with no rows so a removed group doesn't leave an
    // orphaned header (and the single-section header rule below stays correct).
    // group_first_seen_ is NOT touched — it persists so the group returns to its
    // original slot if it reappears.
    std::set<std::string> live_groups;
    for (const auto & [name, row] : rows_) {
      live_groups.insert(row.group);
    }
    for (auto it = sections_.begin(); it != sections_.end(); ) {
      if (live_groups.count(it->first) == 0) {
        delete it->second.header;
        delete it->second.grid;
        it = sections_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // Hide the header while only one section exists, so an ungrouped set renders
  // as a flat grid (unchanged from before grouping). Reveal all headers — incl.
  // "General" — as soon as a second, named section appears.
  const bool show_headers = sections_.size() > 1;
  for (auto & [group, section] : sections_) {
    section.header->setVisible(show_headers);
  }
}

void ControlSetWidget::clear()
{
  for (auto & [name, row] : rows_) {
    delete row.name;
    delete row.value;
    delete row.input;
    delete row.range_hint;
  }
  rows_.clear();
  for (auto & [group, section] : sections_) {
    delete section.header;
    delete section.grid;
  }
  sections_.clear();
  group_first_seen_.clear();
}

int ControlSetWidget::rowCount() const
{
  return static_cast<int>(rows_.size());
}

int ControlSetWidget::sectionCount() const
{
  return static_cast<int>(sections_.size());
}

std::vector<std::string> ControlSetWidget::sectionOrder() const
{
  // Live sections only, in persistent first-seen order. A group that emptied is
  // absent from sections_ and so drops out here, but keeps its slot in
  // group_first_seen_ for when it reappears.
  std::vector<std::string> order;
  for (const auto & group : group_first_seen_) {
    if (sections_.count(group) != 0) {
      order.push_back(group);
    }
  }
  return order;
}

int ControlSetWidget::sectionRowCount(const std::string & group) const
{
  auto it = sections_.find(group);
  return it == sections_.end() ? 0 : it->second.row_count;
}

int ControlSetWidget::gridRowOf(const std::string & name) const
{
  auto rit = rows_.find(name);
  if (rit == rows_.end()) {
    return -1;
  }
  auto sit = sections_.find(rit->second.group);
  if (sit == sections_.end()) {
    return -1;
  }
  // Read the actual grid position of the row's name label, so a test sees the
  // real Qt layout (the genuine proof rows stay packed), not just our bookkeeping.
  const int idx = sit->second.grid->indexOf(rit->second.name);
  if (idx < 0) {
    return -1;
  }
  int row = -1, col = 0, row_span = 0, col_span = 0;
  sit->second.grid->getItemPosition(idx, &row, &col, &row_span, &col_span);
  return row;
}

QString ControlSetWidget::rangeHintText(const std::string & name) const
{
  auto it = rows_.find(name);
  if (it == rows_.end() || it->second.range_hint == nullptr) {
    return QString();
  }
  return it->second.range_hint->text();
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
