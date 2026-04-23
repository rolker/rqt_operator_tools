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

#include "rqt_camera_grid/config_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace rqt_camera_grid
{

namespace
{
const std::vector<QString> kTransports{
  "raw", "compressed", "compressedDepth", "theora", "ffmpeg"};
}

ConfigDialog::ConfigDialog(
  rclcpp::Node::SharedPtr node,
  const GridConfig & initial,
  QWidget * parent)
: QDialog(parent),
  node_(std::move(node)),
  config_(initial)
{
  setWindowTitle("Camera Grid Settings");
  setMinimumSize(600, 400);

  auto * main = new QVBoxLayout(this);

  // Grid dims.
  auto * dims = new QHBoxLayout();
  dims->addWidget(new QLabel("Grid:"));
  rows_spin_ = new QSpinBox();
  rows_spin_->setRange(1, 16);
  rows_spin_->setValue(config_.rows);
  dims->addWidget(new QLabel("rows"));
  dims->addWidget(rows_spin_);
  cols_spin_ = new QSpinBox();
  cols_spin_->setRange(1, 16);
  cols_spin_->setValue(config_.cols);
  dims->addWidget(new QLabel("cols"));
  dims->addWidget(cols_spin_);
  dims->addStretch();
  main->addLayout(dims);

  connect(rows_spin_,
          QOverload<int>::of(&QSpinBox::valueChanged),
          this, &ConfigDialog::onRowsChanged);
  connect(cols_spin_,
          QOverload<int>::of(&QSpinBox::valueChanged),
          this, &ConfigDialog::onColsChanged);

  auto * body = new QHBoxLayout();

  // Left: pane list + add/remove.
  auto * left = new QVBoxLayout();
  list_ = new QListWidget();
  left->addWidget(list_);
  auto * list_btns = new QHBoxLayout();
  auto * add_btn = new QPushButton("+");
  add_btn->setFixedWidth(30);
  auto * rm_btn = new QPushButton("-");
  rm_btn->setFixedWidth(30);
  list_btns->addWidget(add_btn);
  list_btns->addWidget(rm_btn);
  list_btns->addStretch();
  left->addLayout(list_btns);
  body->addLayout(left, 1);

  connect(list_, &QListWidget::currentRowChanged,
          this, &ConfigDialog::onSelectionChanged);
  connect(add_btn, &QPushButton::clicked, this, &ConfigDialog::onAddPane);
  connect(rm_btn, &QPushButton::clicked, this, &ConfigDialog::onRemovePane);

  // Right: pane editor.
  auto * right_form = new QFormLayout();
  base_combo_ = new QComboBox();
  base_combo_->setEditable(true);
  right_form->addRow("Base topic:", base_combo_);
  transport_combo_ = new QComboBox();
  for (const auto & t : kTransports) {
    transport_combo_->addItem(t);
  }
  right_form->addRow("Transport:", transport_combo_);
  warn_spin_ = new QDoubleSpinBox();
  warn_spin_->setRange(0.0, 300.0);
  warn_spin_->setSuffix(" s");
  right_form->addRow("Warn threshold:", warn_spin_);
  error_spin_ = new QDoubleSpinBox();
  error_spin_->setRange(0.0, 600.0);
  error_spin_->setSuffix(" s");
  right_form->addRow("Error threshold:", error_spin_);
  body->addLayout(right_form, 2);
  main->addLayout(body);

  connect(base_combo_,
          QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ConfigDialog::onBaseEditChanged);

  // Import / Export.
  auto * io = new QHBoxLayout();
  auto * import_btn = new QPushButton("Import YAML...");
  auto * export_btn = new QPushButton("Export YAML...");
  io->addWidget(import_btn);
  io->addWidget(export_btn);
  io->addStretch();
  main->addLayout(io);
  connect(import_btn, &QPushButton::clicked, this, &ConfigDialog::onImportYaml);
  connect(export_btn, &QPushButton::clicked, this, &ConfigDialog::onExportYaml);

  // OK / Cancel.
  auto * buttons = new QDialogButtonBox(
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  main->addWidget(buttons);

  // Seed pane list + topic combo.
  populate_topic_combo();
  resize_panes(config_, config_.rows, config_.cols);
  refresh_list();
  if (!config_.panes.empty()) {
    list_->setCurrentRow(0);
  }
}

void ConfigDialog::populate_topic_combo()
{
  base_combo_->clear();
  base_combo_->addItem("");  // leave-empty option
  try {
    auto topic_types = node_->get_topic_names_and_types();
    std::map<std::string, std::vector<std::string>> m(
      topic_types.begin(), topic_types.end());
    const auto pairs = parse_image_topics(m);
    for (const auto & [base, transport] : pairs) {
      const QString label =
        QString::fromStdString(base) + "  [" + QString::fromStdString(transport) + "]";
      base_combo_->addItem(label, QString::fromStdString(base));
    }
  } catch (const std::exception & e) {
    RCLCPP_WARN(
      node_->get_logger(),
      "failed to enumerate topics for dialog: %s", e.what());
  }
}

void ConfigDialog::onBaseEditChanged(int index)
{
  if (index <= 0) {return;}  // 0 is the empty leave-alone entry
  const QString base = base_combo_->itemData(index).toString();
  if (!base.isEmpty()) {
    base_combo_->setEditText(base);
    // Auto-select the transport hinted by the combo label.
    const QString label = base_combo_->itemText(index);
    for (const auto & t : kTransports) {
      if (label.contains("[" + t + "]")) {
        transport_combo_->setCurrentText(t);
        break;
      }
    }
  }
}

void ConfigDialog::onRowsChanged(int value)
{
  save_current_editor_to_config();
  resize_panes(config_, value, config_.cols);
  refresh_list();
  const int new_row = std::min<int>(current_row_, static_cast<int>(config_.panes.size()) - 1);
  if (new_row >= 0) {list_->setCurrentRow(new_row);}
}

void ConfigDialog::onColsChanged(int value)
{
  save_current_editor_to_config();
  resize_panes(config_, config_.rows, value);
  refresh_list();
  const int new_row = std::min<int>(current_row_, static_cast<int>(config_.panes.size()) - 1);
  if (new_row >= 0) {list_->setCurrentRow(new_row);}
}

void ConfigDialog::onSelectionChanged(int row)
{
  save_current_editor_to_config();
  current_row_ = row;
  load_editor_from_config(row);
}

void ConfigDialog::onAddPane()
{
  save_current_editor_to_config();
  config_.panes.push_back(PaneConfig{});
  // Grow the grid if we've overrun rows*cols, so the new pane is visible
  // after OK rather than silently truncated by CameraGridWidget::build_panes.
  // Prefer growing cols first (usually more screen width than height).
  const int needed = static_cast<int>(config_.panes.size());
  while (config_.rows * config_.cols < needed) {
    if (config_.cols <= config_.rows) {
      config_.cols += 1;
    } else {
      config_.rows += 1;
    }
  }
  rows_spin_->blockSignals(true);
  cols_spin_->blockSignals(true);
  rows_spin_->setValue(config_.rows);
  cols_spin_->setValue(config_.cols);
  rows_spin_->blockSignals(false);
  cols_spin_->blockSignals(false);
  refresh_list();
  list_->setCurrentRow(static_cast<int>(config_.panes.size()) - 1);
}

void ConfigDialog::onRemovePane()
{
  if (config_.panes.empty()) {return;}
  int row = list_->currentRow();
  if (row < 0) {return;}
  current_row_ = -1;  // prevent save to about-to-delete slot
  config_.panes.erase(config_.panes.begin() + row);
  refresh_list();
  if (!config_.panes.empty()) {
    list_->setCurrentRow(std::min<int>(row, static_cast<int>(config_.panes.size()) - 1));
  }
}

void ConfigDialog::onImportYaml()
{
  const QString path = QFileDialog::getOpenFileName(
    this, "Import YAML", {}, "YAML files (*.yaml *.yml)");
  if (path.isEmpty()) {return;}
  try {
    GridConfig loaded = config_from_file(path.toStdString());
    config_ = loaded;
    // Prevent the setCurrentRow(0) below from firing onSelectionChanged,
    // which would call save_current_editor_to_config() with the pre-import
    // current_row_ and the editor's stale values and overwrite the freshly
    // imported config_.panes[old_row_].
    current_row_ = -1;
    rows_spin_->blockSignals(true);
    cols_spin_->blockSignals(true);
    rows_spin_->setValue(config_.rows);
    cols_spin_->setValue(config_.cols);
    rows_spin_->blockSignals(false);
    cols_spin_->blockSignals(false);
    refresh_list();
    if (!config_.panes.empty()) {list_->setCurrentRow(0);}
  } catch (const ConfigParseError & e) {
    QMessageBox::warning(
      this, "Import error", QString::fromStdString(e.what()));
  }
}

void ConfigDialog::onExportYaml()
{
  save_current_editor_to_config();
  const QString path = QFileDialog::getSaveFileName(
    this, "Export YAML", {}, "YAML files (*.yaml *.yml)");
  if (path.isEmpty()) {return;}
  try {
    config_to_file(config_, path.toStdString());
  } catch (const ConfigParseError & e) {
    QMessageBox::warning(
      this, "Export error", QString::fromStdString(e.what()));
  }
}

void ConfigDialog::save_current_editor_to_config()
{
  if (current_row_ < 0 || current_row_ >= static_cast<int>(config_.panes.size())) {
    return;
  }
  PaneConfig & p = config_.panes[current_row_];
  p.base = base_combo_->currentText().toStdString();
  p.transport = transport_combo_->currentText().toStdString();
  p.warn_s = warn_spin_->value();
  p.error_s = error_spin_->value();
}

void ConfigDialog::load_editor_from_config(int row)
{
  if (row < 0 || row >= static_cast<int>(config_.panes.size())) {
    base_combo_->setEditText("");
    transport_combo_->setCurrentText("raw");
    warn_spin_->setValue(kDefaultWarnS);
    error_spin_->setValue(kDefaultErrorS);
    return;
  }
  const PaneConfig & p = config_.panes[row];
  base_combo_->setEditText(QString::fromStdString(p.base));
  transport_combo_->setCurrentText(QString::fromStdString(p.transport));
  warn_spin_->setValue(p.warn_s);
  error_spin_->setValue(p.error_s);
}

void ConfigDialog::refresh_list()
{
  list_->blockSignals(true);
  list_->clear();
  for (size_t i = 0; i < config_.panes.size(); ++i) {
    const int row = static_cast<int>(i) / std::max(1, config_.cols);
    const int col = static_cast<int>(i) % std::max(1, config_.cols);
    const QString prefix =
      QString("[%1,%2] ").arg(row).arg(col);
    const QString base = config_.panes[i].base.empty() ?
      QString("(empty)") :
      QString::fromStdString(config_.panes[i].base);
    list_->addItem(prefix + base);
  }
  list_->blockSignals(false);
}

GridConfig ConfigDialog::get_config()
{
  // Fold the editor's live field values into config_ before returning, so
  // callers see everything the user typed (including edits to the currently
  // selected pane that haven't been flushed via a selection change).
  save_current_editor_to_config();
  return config_;
}

}  // namespace rqt_camera_grid
