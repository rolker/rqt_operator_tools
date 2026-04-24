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
#include <QSignalBlocker>
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
  // Pane-edit toolbar: four direction arrows (swap with row-major
  // neighbor) + Clear (reset to default PaneConfig{}). Grid *size* is
  // controlled entirely by the rows/cols spinboxes above; these
  // buttons only rearrange and reset existing cells, keeping
  // panes.size() == rows*cols invariant by construction.
  auto * list_btns = new QHBoxLayout();
  move_up_btn_ = new QPushButton(QString::fromUtf8("↑"));
  move_up_btn_->setFixedWidth(30);
  move_up_btn_->setToolTip("Move pane up (swap with neighbor above)");
  move_left_btn_ = new QPushButton(QString::fromUtf8("←"));
  move_left_btn_->setFixedWidth(30);
  move_left_btn_->setToolTip("Move pane left (swap with neighbor)");
  move_right_btn_ = new QPushButton(QString::fromUtf8("→"));
  move_right_btn_->setFixedWidth(30);
  move_right_btn_->setToolTip("Move pane right (swap with neighbor)");
  move_down_btn_ = new QPushButton(QString::fromUtf8("↓"));
  move_down_btn_->setFixedWidth(30);
  move_down_btn_->setToolTip("Move pane down (swap with neighbor below)");
  clear_btn_ = new QPushButton("Clear");
  clear_btn_->setToolTip("Clear the selected pane's configuration");
  list_btns->addWidget(move_up_btn_);
  list_btns->addWidget(move_left_btn_);
  list_btns->addWidget(move_right_btn_);
  list_btns->addWidget(move_down_btn_);
  list_btns->addSpacing(8);
  list_btns->addWidget(clear_btn_);
  list_btns->addStretch();
  left->addLayout(list_btns);
  body->addLayout(left, 1);

  connect(list_, &QListWidget::currentRowChanged,
          this, &ConfigDialog::onSelectionChanged);
  connect(move_up_btn_, &QPushButton::clicked, this, &ConfigDialog::onMoveUp);
  connect(move_down_btn_, &QPushButton::clicked, this, &ConfigDialog::onMoveDown);
  connect(move_left_btn_, &QPushButton::clicked, this, &ConfigDialog::onMoveLeft);
  connect(move_right_btn_, &QPushButton::clicked, this, &ConfigDialog::onMoveRight);
  connect(clear_btn_, &QPushButton::clicked, this, &ConfigDialog::onClearPane);

  // Right: pane editor.
  auto * right_form = new QFormLayout();
  base_combo_ = new QComboBox();
  base_combo_->setEditable(true);
  // Pair the combo with a Refresh button so operators can pick up topics
  // advertised after the dialog opened without having to close and reopen.
  // Still snapshot-based per stability rule 1 — just on demand.
  auto * base_row = new QHBoxLayout();
  base_row->addWidget(base_combo_, 1);
  auto * refresh_btn = new QPushButton("Refresh");
  refresh_btn->setToolTip("Re-scan for advertised image topics");
  base_row->addWidget(refresh_btn);
  right_form->addRow("Base topic:", base_row);
  connect(refresh_btn, &QPushButton::clicked, this,
          &ConfigDialog::populate_topic_combo);
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

  // Keep error_s >= warn_s at all times — config_from_yaml rejects the
  // reversed case, so a dialog that lets the user save error < warn would
  // persist a config the dialog itself can't reload. Coordinate the two
  // spinboxes: raising warn pushes error up with it; lowering error below
  // warn pulls it back up.
  connect(warn_spin_,
          QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this,
    [this](double v) {
      if (error_spin_->value() < v) {
        const QSignalBlocker block(error_spin_);
        error_spin_->setValue(v);
      }
    });
  connect(error_spin_,
          QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this,
    [this](double v) {
      if (v < warn_spin_->value()) {
        const QSignalBlocker block(error_spin_);
        error_spin_->setValue(warn_spin_->value());
      }
    });
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
  } else {
    update_edit_buttons(-1);
  }
}

void ConfigDialog::populate_topic_combo()
{
  // Preserve any in-progress edit text across the repopulate so that hitting
  // Refresh with a custom topic typed in doesn't clobber the user's input.
  const QString preserved = base_combo_->currentText();
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
  base_combo_->setEditText(preserved);
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
  update_edit_buttons(new_row);
}

