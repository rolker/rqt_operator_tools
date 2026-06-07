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

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <pluginlib/class_list_macros.hpp>

#include "rqt_sonar_waterfall/color_map.hpp"
#include "rqt_sonar_waterfall/control_panel.hpp"
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

  widget_ = new WaterfallWidget(container);

  auto * toolbar = new QWidget(container);
  auto * hbox = new QHBoxLayout(toolbar);
  hbox->setContentsMargins(4, 2, 4, 2);
  port_combo_ = new QComboBox(toolbar);
  starboard_combo_ = new QComboBox(toolbar);
  control_combo_ = new QComboBox(toolbar);
  auto * refresh_button = new QPushButton(tr("Refresh"), toolbar);
  hbox->addWidget(new QLabel(tr("Port:"), toolbar));
  hbox->addWidget(port_combo_, 1);
  hbox->addWidget(new QLabel(tr("Starboard:"), toolbar));
  hbox->addWidget(starboard_combo_, 1);
  hbox->addWidget(new QLabel(tr("Controls:"), toolbar));
  hbox->addWidget(control_combo_, 1);
  hbox->addWidget(refresh_button);

  // Sonar-control panel: hidden until a RadarControlSet topic is selected.
  control_panel_ = new ControlPanel();
  auto * control_scroll = new QScrollArea(container);
  control_scroll->setWidget(control_panel_);
  control_scroll->setWidgetResizable(true);
  control_scroll->setMaximumHeight(160);
  control_scroll->setVisible(false);
  control_section_ = control_scroll;
  connect(
    control_panel_, &ControlPanel::controlChanged, this,
    [this](const QString & key, const QString & value) {publish_control(key, value);});

  vbox->addWidget(toolbar);
  vbox->addWidget(build_controls_bar(container));
  vbox->addWidget(control_section_);
  vbox->addWidget(widget_, 1);

  connect(
    port_combo_, &QComboBox::currentTextChanged, this,
    [this](const QString & topic) {on_port_topic_changed(topic);});
  connect(
    starboard_combo_, &QComboBox::currentTextChanged, this,
    [this](const QString & topic) {on_starboard_topic_changed(topic);});
  connect(
    control_combo_, &QComboBox::currentTextChanged, this,
    [this](const QString & topic) {on_control_topic_changed(topic);});
  connect(refresh_button, &QPushButton::clicked, this, [this]() {refresh_topics();});

  apply_view_settings();

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
  if (args.size() >= 3 && !args[2].isEmpty()) {
    select_topic(control_combo_, args[2]);
  }

  if (context.serialNumber() > 1) {
    container->setWindowTitle(
      container->windowTitle() + " (" + QString::number(context.serialNumber()) + ")");
  }
  context.addWidget(container);
}

QWidget * SonarWaterfallPlugin::build_controls_bar(QWidget * parent)
{
  auto * bar = new QWidget(parent);
  auto * h = new QHBoxLayout(bar);
  h->setContentsMargins(4, 0, 4, 2);

  h->addWidget(new QLabel(tr("Color:"), bar));
  colormap_combo_ = new QComboBox(bar);
  for (int i = 0; i < kColorMapCount; ++i) {
    colormap_combo_->addItem(color_map_name(color_map_from_index(i)));
  }
  h->addWidget(colormap_combo_);

  h->addWidget(new QLabel(tr("Gain:"), bar));
  gain_spin_ = new QDoubleSpinBox(bar);
  gain_spin_->setRange(0.1, 10.0);
  gain_spin_->setSingleStep(0.1);
  gain_spin_->setValue(1.0);
  h->addWidget(gain_spin_);

  h->addWidget(new QLabel(tr("Contrast:"), bar));
  contrast_spin_ = new QDoubleSpinBox(bar);
  contrast_spin_->setRange(0.1, 10.0);
  contrast_spin_->setSingleStep(0.1);
  contrast_spin_->setValue(1.0);
  h->addWidget(contrast_spin_);

  h->addWidget(new QLabel(tr("History:"), bar));
  history_spin_ = new QSpinBox(bar);
  history_spin_->setRange(1, 5000);
  history_spin_->setValue(200);
  h->addWidget(history_spin_);

  auto_range_check_ = new QCheckBox(tr("Auto range"), bar);
  auto_range_check_->setChecked(true);
  h->addWidget(auto_range_check_);

  h->addWidget(new QLabel(tr("Min:"), bar));
  range_min_spin_ = new QDoubleSpinBox(bar);
  range_min_spin_->setRange(-1.0e9, 1.0e9);
  range_min_spin_->setValue(0.0);
  range_min_spin_->setEnabled(false);
  h->addWidget(range_min_spin_);

  h->addWidget(new QLabel(tr("Max:"), bar));
  range_max_spin_ = new QDoubleSpinBox(bar);
  range_max_spin_->setRange(-1.0e9, 1.0e9);
  // Pre-message fallback is the widest common depth (16-bit) so manual mode
  // never clips before any data arrives; maybe_seed_manual_range() refines this
  // to the source's exact full scale (e.g. 255 for 8-bit) on the first message.
  range_max_spin_->setValue(65535.0);
  range_max_spin_->setEnabled(false);
  h->addWidget(range_max_spin_);

  freeze_button_ = new QPushButton(tr("Freeze"), bar);
  freeze_button_->setCheckable(true);
  h->addWidget(freeze_button_);
  h->addStretch(1);

  // Any control change re-applies the full view state to the widget. The
  // widget rebuilds its cached image once per setter; at UI rates the extra
  // rebuilds are negligible and the code stays single-pathed.
  connect(
    colormap_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    [this](int) {apply_view_settings();});
  connect(
    gain_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
    [this](double) {apply_view_settings();});
  connect(
    contrast_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
    [this](double) {apply_view_settings();});
  connect(
    history_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
    [this](int) {apply_view_settings();});
  connect(
    auto_range_check_, &QCheckBox::toggled, this, [this](bool) {apply_view_settings();});
  connect(
    range_min_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
    [this](double) {apply_view_settings();});
  connect(
    range_max_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
    [this](double) {apply_view_settings();});
  connect(
    freeze_button_, &QPushButton::toggled, this, [this](bool) {apply_view_settings();});

  return bar;
}

