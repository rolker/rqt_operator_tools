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

#include "rqt_marine_sonar/marine_echogram_plugin.hpp"

#include <QChart>
#include <QComboBox>
#include <QIcon>
#include <QList>
#include <QMetaObject>
#include <QPushButton>

#include <algorithm>
#include <string>
#include <vector>

#include <pluginlib/class_list_macros.hpp>

namespace rqt_marine_sonar
{

namespace
{
/// ROS 2 type name of the messages this plugin displays.
constexpr const char * kRawSonarImageType = "marine_acoustic_msgs/msg/RawSonarImage";
}  // namespace

MarineEchogramPlugin::MarineEchogramPlugin()
: rqt_gui_cpp::Plugin()
{
  setObjectName("MarineEchogram");
}

void MarineEchogramPlugin::initPlugin(qt_gui_cpp::PluginContext & context)
{
  widget_ = new QWidget();
  ui_.setupUi(widget_);
  echogram_ = ui_.echogramWidget;

  widget_->setWindowTitle(
    widget_->windowTitle() + " (" + QString::number(context.serialNumber()) + ")");

  connect(
    ui_.minDbDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &MarineEchogramPlugin::on_minDbDoubleSpinBox_valueChanged);
  connect(
    ui_.maxDbDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &MarineEchogramPlugin::on_maxDbDoubleSpinBox_valueChanged);
  connect(
    ui_.pingSpacingDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &MarineEchogramPlugin::on_pingSpacingDoubleSpinBox_valueChanged);

  context.addWidget(widget_);

  updateTopicList();

  ui_.topicsComboBox->setCurrentIndex(ui_.topicsComboBox->findText(""));
  connect(
    ui_.topicsComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &MarineEchogramPlugin::onTopicChanged);

  ui_.refreshTopicsPushButton->setIcon(QIcon::fromTheme("view-refresh"));
  connect(
    ui_.refreshTopicsPushButton, &QPushButton::pressed,
    this, &MarineEchogramPlugin::updateTopicList);

  // set topic name if passed in as argument
  const QStringList & argv = context.argv();
  if (!argv.empty()) {
    arg_topic_ = argv[0];
    selectTopic(arg_topic_);
  }
}

void MarineEchogramPlugin::shutdownPlugin()
{
  // Stop new callbacks first, then drop any pings already queued so a
  // late newPings() (posted just before teardown) has nothing to push.
  data_subscriber_.reset();
  std::lock_guard<std::mutex> lock(new_pings_mutex_);
  new_pings_.clear();
}

void MarineEchogramPlugin::saveSettings(
  qt_gui_cpp::Settings & plugin_settings,
  qt_gui_cpp::Settings & instance_settings) const
{
  (void)plugin_settings;
  QString topic = ui_.topicsComboBox->currentText();
  instance_settings.setValue("topic", topic);

  instance_settings.setValue("minimum_db", ui_.echogramWidget->minimumDB());
  instance_settings.setValue("maximum_db", ui_.echogramWidget->maximumDB());
  instance_settings.setValue("ping_spacing", ui_.echogramWidget->pingSpacing());
}

void MarineEchogramPlugin::restoreSettings(
  const qt_gui_cpp::Settings & plugin_settings,
  const qt_gui_cpp::Settings & instance_settings)
{
  (void)plugin_settings;
  QString topic = instance_settings.value("topic", "").toString();
  // don't overwrite topic name passed as command line argument
  if (!arg_topic_.isEmpty()) {
    arg_topic_ = "";
  } else {
    selectTopic(topic);
  }

  ui_.echogramWidget->setMinimumDB(instance_settings.value("minimum_db", -100.0).toFloat());
  ui_.echogramWidget->setMaximumDB(instance_settings.value("maximum_db", 10.0).toFloat());
  ui_.echogramWidget->setPingSpacing(instance_settings.value("ping_spacing", 1.0).toFloat());

  ui_.minDbDoubleSpinBox->setValue(ui_.echogramWidget->minimumDB());
  ui_.maxDbDoubleSpinBox->setValue(ui_.echogramWidget->maximumDB());
  ui_.pingSpacingDoubleSpinBox->setValue(ui_.echogramWidget->pingSpacing());
}

void MarineEchogramPlugin::updateTopicList()
{
  QString selected = ui_.topicsComboBox->currentText();

  QList<QString> topics;
  if (node_) {
    const auto topics_and_types = node_->get_topic_names_and_types();
    for (const auto & entry : topics_and_types) {
      for (const auto & type : entry.second) {
        if (type == kRawSonarImageType) {
          topics.append(QString::fromStdString(entry.first));
        }
      }
    }
  }

  topics.append("");
  std::sort(topics.begin(), topics.end());
  ui_.topicsComboBox->clear();
  for (QList<QString>::const_iterator it = topics.begin(); it != topics.end(); it++) {
    QString label(*it);
    label.replace(" ", "/");
    ui_.topicsComboBox->addItem(label, QVariant(*it));
  }

  // restore previous selection
  selectTopic(selected);
}

void MarineEchogramPlugin::selectTopic(const QString & topic)
{
  int index = ui_.topicsComboBox->findText(topic);
  if (index == -1) {
    // add topic name to list if not yet in
    QString label(topic);
    label.replace(" ", "/");
    ui_.topicsComboBox->addItem(label, QVariant(topic));
    index = ui_.topicsComboBox->findText(topic);
  }
  ui_.topicsComboBox->setCurrentIndex(index);
}

void MarineEchogramPlugin::onTopicChanged(int index)
{
  data_subscriber_.reset();
  ui_.echogramWidget->chart()->setTitle("");
  QString topic = ui_.topicsComboBox->itemData(index).toString();
  if (!topic.isEmpty() && node_) {
    // SensorDataQoS (best_effort) matches this stack's sonar publishers and the
    // sibling rqt_sonar_waterfall; the ROS 1 original had no QoS concept and a
    // reliable subscription would silently receive nothing from them.
    data_subscriber_ = node_->create_subscription<marine_acoustic_msgs::msg::RawSonarImage>(
      topic.toStdString(), rclcpp::SensorDataQoS(),
      std::bind(&MarineEchogramPlugin::dataCallback, this, std::placeholders::_1));
    ui_.echogramWidget->chart()->setTitle(topic);
  }
}

void MarineEchogramPlugin::dataCallback(
  marine_acoustic_msgs::msg::RawSonarImage::ConstSharedPtr message)
{
  {
    std::lock_guard<std::mutex> lock(new_pings_mutex_);
    new_pings_.push_back(*message);
  }
  QMetaObject::invokeMethod(this, "newPings", Qt::QueuedConnection);
}

void MarineEchogramPlugin::newPings()
{
  // Swap the queue out under the lock, then redraw without holding it: addPing()
  // rebuilds the whole image, and holding new_pings_mutex_ across that would
  // block dataCallback() on the executor thread (callback latency / drops).
  std::vector<marine_acoustic_msgs::msg::RawSonarImage> pings;
  {
    std::lock_guard<std::mutex> lock(new_pings_mutex_);
    pings.swap(new_pings_);
  }
  // echogram_ is null once the widget is torn down — drop the pings instead of
  // dereferencing freed memory on the GUI thread.
  if (echogram_) {
    for (const auto & ping : pings) {
      echogram_->addPing(ping);
    }
  }
}

void MarineEchogramPlugin::on_minDbDoubleSpinBox_valueChanged(double value)
{
  ui_.echogramWidget->setMinimumDB(value);
}

void MarineEchogramPlugin::on_maxDbDoubleSpinBox_valueChanged(double value)
{
  ui_.echogramWidget->setMaximumDB(value);
}

void MarineEchogramPlugin::on_pingSpacingDoubleSpinBox_valueChanged(double value)
{
  ui_.echogramWidget->setPingSpacing(value);
}

}  // namespace rqt_marine_sonar

PLUGINLIB_EXPORT_CLASS(rqt_marine_sonar::MarineEchogramPlugin, rqt_gui_cpp::Plugin)
