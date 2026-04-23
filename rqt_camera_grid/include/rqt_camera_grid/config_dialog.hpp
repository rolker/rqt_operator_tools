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

#include <rclcpp/rclcpp.hpp>

#include "rqt_camera_grid/config_model.hpp"

class QComboBox;
class QDoubleSpinBox;
class QListWidget;
class QSpinBox;

namespace rqt_camera_grid
{

class ConfigDialog : public QDialog
{
  Q_OBJECT

public:
  ConfigDialog(
    rclcpp::Node::SharedPtr node,
    const GridConfig & initial,
    QWidget * parent = nullptr);

  GridConfig get_config() const;

private slots:
  void onRowsChanged(int value);
  void onColsChanged(int value);
  void onSelectionChanged(int row);
  void onAddPane();
  void onRemovePane();
  void onImportYaml();
  void onExportYaml();
  void onBaseEditChanged(int index);

private:
  void populate_topic_combo();
  void save_current_editor_to_config();
  void load_editor_from_config(int row);
  void refresh_list();

  rclcpp::Node::SharedPtr node_;
  GridConfig config_;
  int current_row_{-1};

  QSpinBox * rows_spin_;
  QSpinBox * cols_spin_;
  QListWidget * list_;
  QComboBox * base_combo_;
  QComboBox * transport_combo_;
  QDoubleSpinBox * warn_spin_;
  QDoubleSpinBox * error_spin_;
};

}  // namespace rqt_camera_grid

#endif  // RQT_CAMERA_GRID__CONFIG_DIALOG_HPP_
