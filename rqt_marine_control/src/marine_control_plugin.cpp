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

#include "rqt_marine_control/marine_control_plugin.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QList>
#include <QMetaObject>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <vector>

#include <pluginlib/class_list_macros.hpp>

#include "rqt_marine_control/topic_filter.hpp"

namespace rqt_marine_control
{

MarineControlPlugin::MarineControlPlugin()
: rqt_gui_cpp::Plugin()
{
  setObjectName("MarineControl");
}

void MarineControlPlugin::initPlugin(qt_gui_cpp::PluginContext & context)
{
  widget_ = new QWidget();
  auto * layout = new QVBoxLayout(widget_);

  auto * toolbar = new QHBoxLayout();
  toolbar->addWidget(new QLabel(tr("Control topic:")));
  topic_combo_ = new QComboBox();
  topic_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolbar->addWidget(topic_combo_, 1);
  auto * refresh = new QPushButton();
  refresh->setIcon(QIcon::fromTheme("view-refresh"));
  refresh->setToolTip(tr("Refresh the list of control topics"));
  toolbar->addWidget(refresh);
  layout->addLayout(toolbar);

  // The dynamic control panel lives in a scroll area so a device with many
  // controls stays usable in a small dock.
  auto * scroll = new QScrollArea();
  scroll->setWidgetResizable(true);
  control_widget_ = new marine_control_widgets::ControlSetWidget();
  scroll->setWidget(control_widget_);
  layout->addWidget(scroll, 1);

  widget_->setWindowTitle(
    QStringLiteral("Marine Control (") + QString::number(context.serialNumber()) + ")");
  context.addWidget(widget_);

  connect(refresh, &QPushButton::clicked, this, &MarineControlPlugin::updateTopicList);
  connect(
    control_widget_.data(), &marine_control_widgets::ControlSetWidget::controlChanged,
    this, &MarineControlPlugin::publishChange);

  updateTopicList();
  topic_combo_->setCurrentIndex(topic_combo_->findText(""));
  connect(
    topic_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &MarineControlPlugin::onTopicChanged);

  const QList<QString> & argv = context.argv();
  if (!argv.empty()) {
    arg_topic_ = argv[0];
    selectTopic(arg_topic_);
  }
}

void MarineControlPlugin::shutdownPlugin()
{
  state_sub_.reset();
  change_pub_.reset();
}

void MarineControlPlugin::saveSettings(
  qt_gui_cpp::Settings & plugin_settings,
  qt_gui_cpp::Settings & instance_settings) const
{
  (void)plugin_settings;
  instance_settings.setValue("topic", topic_combo_->currentText());
}

void MarineControlPlugin::restoreSettings(
  const qt_gui_cpp::Settings & plugin_settings,
  const qt_gui_cpp::Settings & instance_settings)
{
  (void)plugin_settings;
  const QString topic = instance_settings.value("topic", "").toString();
  if (!arg_topic_.isEmpty()) {
    arg_topic_ = "";   // don't override a topic passed on the command line
  } else {
    selectTopic(topic);
  }
}

void MarineControlPlugin::updateTopicList()
{
  const QString selected = topic_combo_->currentText();

  QList<QString> topics;
  topics.append("");   // the "no topic" entry
  if (node_) {
    for (const auto & name : control_set_topics(node_->get_topic_names_and_types())) {
      topics.append(QString::fromStdString(name));
    }
  }

  topic_combo_->clear();
  for (const auto & topic : topics) {
    topic_combo_->addItem(topic);
  }
  selectTopic(selected);
}

void MarineControlPlugin::selectTopic(const QString & topic)
{
  int index = topic_combo_->findText(topic);
  if (index == -1) {
    topic_combo_->addItem(topic);
    index = topic_combo_->findText(topic);
  }
  topic_combo_->setCurrentIndex(index);
}

void MarineControlPlugin::onTopicChanged(int index)
{
  state_sub_.reset();
  change_pub_.reset();
  if (control_widget_) {
    control_widget_->clear();
  }

  const QString topic = topic_combo_->itemText(index);
  if (topic.isEmpty() || !node_) {
    return;
  }

  const std::string state_topic = topic.toStdString();
  // State QoS RELIABLE + VOLATILE (ADR-0003 D5) to match the device library's
  // publisher; depth 10 absorbs a heartbeat burst.
  state_sub_ = node_->create_subscription<marine_control_interfaces::msg::ControlSet>(
    state_topic, rclcpp::QoS(10),
    [this](marine_control_interfaces::msg::ControlSet::ConstSharedPtr msg) {
      controlSetCallback(msg);
    });
  change_pub_ = node_->create_publisher<marine_control_interfaces::msg::ControlValue>(
    derive_change_topic(state_topic), rclcpp::QoS(10));
}

void MarineControlPlugin::controlSetCallback(
  marine_control_interfaces::msg::ControlSet::ConstSharedPtr message)
{
  {
    std::lock_guard<std::mutex> lock(latest_mutex_);
    latest_ = *message;
    have_latest_ = true;
  }
  // Render on the GUI thread; coalesces to the most recent set.
  QMetaObject::invokeMethod(this, "applyLatest", Qt::QueuedConnection);
}

void MarineControlPlugin::applyLatest()
{
  if (!control_widget_) {
    return;
  }
  marine_control_interfaces::msg::ControlSet set;
  {
    std::lock_guard<std::mutex> lock(latest_mutex_);
    if (!have_latest_) {
      return;
    }
    set = latest_;
  }
  control_widget_->apply(set);
}

void MarineControlPlugin::publishChange(const QString & name, const QString & value)
{
  if (!change_pub_ || !node_) {
    return;
  }
  marine_control_interfaces::msg::ControlValue command;
  command.header.stamp = node_->now();
  command.name = name.toStdString();
  command.value = value.toStdString();
  change_pub_->publish(command);
}

}  // namespace rqt_marine_control

PLUGINLIB_EXPORT_CLASS(rqt_marine_control::MarineControlPlugin, rqt_gui_cpp::Plugin)
