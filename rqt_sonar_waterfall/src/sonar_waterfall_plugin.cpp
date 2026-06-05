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

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <pluginlib/class_list_macros.hpp>

#include "rqt_sonar_waterfall/topic_filter.hpp"
#include "rqt_sonar_waterfall/waterfall_model.hpp"
#include "rqt_sonar_waterfall/waterfall_widget.hpp"

namespace rqt_sonar_waterfall
{

namespace
{
const QString kNoneLabel = QStringLiteral("(none)");

// Rebuild a topic combo from the discovered names, preserving the current
// selection (and keeping it listed even if it is not currently advertised).
void repopulate(QComboBox * combo, const std::vector<std::string> & names)
{
  const QString current = combo->currentText();
  const QSignalBlocker blocker(combo);
  combo->clear();
  combo->addItem(kNoneLabel);
  for (const auto & name : names) {
    combo->addItem(QString::fromStdString(name));
  }
  if (!current.isEmpty() && current != kNoneLabel && combo->findText(current) < 0) {
    combo->addItem(current);
  }
  const int idx = combo->findText(current);
  combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

// Select a topic in a combo, adding it if not present. Emits the change signal
// so the subscription is (re)created.
void select_topic(QComboBox * combo, const QString & topic)
{
  if (topic.isEmpty() || topic == kNoneLabel) {
    combo->setCurrentIndex(0);
    return;
  }
  if (combo->findText(topic) < 0) {
    combo->addItem(topic);
  }
  combo->setCurrentText(topic);
}
}  // namespace

SonarWaterfallPlugin::SonarWaterfallPlugin()
{
  setObjectName("SonarWaterfallPlugin");
}

SonarWaterfallPlugin::~SonarWaterfallPlugin() = default;

void SonarWaterfallPlugin::initPlugin(qt_gui_cpp::PluginContext & context)
{
  auto * container = new QWidget();
  auto * vbox = new QVBoxLayout(container);
  vbox->setContentsMargins(0, 0, 0, 0);

  auto * toolbar = new QWidget(container);
  auto * hbox = new QHBoxLayout(toolbar);
  hbox->setContentsMargins(4, 2, 4, 2);
  port_combo_ = new QComboBox(toolbar);
  starboard_combo_ = new QComboBox(toolbar);
  auto * refresh_button = new QPushButton(tr("Refresh"), toolbar);
  hbox->addWidget(new QLabel(tr("Port:"), toolbar));
  hbox->addWidget(port_combo_, 1);
  hbox->addWidget(new QLabel(tr("Starboard:"), toolbar));
  hbox->addWidget(starboard_combo_, 1);
  hbox->addWidget(refresh_button);

  widget_ = new WaterfallWidget(container);

  vbox->addWidget(toolbar);
  vbox->addWidget(widget_, 1);

  connect(
    port_combo_, &QComboBox::currentTextChanged, this,
    [this](const QString & topic) {on_port_topic_changed(topic);});
  connect(
    starboard_combo_, &QComboBox::currentTextChanged, this,
    [this](const QString & topic) {on_starboard_topic_changed(topic);});
  connect(refresh_button, &QPushButton::clicked, this, [this]() {refresh_topics();});

  refresh_timer_ = new QTimer(this);
  connect(refresh_timer_, &QTimer::timeout, this, [this]() {refresh_topics();});
  refresh_timer_->start(2000);

  refresh_topics();

  // Optional positional args from the standalone launcher: port [starboard].
  const QStringList args = context.argv();
  if (args.size() >= 1 && !args[0].isEmpty()) {
    select_topic(port_combo_, args[0]);
  }
  if (args.size() >= 2 && !args[1].isEmpty()) {
    select_topic(starboard_combo_, args[1]);
  }

  if (context.serialNumber() > 1) {
    container->setWindowTitle(
      container->windowTitle() + " (" + QString::number(context.serialNumber()) + ")");
  }
  context.addWidget(container);
}

void SonarWaterfallPlugin::shutdownPlugin()
{
  if (refresh_timer_) {
    refresh_timer_->stop();
  }
  port_sub_.reset();
  starboard_sub_.reset();
}

void SonarWaterfallPlugin::saveSettings(
  qt_gui_cpp::Settings & /*plugin_settings*/,
  qt_gui_cpp::Settings & instance_settings) const
{
  if (port_combo_) {
    instance_settings.setValue("port_topic", port_combo_->currentText());
  }
  if (starboard_combo_) {
    instance_settings.setValue("starboard_topic", starboard_combo_->currentText());
  }
}

void SonarWaterfallPlugin::restoreSettings(
  const qt_gui_cpp::Settings & /*plugin_settings*/,
  const qt_gui_cpp::Settings & instance_settings)
{
  refresh_topics();
  if (port_combo_ && instance_settings.contains("port_topic")) {
    select_topic(port_combo_, instance_settings.value("port_topic").toString());
  }
  if (starboard_combo_ && instance_settings.contains("starboard_topic")) {
    select_topic(starboard_combo_, instance_settings.value("starboard_topic").toString());
  }
}

void SonarWaterfallPlugin::refresh_topics()
{
  if (!node_ || !port_combo_ || !starboard_combo_) {
    return;
  }
  const auto names = raw_sonar_image_topics(node_->get_topic_names_and_types());
  repopulate(port_combo_, names);
  repopulate(starboard_combo_, names);
}

void SonarWaterfallPlugin::on_port_topic_changed(const QString & topic)
{
  const std::string name = (topic == kNoneLabel) ? std::string() : topic.toStdString();
  subscribe(port_sub_, name, /*is_port=*/true);
  update_active_sides();
}

void SonarWaterfallPlugin::on_starboard_topic_changed(const QString & topic)
{
  const std::string name = (topic == kNoneLabel) ? std::string() : topic.toStdString();
  subscribe(starboard_sub_, name, /*is_port=*/false);
  update_active_sides();
}

void SonarWaterfallPlugin::subscribe(
  rclcpp::Subscription<marine_acoustic_msgs::msg::RawSonarImage>::SharedPtr & sub,
  const std::string & topic, bool is_port)
{
  sub.reset();
  if (topic.empty() || !node_) {
    return;
  }
  using marine_acoustic_msgs::msg::RawSonarImage;
  if (is_port) {
    sub = node_->create_subscription<RawSonarImage>(
      topic, rclcpp::SensorDataQoS(),
      [this](RawSonarImage::ConstSharedPtr msg) {on_port_msg(msg);});
  } else {
    sub = node_->create_subscription<RawSonarImage>(
      topic, rclcpp::SensorDataQoS(),
      [this](RawSonarImage::ConstSharedPtr msg) {on_starboard_msg(msg);});
  }
}

void SonarWaterfallPlugin::on_port_msg(
  marine_acoustic_msgs::msg::RawSonarImage::ConstSharedPtr msg)
{
  const auto row = extractor_.extract(*msg);
  if (!row) {
    return;
  }
  std::optional<WaterfallRow> out;
  {
    std::lock_guard<std::mutex> lock(pairer_mutex_);
    out = pairer_.submit_port(*row);
  }
  if (out) {
    post_row(*out);
  }
}

void SonarWaterfallPlugin::on_starboard_msg(
  marine_acoustic_msgs::msg::RawSonarImage::ConstSharedPtr msg)
{
  const auto row = extractor_.extract(*msg);
  if (!row) {
    return;
  }
  std::optional<WaterfallRow> out;
  {
    std::lock_guard<std::mutex> lock(pairer_mutex_);
    out = pairer_.submit_starboard(*row);
  }
  if (out) {
    post_row(*out);
  }
}

void SonarWaterfallPlugin::post_row(const WaterfallRow & row)
{
  if (!widget_) {
    return;
  }
  // Hop from the executor thread to the GUI thread before touching the widget.
  QPointer<WaterfallWidget> target = widget_;
  WaterfallRow copy = row;
  QMetaObject::invokeMethod(
    target.data(),
    [target, copy]() {
      if (target) {
        target->add_row(copy);
      }
    },
    Qt::QueuedConnection);
}

void SonarWaterfallPlugin::update_active_sides()
{
  std::lock_guard<std::mutex> lock(pairer_mutex_);
  pairer_.set_sides(static_cast<bool>(port_sub_), static_cast<bool>(starboard_sub_));
}

}  // namespace rqt_sonar_waterfall

PLUGINLIB_EXPORT_CLASS(rqt_sonar_waterfall::SonarWaterfallPlugin, rqt_gui_cpp::Plugin)
