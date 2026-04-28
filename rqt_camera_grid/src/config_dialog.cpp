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

#include "rqt_camera_grid/detail/config_dialog_helpers.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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

#include "rqt_camera_grid/thumbnail_cell.hpp"

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
  // Drop Qt 5's default "?" (WhatsThisHelp) titlebar button — nothing in
  // this dialog registers What's This hints, so clicking it was a no-op
  // that confused users.
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  // Thumbnail grid needs meaningful minimum sizes per cell; a 3x3 of
  // the ThumbnailCell minimums fits comfortably in 800x500.
  setMinimumSize(800, 500);

  // Dedicated ImageTransport for the preview thumbnails — separate from
  // the CameraGridWidget's so subscription lifecycle is tied to the
  // dialog. Destroyed with the dialog; thumbnails unsubscribe as part
  // of normal QObject child teardown.
  it_ = std::make_shared<image_transport::ImageTransport>(node_);

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

  // Left: thumbnail preview grid + toolbar. The grid mirrors the target
  // layout at thumbnail scale, with each cell showing the corresponding
  // pane's live frame. Click a cell to select it; the toolbar arrows /
  // Clear then operate on the selection.
  auto * left = new QVBoxLayout();
  auto * thumb_host = new QWidget();
  thumbnail_grid_ = new QGridLayout(thumb_host);
  thumbnail_grid_->setSpacing(2);
  thumbnail_grid_->setContentsMargins(0, 0, 0, 0);
  left->addWidget(thumb_host, 1);
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

  // Commit editor edits into config_.panes and refresh the affected
  // thumbnail as soon as a field is committed (dropdown pick, free-text
  // Enter/focus-loss, transport dropdown, threshold editingFinished).
  // Using editingFinished rather than every-keystroke signals keeps
  // the rebuild count low — once per user commit, not per character.
  connect(base_combo_->lineEdit(), &QLineEdit::editingFinished,
          this, &ConfigDialog::commit_current_editor);
  connect(transport_combo_,
          QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ConfigDialog::commit_current_editor);
  connect(warn_spin_, &QDoubleSpinBox::editingFinished,
          this, &ConfigDialog::commit_current_editor);
  connect(error_spin_, &QDoubleSpinBox::editingFinished,
          this, &ConfigDialog::commit_current_editor);

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
  rebuild_thumbnail_grid();
  set_selected_row(config_.panes.empty() ? -1 : 0);
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
    // Auto-select the transport hinted by the combo label. Block
    // transport_combo_ signals during the update so commit_current_editor
    // fires only once (below), not twice.
    const QString label = base_combo_->itemText(index);
    {
      QSignalBlocker block(transport_combo_);
      for (const auto & t : kTransports) {
        if (label.contains("[" + t + "]")) {
          transport_combo_->setCurrentText(t);
          break;
        }
      }
    }
  }
  // Dropdown pick is a user commit — save + refresh the thumbnail now
  // so the operator sees the new topic render without tabbing away.
  commit_current_editor();
}

void ConfigDialog::onRowsChanged(int value)
{
  save_current_editor_to_config();
  reshape_grid(value, config_.cols);
  rebuild_thumbnail_grid();
  const int new_row = std::min<int>(
    current_row_, static_cast<int>(config_.panes.size()) - 1);
  // Suppress the save inside set_selected_row for the re-select of
  // the same row (we just saved above and haven't touched the editor).
  current_row_ = -1;
  set_selected_row(new_row);
}

void ConfigDialog::onColsChanged(int value)
{
  save_current_editor_to_config();
  reshape_grid(config_.rows, value);
  rebuild_thumbnail_grid();
  const int new_row = std::min<int>(
    current_row_, static_cast<int>(config_.panes.size()) - 1);
  current_row_ = -1;
  set_selected_row(new_row);
}

void ConfigDialog::onMoveUp()
{
  if (current_row_ < 0 || current_row_ < config_.cols) {return;}
  move_pane(current_row_ - config_.cols);
}

void ConfigDialog::onMoveDown()
{
  if (current_row_ < 0) {return;}
  const int dst = current_row_ + config_.cols;
  if (dst >= static_cast<int>(config_.panes.size())) {return;}
  move_pane(dst);
}

void ConfigDialog::onMoveLeft()
{
  if (current_row_ < 0 || (current_row_ % config_.cols) == 0) {return;}
  move_pane(current_row_ - 1);
}

