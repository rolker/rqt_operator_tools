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

#include "rqt_camera_grid/camera_grid_plugin.hpp"

#include <QString>
#include <QVariant>

#include <filesystem>
#include <string>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <pluginlib/class_list_macros.hpp>

#include "rqt_camera_grid/camera_grid_widget.hpp"
#include "rqt_camera_grid/config_dialog.hpp"
#include "rqt_camera_grid/config_model.hpp"

namespace rqt_camera_grid
{

CameraGridPlugin::CameraGridPlugin()
: rqt_gui_cpp::Plugin()
{
  setObjectName("CameraGridPlugin");
}

CameraGridPlugin::~CameraGridPlugin() = default;

void CameraGridPlugin::initPlugin(qt_gui_cpp::PluginContext & context)
{
  widget_ = new CameraGridWidget(node_);
  widget_->setObjectName("CameraGridWidget");
  context.addWidget(widget_);
  load_default_config_if_shipped();
}

void CameraGridPlugin::shutdownPlugin()
{
  // Widget ownership is transferred to context.addWidget; it gets destroyed
  // when the container closes. Explicit nullification here keeps later
  // callbacks from touching a dead pointer.
  widget_ = nullptr;
}

void CameraGridPlugin::load_default_config_if_shipped()
{
  if (!widget_) {return;}
  try {
    const std::string share_dir = ament_index_cpp::get_package_share_directory(
      "rqt_camera_grid");
    const std::string path = share_dir + "/config/default_camera_grid.yaml";
    if (std::filesystem::exists(path)) {
      widget_->load_config(config_from_file(path));
    }
  } catch (const std::exception & e) {
    RCLCPP_WARN(
      node_->get_logger(),
      "failed to load default config: %s", e.what());
  }
}

void CameraGridPlugin::saveSettings(
  qt_gui_cpp::Settings & /*plugin_settings*/,
  qt_gui_cpp::Settings & instance_settings) const
{
  if (!widget_) {return;}
  const auto yaml = config_to_yaml(widget_->get_config());
  instance_settings.setValue(
    "config_yaml", QString::fromStdString(yaml));
}

void CameraGridPlugin::restoreSettings(
  const qt_gui_cpp::Settings & /*plugin_settings*/,
  const qt_gui_cpp::Settings & instance_settings)
{
  if (!widget_) {return;}
  const QVariant v = instance_settings.value("config_yaml");
  if (v.isValid()) {
    const std::string yaml = v.toString().toStdString();
    if (!yaml.empty()) {
      try {
        widget_->load_config(config_from_yaml(yaml));
      } catch (const ConfigParseError & e) {
        RCLCPP_WARN(
          node_->get_logger(),
          "failed to restore config from perspective: %s", e.what());
      }
    }
  }
}

void CameraGridPlugin::triggerConfiguration()
{
  if (!widget_) {return;}
  ConfigDialog dialog(node_, widget_->get_config(), widget_);
  if (dialog.exec() == QDialog::Accepted) {
    widget_->load_config(dialog.get_config());
  }
}

}  // namespace rqt_camera_grid

PLUGINLIB_EXPORT_CLASS(
  rqt_camera_grid::CameraGridPlugin,
  rqt_gui_cpp::Plugin)
