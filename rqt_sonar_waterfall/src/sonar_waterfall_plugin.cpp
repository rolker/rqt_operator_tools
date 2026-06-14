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

#include <cmath>
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

// Manual-range Max default used before any message has been seen (and after a
// source switch, until the new source's first message re-seeds it). The widest
// common bit depth (16-bit) so manual mode never clips before data arrives;
// maybe_seed_manual_range() refines it to the source's exact full scale.
constexpr double kDefaultRangeMax = 65535.0;

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
  depth_combo_ = new QComboBox(toolbar);
  auto * refresh_button = new QPushButton(tr("Refresh"), toolbar);
  hbox->addWidget(new QLabel(tr("Port:"), toolbar));
  hbox->addWidget(port_combo_, 1);
  hbox->addWidget(new QLabel(tr("Starboard:"), toolbar));
  hbox->addWidget(starboard_combo_, 1);
  hbox->addWidget(new QLabel(tr("Depth:"), toolbar));
  hbox->addWidget(depth_combo_, 1);
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
  connect(
    depth_combo_, &QComboBox::currentTextChanged, this,
    [this](const QString & topic) {on_depth_topic_changed(topic);});
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
  auto * v = new QVBoxLayout(bar);
  v->setContentsMargins(4, 0, 4, 2);
  v->setSpacing(2);
  auto * h = new QHBoxLayout();
  v->addLayout(h);

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
  // Pre-message fallback (see kDefaultRangeMax); maybe_seed_manual_range()
  // refines it to the source's exact full scale on the first message.
  range_max_spin_->setValue(kDefaultRangeMax);
  range_max_spin_->setEnabled(false);
  h->addWidget(range_max_spin_);

  freeze_button_ = new QPushButton(tr("Freeze"), bar);
  freeze_button_->setCheckable(true);
  h->addWidget(freeze_button_);
  h->addStretch(1);

  // Second row: geometry / correction controls (issue #58).
  auto * h2 = new QHBoxLayout();
  v->addLayout(h2);

  ground_check_ = new QCheckBox(tr("Ground range"), bar);
  ground_check_->setChecked(true);
  ground_check_->setToolTip(
    tr("Remove the water column and show ground (horizontal) range. Needs a depth source."));
  h2->addWidget(ground_check_);

  uniform_check_ = new QCheckBox(tr("Uniform scale"), bar);
  uniform_check_->setChecked(true);
  uniform_check_->setToolTip(
    tr("Render every visible ping at one scale (auto-fit the widest)."));
  h2->addWidget(uniform_check_);

  range_lines_check_ = new QCheckBox(tr("Range lines"), bar);
  range_lines_check_->setChecked(true);
  h2->addWidget(range_lines_check_);

  h2->addWidget(new QLabel(tr("Density:"), bar));
  density_spin_ = new QDoubleSpinBox(bar);
  density_spin_->setRange(0.25, 4.0);
  density_spin_->setSingleStep(0.25);
  density_spin_->setValue(1.0);
  density_spin_->setToolTip(tr("Range-line density (higher = more lines)."));
  h2->addWidget(density_spin_);

  tvg_check_ = new QCheckBox(tr("TVG"), bar);
  tvg_check_->setChecked(false);
  tvg_check_->setToolTip(
    tr("Display time-varied gain: amplify returns with range to flatten the image."));
  h2->addWidget(tvg_check_);

  h2->addWidget(new QLabel(tr("TVG slope:"), bar));
  tvg_slope_spin_ = new QDoubleSpinBox(bar);
  tvg_slope_spin_->setRange(0.0, 3.0);
  tvg_slope_spin_->setSingleStep(0.1);
  tvg_slope_spin_->setValue(1.5);
  tvg_slope_spin_->setEnabled(false);  // enabled only when TVG is on
  h2->addWidget(tvg_slope_spin_);
  h2->addStretch(1);

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
  connect(
    ground_check_, &QCheckBox::toggled, this, [this](bool) {apply_view_settings();});
  connect(
    uniform_check_, &QCheckBox::toggled, this, [this](bool) {apply_view_settings();});
  connect(
    range_lines_check_, &QCheckBox::toggled, this, [this](bool) {apply_view_settings();});
  connect(
    density_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
    [this](double) {apply_view_settings();});
  connect(
    tvg_check_, &QCheckBox::toggled, this, [this](bool) {apply_view_settings();});
  connect(
    tvg_slope_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
    [this](double) {apply_view_settings();});

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

  // Geometry / correction controls (issue #58).
  widget_->set_ground_range(ground_check_->isChecked());
  widget_->set_uniform_scale(uniform_check_->isChecked());
  widget_->set_range_lines(range_lines_check_->isChecked());
  widget_->set_range_line_density(static_cast<float>(density_spin_->value()));
  const bool tvg_on = tvg_check_->isChecked();
  tvg_slope_spin_->setEnabled(tvg_on);
  widget_->set_tvg_slope(static_cast<float>(tvg_slope_spin_->value()));
  widget_->set_tvg(tvg_on);
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
  depth_sub_.reset();
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
  if (depth_combo_) {
    instance_settings.setValue("depth_topic", depth_combo_->currentText());
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
    instance_settings.setValue("ground_range", ground_check_->isChecked());
    instance_settings.setValue("uniform_scale", uniform_check_->isChecked());
    instance_settings.setValue("range_lines", range_lines_check_->isChecked());
    instance_settings.setValue("range_line_density", density_spin_->value());
    instance_settings.setValue("tvg", tvg_check_->isChecked());
    instance_settings.setValue("tvg_slope", tvg_slope_spin_->value());
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
  if (depth_combo_ && instance_settings.contains("depth_topic")) {
    select_topic(depth_combo_, instance_settings.value("depth_topic").toString());
  }

  if (colormap_combo_ && instance_settings.contains("color_map")) {
    colormap_combo_->setCurrentIndex(instance_settings.value("color_map").toInt());
    gain_spin_->setValue(instance_settings.value("gain", 1.0).toDouble());
    contrast_spin_->setValue(instance_settings.value("contrast", 1.0).toDouble());
    history_spin_->setValue(instance_settings.value("history", 200).toInt());
    freeze_button_->setChecked(instance_settings.value("frozen", false).toBool());
    auto_range_check_->setChecked(instance_settings.value("auto_range", true).toBool());
    range_min_spin_->setValue(instance_settings.value("range_min", 0.0).toDouble());
    // Same pre-message fallback as build_controls_bar(); a saved layout missing
    // range_max (older config) must not revert to a 15-bit clip.
    range_max_spin_->setValue(
      instance_settings.value("range_max", kDefaultRangeMax).toDouble());
    // Geometry / correction controls (issue #58) — default to the active-on
    // posture for the three geometry knobs, TVG off, matching build_controls_bar.
    ground_check_->setChecked(instance_settings.value("ground_range", true).toBool());
    uniform_check_->setChecked(instance_settings.value("uniform_scale", true).toBool());
    range_lines_check_->setChecked(instance_settings.value("range_lines", true).toBool());
    density_spin_->setValue(instance_settings.value("range_line_density", 1.0).toDouble());
    tvg_check_->setChecked(instance_settings.value("tvg", false).toBool());
    tvg_slope_spin_->setValue(instance_settings.value("tvg_slope", 1.5).toDouble());
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
  if (depth_combo_) {
    repopulate(depth_combo_, range_topics(graph));
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

void SonarWaterfallPlugin::on_depth_topic_changed(const QString & topic)
{
  depth_sub_.reset();
  latest_altitude_.store(0.0);  // drop stale altitude when the source changes
  const std::string name = (topic == kNoneLabel) ? std::string() : topic.toStdString();
  if (name.empty() || !node_) {
    return;
  }
  // Match the driver's BEST_EFFORT nadir_depth publisher; a default-reliable sub
  // would silently receive nothing (issue #58 plan-review F2).
  depth_sub_ = node_->create_subscription<sensor_msgs::msg::Range>(
    name, rclcpp::SensorDataQoS(),
    [this](sensor_msgs::msg::Range::ConstSharedPtr msg) {on_depth_msg(msg);});
}

void SonarWaterfallPlugin::on_depth_msg(sensor_msgs::msg::Range::ConstSharedPtr msg)
{
  // Range.range is the sonar altitude above the bottom (nadir depth). Ignore
  // non-finite / non-positive values (out-of-range sentinel, no bottom lock).
  const double r = static_cast<double>(msg->range);
  if (std::isfinite(r) && r > 0.0) {
    latest_altitude_.store(r);
  }
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
  // Restore the pre-message fallback so a previous source's seeded value (e.g.
  // 255 for UINT8) can't clip a newly selected source in manual mode before its
  // first message re-seeds. subscribe() runs on the GUI thread (topic-changed
  // slots / restoreSettings / initPlugin), so the spin is set directly. In
  // restoreSettings the saved range_max is applied after this and wins.
  if (range_max_spin_) {
    range_max_spin_->setValue(kDefaultRangeMax);
  }
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
  // Stamp the latest cached altitude (nadir depth). Read here on the executor
  // thread, the same thread the depth callback writes on — no lock needed.
  copy.altitude = latest_altitude_.load();
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
  // (re)subscribe, on the first message of whichever side arrives first. The
  // GUI-thread check below applies it only when auto-range is on OR the spin is
  // still at its fallback, so a deliberate/restored manual value is never
  // clobbered. exchange() makes the "first wins" decision atomic across the
  // racing port/starboard callbacks.
  // Callers gate this on a current subscription id (see on_*_msg), so a stale
  // source can't reach here. Ordering of the queued setValue below relies on rqt
  // spinning the node on a single executor thread (callbacks serialized,
  // enqueued FIFO).
  if (range_seeded_.exchange(true)) {
    return;
  }
  const double full_scale = default_full_scale(dtype);
  // Capture QPointers (not `this`) so a teardown during the queued call is a
  // no-op even if the widgets outlive nothing — same pattern as post_row, and
  // safe against the widget being destroyed before the plugin (raw member
  // pointers would dangle). The widget *state* read (isChecked) and write
  // (setValue) both run on the GUI thread.
  QPointer<QDoubleSpinBox> spin = range_max_spin_;
  QPointer<QCheckBox> auto_check = auto_range_check_;
  if (!spin) {
    return;
  }
  // Seed when EITHER auto-range is enabled (the spin is disabled, its value a
  // pending default — safe to refresh) OR the spin is still at the pre-message
  // fallback (the operator hasn't set a deliberate manual value yet). The latter
  // keeps a dtype-aware default in manual mode without clobbering a value the
  // operator (or a restored config) deliberately set. range_seeded_ is consumed
  // either way, so this one-shot decision must cover the manual-from-start case
  // too — otherwise the spin would stay stuck at the 65535 fallback for an 8-bit
  // source. Exact == is reliable: kDefaultRangeMax is set programmatically.
  QMetaObject::invokeMethod(
    spin.data(),
    [spin, auto_check, full_scale]() {
      if (!spin) {
        return;
      }
      const bool auto_on = auto_check && auto_check->isChecked();
      const bool untouched = (spin->value() == kDefaultRangeMax);
      if (auto_on || untouched) {
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