void ConfigDialog::onMoveRight()
{
  if (current_row_ < 0 ||
    (current_row_ % config_.cols) == config_.cols - 1)
  {
    return;
  }
  const int dst = current_row_ + 1;
  if (dst >= static_cast<int>(config_.panes.size())) {return;}
  move_pane(dst);
}

void ConfigDialog::onClearPane()
{
  const int row = current_row_;
  if (row < 0 || row >= static_cast<int>(config_.panes.size())) {return;}
  // Suppress save_current_editor_to_config on the reselect below — we're
  // deliberately replacing the pane, not copying the editor's stale
  // values into it.
  current_row_ = -1;
  config_.panes[row] = PaneConfig{};
  rebuild_thumbnail_grid();
  set_selected_row(row);
}

void ConfigDialog::move_pane(int dst_row)
{
  const int src = current_row_;
  if (src < 0 ||
    src >= static_cast<int>(config_.panes.size()) ||
    dst_row < 0 ||
    dst_row >= static_cast<int>(config_.panes.size()))
  {
    return;
  }
  // Flush pending edits into the source pane so the swap carries them
  // along. Then clear current_row_ so set_selected_row(dst) below
  // doesn't overwrite the moved pane with the editor's pre-swap values.
  save_current_editor_to_config();
  std::swap(config_.panes[src], config_.panes[dst_row]);
  current_row_ = -1;
  rebuild_thumbnail_grid();
  set_selected_row(dst_row);
}

void ConfigDialog::reshape_grid(int new_rows, int new_cols)
{
  // Snapshot the current grid as a (row, col) -> PaneConfig map so we
  // can rebuild at the new dimensions without the flat-vector shuffle
  // that resize_panes produces when cols changes.
  std::map<std::pair<int, int>, PaneConfig> current;
  for (int r = 0; r < config_.rows; ++r) {
    for (int c = 0; c < config_.cols; ++c) {
      const int idx = r * config_.cols + c;
      if (idx < static_cast<int>(config_.panes.size())) {
        current[{r, c}] = config_.panes[idx];
      }
    }
  }

  // Start the new removed buffer from the existing one (unchanged
  // out-of-bounds cells stay remembered) and push any cells that are
  // now falling out of bounds into it too.
  std::map<std::pair<int, int>, PaneConfig> new_removed = removed_cells_;
  for (const auto & [key, pane] : current) {
    const auto & [r, c] = key;
    if (r >= new_rows || c >= new_cols) {
      new_removed[key] = pane;
    }
  }

  // Build the new flat vector in row-major order. For each new (r, c):
  //   - take from the current in-bounds snapshot if available
  //   - else restore from the removed buffer if we have a match there
  //   - else fall back to a default PaneConfig{}
  std::vector<PaneConfig> new_panes;
  new_panes.reserve(static_cast<size_t>(new_rows) * new_cols);
  for (int r = 0; r < new_rows; ++r) {
    for (int c = 0; c < new_cols; ++c) {
      const auto key = std::make_pair(r, c);
      auto cur_it = current.find(key);
      if (cur_it != current.end()) {
        new_panes.push_back(cur_it->second);
        new_removed.erase(key);  // in case it was stale there
        continue;
      }
      auto rem_it = new_removed.find(key);
      if (rem_it != new_removed.end()) {
        new_panes.push_back(rem_it->second);
        new_removed.erase(rem_it);  // consumed on restore
        continue;
      }
      new_panes.push_back(PaneConfig{});
    }
  }

  config_.panes = std::move(new_panes);
  config_.rows = new_rows;
  config_.cols = new_cols;
  removed_cells_ = std::move(new_removed);
}

void ConfigDialog::commit_current_editor()
{
  if (current_row_ < 0 ||
    current_row_ >= static_cast<int>(config_.panes.size()))
  {
    return;
  }
  // Snapshot the saved pane so we can skip the rebuild if nothing
  // actually changed. Subscription-affecting fields are base and
  // transport; warn_s/error_s drive the staleness tracker which is
  // constructed with the pane so a rebuild picks them up too.
  const PaneConfig before = config_.panes[current_row_];
  save_current_editor_to_config();
  if (before == config_.panes[current_row_]) {
    return;
  }
  rebuild_thumbnail_cell(current_row_);
}

