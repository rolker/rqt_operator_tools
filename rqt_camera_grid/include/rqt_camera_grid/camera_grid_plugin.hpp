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

#ifndef RQT_CAMERA_GRID__CAMERA_GRID_PLUGIN_HPP_
#define RQT_CAMERA_GRID__CAMERA_GRID_PLUGIN_HPP_

#include <QObject>  // NOLINT(build/include_order)
#include <QPointer>

#include <rqt_gui_cpp/plugin.h>

namespace rqt_camera_grid
{

class CameraGridWidget;

class CameraGridPlugin : public rqt_gui_cpp::Plugin
{
  Q_OBJECT

public:
  CameraGridPlugin();
  ~CameraGridPlugin() override;

  void initPlugin(qt_gui_cpp::PluginContext & context) override;
  void shutdownPlugin() override;
  void saveSettings(
    qt_gui_cpp::Settings & plugin_settings,
    qt_gui_cpp::Settings & instance_settings) const override;
  void restoreSettings(
    const qt_gui_cpp::Settings & plugin_settings,
    const qt_gui_cpp::Settings & instance_settings) override;

  bool hasConfiguration() const override {return true;}
  void triggerConfiguration() override;

private:
  void load_default_config_if_shipped();

  // QPointer auto-nulls when the referenced QObject is destroyed. The
  // widget's lifetime is owned by qt_gui_cpp's PluginContext (via
  // addWidget), so if rqt ever destroys the widget before or after
  // shutdownPlugin, the guard `if (widget_)` remains correct under
  // both orderings instead of leaving a dangling raw pointer.
  QPointer<CameraGridWidget> widget_;
};

}  // namespace rqt_camera_grid

#endif  // RQT_CAMERA_GRID__CAMERA_GRID_PLUGIN_HPP_