void SonarWaterfallPlugin::apply_view_settings()
{
  if (!widget_ || !colormap_combo_) {
    return;
  }
  widget_->set_color_map(color_map_from_index(colormap_combo_->currentIndex()));
  widget_->set_gain(static_cast<float>(gain_spin_->value()));
  widget_->set_contrast(static_cast<float>(contrast_spin_->value()));
  widget_->set_history(static_cast<std::size_t>(history_spin_->value()));
  widget_->set_frozen(freeze_button_->isChecked());

  const bool automatic = auto_range_check_->isChecked();
  range_min_spin_->setEnabled(!automatic);
  range_max_spin_->setEnabled(!automatic);
  if (automatic) {
    widget_->set_auto_range(true);
  } else {
    widget_->set_manual_range(
      static_cast<float>(range_min_spin_->value()),
      static_cast<float>(range_max_spin_->value()));
  }
}

void SonarWaterfallPlugin::shutdownPlugin()
{
  if (refresh_timer_) {
    refresh_timer_->stop();
  }
  port_sub_.reset();
  starboard_sub_.reset();
  control_sub_.reset();
  control_pub_.reset();
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
  if (control_combo_) {
    instance_settings.setValue("control_topic", control_combo_->currentText());
  }
  if (colormap_combo_) {
    instance_settings.setValue("color_map", colormap_combo_->currentIndex());
    instance_settings.setValue("gain", gain_spin_->value());
    instance_settings.setValue("contrast", contrast_spin_->value());
    instance_settings.setValue("history", history_spin_->value());
    instance_settings.setValue("frozen", freeze_button_->isChecked());
    instance_settings.setValue("auto_range", auto_range_check_->isChecked());
    instance_settings.setValue("range_min", range_min_spin_->value());
    instance_settings.setValue("range_max", range_max_spin_->value());
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
  if (control_combo_ && instance_settings.contains("control_topic")) {
    select_topic(control_combo_, instance_settings.value("control_topic").toString());
  }

  if (colormap_combo_ && instance_settings.contains("color_map")) {
    colormap_combo_->setCurrentIndex(instance_settings.value("color_map").toInt());
    gain_spin_->setValue(instance_settings.value("gain", 1.0).toDouble());
    contrast_spin_->setValue(instance_settings.value("contrast", 1.0).toDouble());
    history_spin_->setValue(instance_settings.value("history", 200).toInt());
    freeze_button_->setChecked(instance_settings.value("frozen", false).toBool());
    auto_range_check_->setChecked(instance_settings.value("auto_range", true).toBool());
    range_min_spin_->setValue(instance_settings.value("range_min", 0.0).toDouble());
    // Same 16-bit-safe pre-message fallback as build_controls_bar(); a saved
    // layout missing range_max (older config) must not revert to a 15-bit clip.
    range_max_spin_->setValue(instance_settings.value("range_max", 65535.0).toDouble());
    apply_view_settings();
  }
}

void SonarWaterfallPlugin::refresh_topics()
{
  if (!node_ || !port_combo_ || !starboard_combo_) {
    return;
  }
  const auto graph = node_->get_topic_names_and_types();
  const auto image_names = raw_sonar_image_topics(graph);
  repopulate(port_combo_, image_names);
  repopulate(starboard_combo_, image_names);
  if (control_combo_) {
    repopulate(control_combo_, radar_control_set_topics(graph));
  }
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

void SonarWaterfallPlugin::on_control_topic_changed(const QString & topic)
{
  control_sub_.reset();
  control_pub_.reset();
  if (control_panel_) {
    control_panel_->clear();
  }

  const std::string name = (topic == kNoneLabel) ? std::string() : topic.toStdString();
  const bool active = !name.empty() && node_;
  if (control_section_) {
    control_section_->setVisible(active);
  }
  if (!active) {
    return;
  }

  using marine_radar_control_msgs::msg::RadarControlSet;
  using marine_radar_control_msgs::msg::RadarControlValue;
  control_sub_ = node_->create_subscription<RadarControlSet>(
    name, rclcpp::QoS(10),
    [this](RadarControlSet::ConstSharedPtr msg) {on_control_set(msg);});
  control_pub_ = node_->create_publisher<RadarControlValue>(
    derive_change_topic(name), rclcpp::QoS(10));
}

void SonarWaterfallPlugin::on_control_set(
  marine_radar_control_msgs::msg::RadarControlSet::ConstSharedPtr msg)
{
  if (!control_panel_) {
    return;
  }
  // Build the control widgets on the GUI thread.
  QPointer<ControlPanel> panel = control_panel_;
  marine_radar_control_msgs::msg::RadarControlSet set = *msg;
  QMetaObject::invokeMethod(
    panel.data(),
    [panel, set]() {
      if (panel) {
        panel->apply(set);
      }
    },
    Qt::QueuedConnection);
}

void SonarWaterfallPlugin::publish_control(const QString & key, const QString & value)
{
  if (!control_pub_) {
    return;
  }
  marine_radar_control_msgs::msg::RadarControlValue command;
  command.key = key.toStdString();
  command.value = value.toStdString();
  control_pub_->publish(command);
}

void SonarWaterfallPlugin::subscribe(
  rclcpp::Subscription<marine_acoustic_msgs::msg::RawSonarImage>::SharedPtr & sub,
  const std::string & topic, bool is_port)
{
  // Changing a subscription invalidates the prior source. Bump this side's id
  // (and clear range_seeded_) BEFORE resetting the old subscription: an old
  // in-flight callback then sees the id mismatch and returns early. Resetting
  // first would leave a window where such a callback still observes the
  // un-bumped id and proceeds to seed/post from the stale source.
  auto & sub_id = is_port ? port_sub_id_ : starboard_sub_id_;
  const uint64_t id = sub_id.fetch_add(1) + 1;
  range_seeded_.store(false);
  sub.reset();
  if (topic.empty() || !node_) {
    return;
  }
  using marine_acoustic_msgs::msg::RawSonarImage;
  if (is_port) {
    sub = node_->create_subscription<RawSonarImage>(
      topic, rclcpp::SensorDataQoS(),
      [this, id](RawSonarImage::ConstSharedPtr msg) {on_port_msg(msg, id);});
  } else {
    sub = node_->create_subscription<RawSonarImage>(
      topic, rclcpp::SensorDataQoS(),
      [this, id](RawSonarImage::ConstSharedPtr msg) {on_starboard_msg(msg, id);});
  }
}

void SonarWaterfallPlugin::on_port_msg(
  marine_acoustic_msgs::msg::RawSonarImage::ConstSharedPtr msg, uint64_t sub_id)
{
  if (sub_id != port_sub_id_.load()) {
    return;  // message from a replaced subscription; ignore.
  }
  maybe_seed_manual_range(msg->image.dtype);
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
  marine_acoustic_msgs::msg::RawSonarImage::ConstSharedPtr msg, uint64_t sub_id)
{
  if (sub_id != starboard_sub_id_.load()) {
    return;  // message from a replaced subscription; ignore.
  }
  maybe_seed_manual_range(msg->image.dtype);
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

void SonarWaterfallPlugin::maybe_seed_manual_range(uint32_t dtype)
{
  // Seed the manual-range spin default to the source's full scale once per
  // (re)subscribe, on the first message of whichever side arrives first; after
  // that the value is the operator's to set. exchange() makes the "first wins"
  // decision atomic across the racing port/starboard callbacks. Callers gate
  // this on a current subscription id (see on_*_msg), so a stale source can't
  // reach here. Ordering of the queued setValue below relies on rqt spinning the
  // node on a single executor thread (callbacks serialized, enqueued FIFO).
  if (range_seeded_.exchange(true)) {
    return;
  }
  const double full_scale = default_full_scale(dtype);
  QPointer<QDoubleSpinBox> spin = range_max_spin_;
  if (!spin) {
    return;
  }
  // Hop to the GUI thread before touching the widget (callbacks run on the
  // executor thread). The spin is disabled while auto-range is on, so updating
  // its value here only changes what the operator sees when they switch to
  // manual mode; auto-range rendering is unaffected.
  QMetaObject::invokeMethod(
    spin.data(),
    [spin, full_scale]() {
      if (spin) {
        spin->setValue(full_scale);
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
