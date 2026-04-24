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

#ifndef RQT_CAMERA_GRID__CONFIG_DIALOG_HPP_
#define RQT_CAMERA_GRID__CONFIG_DIALOG_HPP_

#include <QDialog>  // NOLINT(build/include_order)

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <image_transport/image_transport.hpp>
#include <rclcpp/rclcpp.hpp>

#include "rqt_camera_grid/config_model.hpp"

class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QPushButton;
class QSpinBox;

namespace rqt_camera_grid
{

class ThumbnailCell;

class ConfigDialog : public QDialog
{
  Q_OBJECT

public:
  ConfigDialog(
    rclcpp::Node::SharedPtr node,
    const GridConfig & initial,
    QWidget * parent = nullptr);

  // Non-const because it folds the editor's live field values into config_
  // before returning. The alternative (const method with const_cast) hides
  // the mutation from readers; the alternative-alternative (commit on
  // accepted signal) would leave the dialog surprising if callers read
  // mid-edit. This is the honest compromise.
  GridConfig get_config();

private slots:
  void onRowsChanged(int value);
  void onColsChanged(int value);
  void onMoveUp();
  void onMoveDown();
  void onMoveLeft();
  void onMoveRight();
  void onClearPane();
  void onImportYaml();
  void onExportYaml();
  void onBaseEditChanged(int index);

private:
  void populate_topic_combo();
  void save_current_editor_to_config();
  void load_editor_from_config(int row);
  // Rebuild config_.panes for new_rows × new_cols while preserving each
  // cell's (row, col) position — unlike `resize_panes` which just
  // truncates or pads the flat vector (re-interpreting row-major order
  // shuffles cells on col shrink/grow). Cells that fall out of bounds
  // are moved to `removed_cells_`; cells that come into bounds at a
  // position present in `removed_cells_` are restored from there. So
  // shrink-then-grow within a single dialog session round-trips
  // losslessly.
  void reshape_grid(int new_rows, int new_cols);
  // Tear down and rebuild the thumbnail cells. Called whenever config_
  // changes size (rows/cols spinbox, import) or the pane configs
  // themselves change (clear, swap). Subscription churn is acceptable
  // because the dialog is short-lived.
  void rebuild_thumbnail_grid();
  // Selection helper: highlight cell at `row`, load its pane into the
  // editor fields, refresh toolbar button states. `row == -1` clears
  // selection and editor.
  void set_selected_row(int row);
  // Swap the selected pane with its row-major neighbor and track focus
  // to the new cell. Called by the four arrow-button slots.
  void move_pane(int dst_row);
  // Recompute enabled state of the arrow/Clear buttons for `row` (or -1
  // when nothing is selected). Call from selection / grid-dim / import
  // paths, i.e. any time `row` or `rows*cols` might change.
  void update_edit_buttons(int row);

  rclcpp::Node::SharedPtr node_;
  std::shared_ptr<image_transport::ImageTransport> it_;
  GridConfig config_;
  int current_row_{-1};
  // Cells that fell out of bounds when the user shrank the grid.
  // Keyed by (row, col); if the grid later grows back to include the
  // same (row, col), the cell is restored from here. Cleared on import
  // (new starting state). Lifetime = dialog lifetime only.
  std::map<std::pair<int, int>, PaneConfig> removed_cells_;

  QSpinBox * rows_spin_;
  QSpinBox * cols_spin_;
  QGridLayout * thumbnail_grid_;
  std::vector<ThumbnailCell *> thumbnails_;
  QComboBox * base_combo_;
  QComboBox * transport_combo_;
  QDoubleSpinBox * warn_spin_;
  QDoubleSpinBox * error_spin_;
  QPushButton * move_up_btn_{nullptr};
  QPushButton * move_down_btn_{nullptr};
  QPushButton * move_left_btn_{nullptr};
  QPushButton * move_right_btn_{nullptr};
  QPushButton * clear_btn_{nullptr};
};

}  // namespace rqt_camera_grid

#endif  // RQT_CAMERA_GRID__CONFIG_DIALOG_HPP_
