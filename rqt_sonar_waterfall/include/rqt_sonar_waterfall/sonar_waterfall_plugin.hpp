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

#ifndef RQT_SONAR_WATERFALL__SONAR_WATERFALL_PLUGIN_HPP_
#define RQT_SONAR_WATERFALL__SONAR_WATERFALL_PLUGIN_HPP_

#include <QObject>  // NOLINT(build/include_order)
#include <QPointer>

#include <rqt_gui_cpp/plugin.h>

namespace rqt_sonar_waterfall
{

class WaterfallWidget;

/// rqt plugin entry point for the sonar backscatter waterfall viewer.
///
/// This scaffold wires up the plugin lifecycle and hosts an (empty)
/// WaterfallWidget. Topic selection, RawSonarImage subscriptions, client-side
/// image processing and the optional marine_radar_control_msgs control panel
/// are added in subsequent steps of issue #39.
class SonarWaterfallPlugin : public rqt_gui_cpp::Plugin
{
  Q_OBJECT

public:
  SonarWaterfallPlugin();
  ~SonarWaterfallPlugin() override;

  void initPlugin(qt_gui_cpp::PluginContext & context) override;
  void shutdownPlugin() override;
  void saveSettings(
    qt_gui_cpp::Settings & plugin_settings,
    qt_gui_cpp::Settings & instance_settings) const override;
  void restoreSettings(
    const qt_gui_cpp::Settings & plugin_settings,
    const qt_gui_cpp::Settings & instance_settings) override;

private:
  // QPointer auto-nulls if qt_gui_cpp's PluginContext destroys the widget,
  // keeping `if (widget_)` guards correct under either teardown ordering.
  QPointer<WaterfallWidget> widget_;
};

}  // namespace rqt_sonar_waterfall

#endif  // RQT_SONAR_WATERFALL__SONAR_WATERFALL_PLUGIN_HPP_
