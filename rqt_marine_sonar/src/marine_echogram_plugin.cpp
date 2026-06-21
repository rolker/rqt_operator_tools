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

#include <QCheckBox>
#include <QComboBox>
#include <QIcon>
#include <QList>
#include <QPushButton>
#include <QTimer>

#include <algorithm>
#include <string>
#include <vector>

#include <pluginlib/class_list_macros.hpp>
#include <rqt_sonar_waterfall/color_map.hpp>
#include <rqt_sonar_waterfall/history_spinbox.hpp>

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
    ui_.autoRangeCheckBox, &QCheckBox::toggled,
    this, &MarineEchogramPlugin::on_autoRangeCheckBox_toggled);
  connect(
    ui_.blackDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &MarineEchogramPlugin::on_blackDoubleSpinBox_valueChanged);
  connect(
    ui_.whiteDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &MarineEchogramPlugin::on_whiteDoubleSpinBox_valueChanged);
  connect(
    ui_.contrastDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &MarineEchogramPlugin::on_contrastDoubleSpinBox_valueChanged);
  // Palettes come from the shared marine_colormap set (same list, same order
  // as the waterfall, so the operator sees consistent choices).
  for (int i = 0; i < rqt_sonar_waterfall::kColorMapCount; ++i) {
    ui_.paletteComboBox->addItem(
      rqt_sonar_waterfall::color_map_name(
        rqt_sonar_waterfall::color_map_from_index(i)));
  }
  connect(
    ui_.paletteComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
    this, &MarineEchogramPlugin::on_paletteComboBox_currentIndexChanged);
  // History: data-retention knob shared with the waterfall (range/step/tooltip
  // from the common factory). Default 500 pings.
  rqt_sonar_waterfall::configure_history_spinbox(ui_.historySpinBox, 500);
  connect(
    ui_.historySpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
    this, [this](int value) {echogram_->setHistory(value);});

  context.addWidget(widget_);

  // Coalesce incoming pings into a fixed-cadence redraw (~20 Hz) on the GUI
  // thread, so a fast feed doesn't post one repaint per message.
  redraw_timer_ = new QTimer(this);
  connect(redraw_timer_, &QTimer::timeout, this, &MarineEchogramPlugin::newPings);
  redraw_timer_->start(50);

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
  // Stop new callbacks and the redraw timer first, then drop any pings already
  // queued so a late drain has nothing to push.
  if (redraw_timer_) {
    redraw_timer_->stop();
  }
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

  instance_settings.setValue("auto_range", ui_.echogramWidget->autoRange());
  instance_settings.setValue("black_point", ui_.echogramWidget->blackPoint());
  instance_settings.setValue("white_point", ui_.echogramWidget->whitePoint());
  instance_settings.setValue("contrast", ui_.echogramWidget->contrast());
  instance_settings.setValue("palette", ui_.echogramWidget->colorMapIndex());
  instance_settings.setValue("history", ui_.echogramWidget->history());
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

  // Auto-range defaults on (the palette spans the live data extent), so a fresh
  // layout needs no value-window seeding. Black/white default to the full
  // [0, 1] window for when the operator turns auto-range off.
  ui_.echogramWidget->setAutoRange(instance_settings.value("auto_range", true).toBool());
  ui_.echogramWidget->setBlackPoint(instance_settings.value("black_point", 0.0).toFloat());
  ui_.echogramWidget->setWhitePoint(instance_settings.value("white_point", 1.0).toFloat());
  ui_.echogramWidget->setContrast(instance_settings.value("contrast", 1.0).toFloat());
  ui_.echogramWidget->setColorMapIndex(instance_settings.value("palette", 0).toInt());
  // "history" replaces the old "ping_spacing" key; old layouts missing it fall
  // back to 500, and a stale "ping_spacing" key is simply not read.
  ui_.echogramWidget->setHistory(instance_settings.value("history", 500).toInt());

  ui_.autoRangeCheckBox->setChecked(ui_.echogramWidget->autoRange());
  ui_.blackDoubleSpinBox->setValue(ui_.echogramWidget->blackPoint());
  ui_.whiteDoubleSpinBox->setValue(ui_.echogramWidget->whitePoint());
  ui_.blackDoubleSpinBox->setEnabled(!ui_.echogramWidget->autoRange());
  ui_.whiteDoubleSpinBox->setEnabled(!ui_.echogramWidget->autoRange());
  ui_.contrastDoubleSpinBox->setValue(ui_.echogramWidget->contrast());
  ui_.paletteComboBox->setCurrentIndex(ui_.echogramWidget->colorMapIndex());
  ui_.historySpinBox->setValue(ui_.echogramWidget->history());
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
  QString topic = ui_.topicsComboBox->itemData(index).toString();
  if (!topic.isEmpty() && node_) {
    // SensorDataQoS (best_effort) matches this stack's sonar publishers and the
    // sibling rqt_sonar_waterfall; the ROS 1 original had no QoS concept and a
    // reliable subscription would silently receive nothing from them.
    data_subscriber_ = node_->create_subscription<marine_acoustic_msgs::msg::RawSonarImage>(
      topic.toStdString(), rclcpp::SensorDataQoS(),
      std::bind(&MarineEchogramPlugin::dataCallback, this, std::placeholders::_1));
  }
  if (widget_) {
    widget_->setToolTip(topic);
  }
}