void ConfigDialog::onColsChanged(int value)
{
  save_current_editor_to_config();
  resize_panes(config_, config_.rows, value);
  refresh_list();
  const int new_row = std::min<int>(current_row_, static_cast<int>(config_.panes.size()) - 1);
  if (new_row >= 0) {list_->setCurrentRow(new_row);}
  update_edit_buttons(new_row);
}

void ConfigDialog::onSelectionChanged(int row)
{
  save_current_editor_to_config();
  current_row_ = row;
  load_editor_from_config(row);
  update_edit_buttons(row);
}

void ConfigDialog::onMoveUp()
{
  const int row = list_->currentRow();
  if (row < 0 || row < config_.cols) {return;}
  move_pane(row - config_.cols);
}

void ConfigDialog::onMoveDown()
{
  const int row = list_->currentRow();
  if (row < 0) {return;}
  const int dst = row + config_.cols;
  if (dst >= static_cast<int>(config_.panes.size())) {return;}
  move_pane(dst);
}

void ConfigDialog::onMoveLeft()
{
  const int row = list_->currentRow();
  if (row < 0 || (row % config_.cols) == 0) {return;}
  move_pane(row - 1);
}

void ConfigDialog::onMoveRight()
{
  const int row = list_->currentRow();
  if (row < 0 || (row % config_.cols) == config_.cols - 1) {return;}
  const int dst = row + 1;
  if (dst >= static_cast<int>(config_.panes.size())) {return;}
  move_pane(dst);
}

void ConfigDialog::onClearPane()
{
  const int row = list_->currentRow();
  if (row < 0 || row >= static_cast<int>(config_.panes.size())) {return;}
  // Suppress save_current_editor_to_config on the selection change
  // below — we're deliberately replacing the pane, not copying the
  // editor's stale values into it.
  current_row_ = -1;
  config_.panes[row] = PaneConfig{};
  refresh_list();
  list_->setCurrentRow(row);  // triggers load_editor_from_config + update_edit_buttons
}

void ConfigDialog::move_pane(int dst_row)
{
  const int src = list_->currentRow();
  if (src < 0 ||
    src >= static_cast<int>(config_.panes.size()) ||
    dst_row < 0 ||
    dst_row >= static_cast<int>(config_.panes.size()))
  {
    return;
  }
  // Flush any pending edits into the source pane so the swap carries
  // them along, then suppress the about-to-fire save on the
  // setCurrentRow(dst_row) below (dst is now the source's config;
  // we don't want to overwrite it with the editor's pre-swap values).
  save_current_editor_to_config();
  std::swap(config_.panes[src], config_.panes[dst_row]);
  current_row_ = -1;
  refresh_list();
  list_->setCurrentRow(dst_row);
}

void ConfigDialog::update_edit_buttons(int row)
{
  const int n = static_cast<int>(config_.panes.size());
  const bool selected = row >= 0 && row < n;
  clear_btn_->setEnabled(selected);
  if (!selected) {
    move_up_btn_->setEnabled(false);
    move_down_btn_->setEnabled(false);
    move_left_btn_->setEnabled(false);
    move_right_btn_->setEnabled(false);
    return;
  }
  const int r = row / config_.cols;
  const int c = row % config_.cols;
  move_up_btn_->setEnabled(r > 0);
  move_down_btn_->setEnabled(r < config_.rows - 1 && row + config_.cols < n);
  move_left_btn_->setEnabled(c > 0);
  move_right_btn_->setEnabled(c < config_.cols - 1 && row + 1 < n);
}

void ConfigDialog::onImportYaml()
{
  const QString path = QFileDialog::getOpenFileName(
    this, "Import YAML", {}, "YAML files (*.yaml *.yml)");
  if (path.isEmpty()) {return;}
  try {
    GridConfig loaded = config_from_file(path.toStdString());
    config_ = loaded;
    // Restore the dialog's panes.size() == rows*cols invariant (symmetric
    // with onAddPane / onRemovePane). YAML authors can hand-edit a file
    // where panes and grid dimensions disagree; pad or truncate to match
    // so every cell is editable from the list.
    resize_panes(config_, config_.rows, config_.cols);
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
    if (!config_.panes.empty()) {
      list_->setCurrentRow(0);
    } else {
      update_edit_buttons(-1);
    }
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