void ConfigDialog::rebuild_thumbnail_cell(int row)
{
  if (row < 0 || row >= static_cast<int>(thumbnails_.size())) {return;}
  if (row >= static_cast<int>(config_.panes.size())) {return;}
  const int r = row / config_.cols;
  const int c = row % config_.cols;
  const bool was_selected = (current_row_ == row);
  ThumbnailCell * old_cell = thumbnails_[row];
  thumbnail_grid_->removeWidget(old_cell);
  old_cell->deleteLater();
  auto * new_cell = new ThumbnailCell(
    node_, it_, config_.panes[row], row,
    [this](int i) {set_selected_row(i);}, nullptr);
  thumbnail_grid_->addWidget(new_cell, r, c);
  thumbnails_[row] = new_cell;
  if (was_selected) {
    new_cell->set_selected(true);
  }
}

void ConfigDialog::rebuild_thumbnail_grid()
{
  for (auto * cell : thumbnails_) {
    thumbnail_grid_->removeWidget(cell);
    cell->deleteLater();
  }
  thumbnails_.clear();
  for (int r = 0; r < config_.rows; ++r) {
    for (int c = 0; c < config_.cols; ++c) {
      const int idx = r * config_.cols + c;
      if (idx >= static_cast<int>(config_.panes.size())) {break;}
      auto * cell = new ThumbnailCell(
        node_, it_, config_.panes[idx], idx,
        [this](int i) {set_selected_row(i);}, nullptr);
      thumbnail_grid_->addWidget(cell, r, c);
      thumbnails_.push_back(cell);
    }
  }
  // Re-apply any pre-existing selection (used when rebuild happens
  // inside a handler that set current_row_ beforehand).
  if (current_row_ >= 0 &&
    current_row_ < static_cast<int>(thumbnails_.size()))
  {
    thumbnails_[current_row_]->set_selected(true);
  }
}

void ConfigDialog::set_selected_row(int row)
{
  save_current_editor_to_config();
  // Clear previous cell's selected state.
  if (current_row_ >= 0 &&
    current_row_ < static_cast<int>(thumbnails_.size()))
  {
    thumbnails_[current_row_]->set_selected(false);
  }
  current_row_ = row;
  if (row >= 0 && row < static_cast<int>(thumbnails_.size())) {
    thumbnails_[row]->set_selected(true);
  }
  load_editor_from_config(row);
  update_edit_buttons(row);
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
    // Fresh starting state — forget cells removed under the previous
    // config, they aren't relevant to the imported one.
    removed_cells_.clear();
    // Restore the dialog's panes.size() == rows*cols invariant. YAML
    // authors can hand-edit a file where panes and grid dimensions
    // disagree; pad or truncate to match so every cell has an editable
    // thumbnail.
    resize_panes(config_, config_.rows, config_.cols);
    // Clear current_row_ so set_selected_row(0) below doesn't save the
    // editor's pre-import values into the freshly loaded config.
    current_row_ = -1;
    rows_spin_->blockSignals(true);
    cols_spin_->blockSignals(true);
    rows_spin_->setValue(config_.rows);
    cols_spin_->setValue(config_.cols);
    rows_spin_->blockSignals(false);
    cols_spin_->blockSignals(false);
    rebuild_thumbnail_grid();
    set_selected_row(config_.panes.empty() ? -1 : 0);
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
  // See detail/config_dialog_helpers.hpp for the contract behind this
  // resolution. Pulled out so the four cases (idx == -1, idx == 0,
  // idx > 0 with text matching the item, idx > 0 with free-typed text)
  // can be unit-tested without standing up the full dialog.
  const int idx = base_combo_->currentIndex();
  p.base = detail::resolve_base_from_combo(
    idx,
    base_combo_->currentText(),
    idx >= 0 ? base_combo_->itemText(idx) : QString(),
    idx >= 0 ? base_combo_->itemData(idx).toString() : QString());
  p.transport = transport_combo_->currentText().toStdString();
  p.warn_s = warn_spin_->value();
  p.error_s = error_spin_->value();
}

void ConfigDialog::load_editor_from_config(int row)
{
  // Block transport_combo signals: setCurrentText fires
  // currentIndexChanged when it lands on a matching item, which would
  // re-enter commit_current_editor mid-load and overwrite the pane's
  // warn/error with stale editor values (those fields haven't been
  // repopulated yet — setValue calls below). setEditText / setValue
  // don't fire editingFinished, so no other blockers needed.
  QSignalBlocker block_transport(transport_combo_);
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

GridConfig ConfigDialog::get_config()
{
  // Fold the editor's live field values into config_ before returning, so
  // callers see everything the user typed (including edits to the currently
  // selected pane that haven't been flushed via a selection change).
  save_current_editor_to_config();
  return config_;
}

}  // namespace rqt_camera_grid