void MarineEchogramPlugin::dataCallback(
  marine_acoustic_msgs::msg::RawSonarImage::ConstSharedPtr message)
{
  // Enqueue only; the GUI-thread redraw_timer_ drains the queue at a fixed
  // cadence (initPlugin), coalescing bursts into one redraw.
  std::lock_guard<std::mutex> lock(new_pings_mutex_);
  new_pings_.push_back(*message);
}

void MarineEchogramPlugin::newPings()
{
  // Swap the queue out under the lock, then redraw without holding it: the GPU
  // upload/redraw shouldn't block dataCallback() on the executor thread.
  std::vector<marine_acoustic_msgs::msg::RawSonarImage> pings;
  {
    std::lock_guard<std::mutex> lock(new_pings_mutex_);
    pings.swap(new_pings_);
  }
  // echogram_ is null once the widget is torn down — drop the pings instead of
  // dereferencing freed memory on the GUI thread.
  if (echogram_) {
    // Batch ingest: one image rebuild for the whole burst instead of one per
    // ping (fast bag replay can queue dozens of pings per GUI-thread wakeup).
    // Auto-range (default) scales the palette to the data; no value-window
    // seeding needed.
    echogram_->addPings(pings);
  }
}

void MarineEchogramPlugin::on_autoRangeCheckBox_toggled(bool checked)
{
  ui_.echogramWidget->setAutoRange(checked);
  // Black/white points only apply in manual mode.
  ui_.blackDoubleSpinBox->setEnabled(!checked);
  ui_.whiteDoubleSpinBox->setEnabled(!checked);
}

void MarineEchogramPlugin::on_blackDoubleSpinBox_valueChanged(double value)
{
  ui_.echogramWidget->setBlackPoint(value);
}

void MarineEchogramPlugin::on_whiteDoubleSpinBox_valueChanged(double value)
{
  ui_.echogramWidget->setWhitePoint(value);
}

void MarineEchogramPlugin::on_contrastDoubleSpinBox_valueChanged(double value)
{
  ui_.echogramWidget->setContrast(value);
}

void MarineEchogramPlugin::on_paletteComboBox_currentIndexChanged(int index)
{
  ui_.echogramWidget->setColorMapIndex(index);
}

}  // namespace rqt_marine_sonar

PLUGINLIB_EXPORT_CLASS(rqt_marine_sonar::MarineEchogramPlugin, rqt_gui_cpp::Plugin)
