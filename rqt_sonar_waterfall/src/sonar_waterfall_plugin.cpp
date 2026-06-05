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

#include "rqt_sonar_waterfall/sonar_waterfall_plugin.hpp"

#include <QVBoxLayout>
#include <QWidget>

#include <pluginlib/class_list_macros.hpp>

#include "rqt_sonar_waterfall/waterfall_widget.hpp"

namespace rqt_sonar_waterfall
{

SonarWaterfallPlugin::SonarWaterfallPlugin()
{
  setObjectName("SonarWaterfallPlugin");
}

SonarWaterfallPlugin::~SonarWaterfallPlugin() = default;

void SonarWaterfallPlugin::initPlugin(qt_gui_cpp::PluginContext & context)
{
  // Container hosts the waterfall today; the topic-selection toolbar and
  // control panel are added alongside it in later steps.
  auto * container = new QWidget();
  auto * layout = new QVBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);

  widget_ = new WaterfallWidget(container);
  layout->addWidget(widget_);

  if (context.serialNumber() > 1) {
    container->setWindowTitle(
      container->windowTitle() + " (" + QString::number(context.serialNumber()) + ")");
  }
  context.addWidget(container);
}

void SonarWaterfallPlugin::shutdownPlugin()
{
  // Subscriptions/publishers are torn down here once they exist.
}

void SonarWaterfallPlugin::saveSettings(
  qt_gui_cpp::Settings & /*plugin_settings*/,
  qt_gui_cpp::Settings & /*instance_settings*/) const
{
  // Selected topics and view knobs are persisted here in a later step.
}

void SonarWaterfallPlugin::restoreSettings(
  const qt_gui_cpp::Settings & /*plugin_settings*/,
  const qt_gui_cpp::Settings & /*instance_settings*/)
{
  // Restores the state saved by saveSettings().
}

}  // namespace rqt_sonar_waterfall

PLUGINLIB_EXPORT_CLASS(rqt_sonar_waterfall::SonarWaterfallPlugin, rqt_gui_cpp::Plugin)
