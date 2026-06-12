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
#include <rqt_sonar_waterfall/color_map.hpp>
#include <rqt_sonar_waterfall/waterfall_model.hpp>

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
    ui_.minValueDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &MarineEchogramPlugin::on_minValueDoubleSpinBox_valueChanged);
  connect(
    ui_.maxValueDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &MarineEchogramPlugin::on_maxValueDoubleSpinBox_valueChanged);
  connect(
    ui_.gainDoubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
    this, &MarineEchogramPlugin::on_gainDoubleSpinBox_valueChanged);
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

  instance_settings.setValue("value_min", ui_.echogramWidget->minimumValue());
  instance_settings.setValue("value_max", ui_.echogramWidget->maximumValue());
  instance_settings.setValue("gain", ui_.echogramWidget->gain());
  instance_settings.setValue("contrast", ui_.echogramWidget->contrast());
  instance_settings.setValue("palette", ui_.echogramWidget->colorMapIndex());
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

  // value_min/value_max default to a degenerate (0, 0) window = "unset"; the
  // first ping then seeds a dtype-aware default (maybeSeedValueWindow). A
  // perspective saved by the pre-modernization plugin carries minimum_db /
  // maximum_db instead -- honour those as the window (for float dB sonars
  // they are exactly the right values).
  const float legacy_min = instance_settings.value("minimum_db", 0.0).toFloat();
  const float legacy_max = instance_settings.value("maximum_db", 0.0).toFloat();
  ui_.echogramWidget->setMinimumValue(
    instance_settings.value("value_min", legacy_min).toFloat());
  ui_.echogramWidget->setMaximumValue(
    instance_settings.value("value_max", legacy_max).toFloat());
  ui_.echogramWidget->setGain(instance_settings.value("gain", 1.0).toFloat());
  ui_.echogramWidget->setContrast(instance_settings.value("contrast", 1.0).toFloat());
  ui_.echogramWidget->setColorMapIndex(instance_settings.value("palette", 0).toInt());
  ui_.echogramWidget->setPingSpacing(instance_settings.value("ping_spacing", 1.0).toFloat());

  ui_.minValueDoubleSpinBox->setValue(ui_.echogramWidget->minimumValue());
  ui_.maxValueDoubleSpinBox->setValue(ui_.echogramWidget->maximumValue());
  ui_.gainDoubleSpinBox->setValue(ui_.echogramWidget->gain());
  ui_.contrastDoubleSpinBox->setValue(ui_.echogramWidget->contrast());
  ui_.paletteComboBox->setCurrentIndex(ui_.echogramWidget->colorMapIndex());
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
    if (!pings.empty()) {
      maybeSeedValueWindow(pings.front());
    }
    for (const auto & ping : pings) {
      echogram_->addPing(ping);
    }
  }
}

void MarineEchogramPlugin::maybeSeedValueWindow(
  const marine_acoustic_msgs::msg::RawSonarImage & ping)
{
  // Runs on the GUI thread (newPings). A degenerate window (max <= min) means
  // "not yet configured" -- neither restored settings nor the operator set it
  // -- so seed a dtype-aware default: integer sonars get [0, full scale]
  // (mirrors the waterfall's maybe_seed_manual_range); float sonars get the
  // dB window the pre-modernization plugin always used. A deliberate window
  // is never clobbered; re-seeding after a dtype change is the operator's
  // call (set max <= min to request a reseed).
  if (ui_.maxValueDoubleSpinBox->value() > ui_.minValueDoubleSpinBox->value()) {
    return;
  }
  using Img = marine_acoustic_msgs::msg::SonarImageData;
  double lo = 0.0;
  double hi = rqt_sonar_waterfall::default_full_scale(ping.image.dtype);
  if (ping.image.dtype == Img::DTYPE_FLOAT32 || ping.image.dtype == Img::DTYPE_FLOAT64) {
    lo = -100.0;
    hi = 10.0;
  }
  // The spin boxes' valueChanged handlers forward to the widget.
  ui_.minValueDoubleSpinBox->setValue(lo);
  ui_.maxValueDoubleSpinBox->setValue(hi);
}

void MarineEchogramPlugin::on_minValueDoubleSpinBox_valueChanged(double value)
{
  ui_.echogramWidget->setMinimumValue(value);
}

void MarineEchogramPlugin::on_maxValueDoubleSpinBox_valueChanged(double value)
{
  ui_.echogramWidget->setMaximumValue(value);
}

void MarineEchogramPlugin::on_gainDoubleSpinBox_valueChanged(double value)
{
  ui_.echogramWidget->setGain(value);
}

void MarineEchogramPlugin::on_contrastDoubleSpinBox_valueChanged(double value)
{
  ui_.echogramWidget->setContrast(value);
}

void MarineEchogramPlugin::on_paletteComboBox_currentIndexChanged(int index)
{
  ui_.echogramWidget->setColorMapIndex(index);
}

void MarineEchogramPlugin::on_pingSpacingDoubleSpinBox_valueChanged(double value)
{
  ui_.echogramWidget->setPingSpacing(value);
}

}  // namespace rqt_marine_sonar

PLUGINLIB_EXPORT_CLASS(rqt_marine_sonar::MarineEchogramPlugin, rqt_gui_cpp::Plugin)
