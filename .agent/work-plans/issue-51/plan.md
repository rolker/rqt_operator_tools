# Work Plan — Issue #51: Migrate `rqt_marine_sonar` (water-column echogram), faithful jazzy port

Tracking: rolker/rqt_operator_tools#51
Source: rolker/rqt_marine_sonar @ `noetic` (3 commits, last `5daec51`)

## Goal

Bring the standalone ROS 1 echogram plugin into this repo as a new package
`rqt_marine_sonar`, ported to ROS 2 / jazzy, sitting beside `rqt_sonar_waterfall`.
**Faithful 1:1 port** — keep the QtCharts/CPU-`QImage` echogram design, preserve
every existing feature. Rendering modernization (GPU + `marine_colormap`) is a
separate follow-up and out of scope here.

## Feature inventory to preserve

- Topic combo filtered to `marine_acoustic_msgs/msg/RawSonarImage`, refresh button,
  `argv[0]` topic argument
- `saveSettings`/`restoreSettings`: topic, min dB, max dB, ping spacing
- Thread-safe ingestion (mutex + queued `invokeMethod` to GUI thread)
- Controls: min dB, max dB, ping-spacing spin boxes
- Echogram canvas: QtCharts depth axis with dynamic ticks; grayscale depth-binned
  `QImage`; 2048-ping ring buffer; mouse-wheel depth zoom (Ctrl = fine, cursor focus);
  left-drag depth pan; dB→grayscale mapping; chart title = topic
- `Ping` depth geometry from `sound_speed`/`sample0`/`samples_per_beam`/`sample_rate`;
  `sampleAt(depth)` (FLOAT32)

## ROS 1 → ROS 2 mechanics

| ROS 1 | ROS 2 |
|-------|-------|
| catkin | ament_cmake + AUTOMOC/AUTOUIC, `pluginlib_export_plugin_description_file` |
| `ros::NodeHandle::subscribe` | `node_->create_subscription<RawSonarImage>(...)` |
| `ros::master::getTopics` | `node_->get_topic_names_and_types()` (match `marine_acoustic_msgs/msg/RawSonarImage`) |
| `data_subscriber_.shutdown()` | `data_subscriber_.reset()` |
| `marine_acoustic_msgs/RawSonarImage.h` | `marine_acoustic_msgs/msg/raw_sonar_image.hpp` |
| `marine_acoustic_msgs::RawSonarImage` | `marine_acoustic_msgs::msg::RawSonarImage` |
| `std::map<ros::Time, ...>` | `std::map<int64_t /*ns*/, ...>` keyed off `header.stamp` |
| `qSort` | `std::sort` |

## Deliberate deviations (call out in PR)

1. **QoS**: subscribe with `rclcpp::SensorDataQoS()` (best_effort), matching the
   sibling waterfall and this stack's sonar publishers. The ROS 1 original had no
   QoS concept (reliable TCP); a literal reliable `QoS(10)` would silently receive
   nothing from best_effort publishers.
2. **Drop dead slot** `on_depthIntervalDoubleSpinBox_valueChanged` — declared in the
   ROS 1 header but never defined, connected, or present in the `.ui`. MOC would
   emit an undefined reference for it under ament. No depth-interval control exists;
   the dynamic-tick logic already lives in `adjustAxis()`.
3. Header extensions `.h` → `.hpp`, BSD-3-Clause headers, `resource_index` marker,
   launcher script — to match repo conventions.

## Steps

1. Commit this plan (first commit on the branch).
2. Scaffold package: `package.xml`, `CMakeLists.txt`, `plugin.xml`,
   `resource/rqt_marine_sonar`, `scripts/rqt_marine_sonar`.
3. Port sources: `ping`, `echogram_widget`, `marine_echogram_plugin` (+ `.ui`).
4. Build via `./ui_ws/build.sh rqt_marine_sonar`; fix port issues.
5. `/review-code` against the diff; address findings.
6. Push, open PR targeting `jazzy`, `Closes #51`.

## Out of scope / follow-up

File a modernization issue after this lands: fold the echogram into the waterfall's
GPU + `marine_colormap` path. That merge is a **two-way** feature port — carry the
echogram's depth zoom/pan, depth-binned geometry, and dynamic depth-axis ticks into
the shared path; do not drop them.
