# Work Plan: Issue #20 — rqt_camera_grid (C++)

**Issue**: [feat: rqt_camera_grid — multi-stream image grid with staleness border](https://github.com/rolker/rqt_operator_tools/issues/20)
**Branch**: `feature/issue-20`
**Repo**: `rolker/rqt_operator_tools` (default branch: `jazzy`)

## Summary

Add a new `rqt_camera_grid` package to this repo: a **C++ rqt plugin**
(`rqt_gui_cpp`) that displays N `image_transport` streams in a configurable
grid, with a per-pane staleness border (neutral / amber / red) that matches
`rqt_annunciator`'s dark-until-problem convention. Secondary goal: declare
`ffmpeg_image_transport` as `exec_depend` so a fresh `rosdep install` on an
operator station pulls the H.265 decoder needed by the bizzyboat / izzyboat
rollout
([unh_marine_perception#4](https://github.com/rolker/unh_marine_perception/issues/4),
[unh_echoboats_project11#78](https://github.com/rolker/unh_echoboats_project11/issues/78)).

## Language choice: C++ over Python

This repo's precedent is Python (`rqt_annunciator`, `rqt_operator_log`), so the
default pull was Python. We're diverging because:

- **Reference implementation is C++**: upstream `rqt_image_view` on jazzy is a
  C++ `rqt_gui_cpp` plugin with essentially the dependency set we need
  (`rqt_gui_cpp`, `image_transport`, `cv_bridge`, `sensor_msgs`, Qt5). We crib
  **its structure** (CMake layout, pluginlib wiring, Qt thread marshaling),
  not its behavior — see stability caveat below.
- **Zero-copy rendering**: `QImage(Image::data.data(), w, h, step, Format_RGB888)`
  is a pointer view of the `sensor_msgs::msg::Image` buffer — no per-frame copy
  between Python and Qt. Relevant at higher pane counts.
- **Native `image_transport::Subscriber`**: the C++ API is first-class; the
  Python binding is a wrapper. No ambiguity about `FFMPEGPacket` vs decoded
  `Image` delivery — the plugin's `getTransport()` handles decode inside the
  subscriber and hands us `sensor_msgs::msg::Image`.

Cost we accept: `ament_cmake` + MOC + Qt build complexity instead of a flat
Python package; slower iteration; first C++ package in this repo.

### Stability caveat: rqt_image_view is a **structural** reference only

`rqt_image_view` has field-observed crashes on operator stations (e.g., when
refreshing the topic list in the dropdown). We do not copy its patterns
blindly. Explicit design rules this plugin must follow:

1. **Topic discovery is dialog-scoped and snapshot-based**. Populate the
   `(base, transport)` dropdown when the config dialog opens by calling
   `node->get_topic_names_and_types()` once, operating on that snapshot, and
   closing the dialog commits whatever was selected. Do not refresh topics
   continuously while subscriptions are live, and do not tear down
   subscriptions from the refresh path.
2. **Reconfigure runs on the Qt main thread, with subscriptions serialized**.
   When the user applies new config, tear down existing `image_transport::Subscriber`
   objects first (on the Qt main thread via `QMetaObject::invokeMethod` /
   queued connection), wait for their destructors to return, then construct
   the new ones. Never swap subscriptions from inside an incoming-image
   callback.
3. **Callback → UI marshaling is explicit and signal-based**. image_transport
   callbacks arrive on the `rclcpp::executors::MultiThreadedExecutor` thread.
   Each pane emits a Qt signal with the `Image::ConstSharedPtr` captured by
   value; the pane's paint slot runs on the main thread. This keeps
   `sensor_msgs::msg::Image` storage alive across the thread boundary via
   `shared_ptr` refcount, avoiding use-after-free on rapid reconfigure.
4. **Paint slot is defensive**. Guard on `msg->width > 0 && msg->height > 0`,
   on encoding match (or `cv_bridge` conversion success), and on
   `!label_->isDestroyed()`-equivalent widget state before painting.
5. **Shutdown order is explicit**. `CameraGridWidget::~CameraGridWidget` and
   `CameraGridPlugin::shutdownPlugin` must destruct panes (and therefore their
   subscribers) **before** letting the node go out of scope. Verify with
   valgrind on at least a two-pane reconfigure-heavy scenario before merging.
6. **Regression tests for the fragile paths**: write gtests that construct and
   destruct `CameraPaneWidget` rapidly while pushing dummy `Image` messages
   through the subscriber callback; use `QT_QPA_PLATFORM=offscreen` so this
   runs in CI.

Before importing any specific code pattern from `rqt_image_view` (e.g., its
`on_topic_changed` handler, its `QComboBox` setup, its `rgb8`/`bgr8`
conversion path), understand the pattern and its failure modes first. "It
works in rqt_image_view" is not evidence of safety.

## Principles and ADRs Considered

| Principle / ADR | How it applies |
|---|---|
| **Only what's needed** | Ship the core grid + staleness border + perspective save/restore. Defer click-to-expand, FPS limiter, and recording-status overlay to follow-up issues. |
| **Improve incrementally** | Single reviewable PR; nice-to-haves land later. |
| **Capture decisions** | Record the language choice (C++, cribbing from `rqt_image_view`) and the per-pane `{base, transport}` config model in the package `README.md`. Both are load-bearing and non-obvious. |
| **A change includes its consequences** | Same PR updates repo-root `README.md` (new pkg + pre-existing missing `rqt_operator_log` entry), ships a default config, and adds gtest unit tests. |
| **Test what breaks** | Gtest the staleness state machine (neutral → warn → error → recovered) and config YAML roundtrip. GUI layout / letterbox left to manual acceptance. |
| **Workspace vs. project separation** | Plugin stays platform-agnostic — no bizzyboat defaults in code; default config is a 2×2 empty placeholder. |
| **ADR-0008 (ROS 2 conventions)** | `package.xml` format 3, `ament_cmake`, BSD-3-Clause, pluginlib export via `plugin_description.xml`, REP-144 naming. Matches `rqt_image_view`. |
| **ADR-0009 (Python packaging)** | N/A for a C++ package; but `ffmpeg_image_transport` still lands via `rosdep` in `package.xml`, not pip. |

## Package Structure

```
rqt_operator_tools/
├── rqt_camera_grid/                              # NEW C++ package
│   ├── package.xml                               # format 3, ament_cmake
│   ├── CMakeLists.txt                            # find_package Qt5/rclcpp/rqt_gui_cpp; MOC; pluginlib export
│   ├── plugin.xml                                # rqt GUI descriptor (menu/label/icon)
│   ├── plugin_description.xml                    # pluginlib descriptor for PLUGINLIB_EXPORT_CLASS
│   ├── README.md                                 # design section: language choice, config model, perspective schema
│   ├── config/default_camera_grid.yaml           # 2×2 empty placeholder
│   ├── include/rqt_camera_grid/
│   │   ├── camera_grid_plugin.hpp                # : public rqt_gui_cpp::Plugin
│   │   ├── camera_grid_widget.hpp                # : public QWidget
│   │   ├── camera_pane_widget.hpp                # : public QFrame
│   │   ├── staleness_tracker.hpp                 # pure logic, no Qt/ROS
│   │   └── config_model.hpp                      # PaneConfig / GridConfig structs + yaml I/O
│   ├── src/
│   │   ├── camera_grid_plugin.cpp                # PLUGINLIB_EXPORT_CLASS + rqt save/restore
│   │   ├── camera_grid_widget.cpp                # QGridLayout container, perspective I/O
│   │   ├── camera_pane_widget.cpp                # image_transport::Subscriber, QImage render, border
│   │   ├── staleness_tracker.cpp
│   │   ├── config_model.cpp                      # yaml-cpp load/save
│   │   └── config_dialog.cpp                     # grid-dims + per-pane (base, transport) editor
│   └── test/
│       ├── test_staleness_tracker.cpp            # gtest
│       ├── test_config_model.cpp                 # gtest
│       └── CMakeLists.txt (or inline)            # ament_add_gtest
```

## Design Decisions

### Per-pane config: `{base, transport}` struct, not a single topic string

```yaml
grid: { rows: 2, cols: 2 }
panes:
  - { base: /bizzy/sensors/cameras/oak_forward/image_raw,   transport: ffmpeg,     warn_s: 1.0, error_s: 3.0 }
  - { base: /bizzy/sensors/cameras/oak_starboard/image_raw, transport: ffmpeg,     warn_s: 1.0, error_s: 3.0 }
  - { base: /bizzy/sensors/cameras/oak_aft/segmentation,    transport: compressed, warn_s: 2.0, error_s: 6.0 }
  - { base: /some/usb_cam/image_raw,                        transport: raw,        warn_s: 1.0, error_s: 3.0 }
```

```cpp
it_.reset(new image_transport::ImageTransport(node_));
sub_ = it_->subscribe(pane.base, 1,
         std::bind(&CameraPaneWidget::on_image, this, _1),
         nullptr, image_transport::TransportHints(node_.get(), pane.transport));
```

The base topic may never be advertised (bizzyboat publishes only the `/ffmpeg`
sibling). `image_transport::Subscriber` builds the full name as
`<base>/<suffix>` and subscribes directly — it does not require the base to
exist.

### Staleness state machine (pure, gtest-able)

```cpp
class StalenessTracker {
 public:
  enum class Level { Neutral, Warn, Error };
  StalenessTracker(double warn_s, double error_s);
  void mark_frame(rclcpp::Time now);
  Level tick(rclcpp::Time now) const;   // called from a QTimer at 1 Hz
};
```

Transitions:

```
                     frame arrives
  Neutral <────────────────────────── Warn / Error
      │  age > warn_s                    ▲
      ▼                                   │
    Warn ─── age > error_s ─────────▶ Error
```

No Qt or ROS dependency inside the class — unit-testable with `rclcpp::Time`
constructed from `builtin_interfaces`. Pane widget owns an instance, calls
`mark_frame()` from `on_image()`, and `tick()` from the 1 Hz timer.

### Image rendering: zero-copy QImage view

For `sensor_msgs::msg::Image` with `encoding == "rgb8"`:

```cpp
QImage view(msg->data.data(), msg->width, msg->height, msg->step, QImage::Format_RGB888);
pixmap_ = QPixmap::fromImage(view.scaled(label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
```

For `bgr8` and other common encodings, convert via `cv_bridge` (fallback path).
`ffmpeg_image_transport` decodes to `bgr8` by default, so the `cv_bridge` path
is exercised for our primary deployment — `cv_bridge::toCvShare(msg, "rgb8")`
then wrap the `cv::Mat` buffer as a `QImage`.

### Config dialog UX

`rclcpp::Node::get_topic_names_and_types()` gives all advertised topics. The
dialog filters to those whose name ends in a known transport suffix
(`/ffmpeg`, `/compressed`, `/compressedDepth`, `/theora`) or whose type is
`sensor_msgs/msg/Image` / `sensor_msgs/msg/CompressedImage` /
`ffmpeg_image_transport_msgs/msg/FFMPEGPacket`. Each parsed into a `(base,
transport)` dropdown entry. Free-form fallback fields cover the ffmpeg-only
deployment where the base isn't advertised.

### Configuration lifecycle

We mirror `rqt_annunciator` exactly. Three layers feed the widget's
in-memory `GridConfig`; whichever was set most recently wins.

| Layer | When | Mechanism |
|---|---|---|
| 1. Shipped default | Plugin construction | Load `share/rqt_camera_grid/config/default_camera_grid.yaml` via `ament_index_cpp::get_package_share_directory`. Baseline so the grid isn't empty on first launch. |
| 2. rqt perspective | After construction, and on shutdown | `qt_gui_cpp::Plugin::restoreSettings(plugin_settings, instance_settings)` reads `instance_settings.value("config_yaml")`. If present, **overrides the default**. `saveSettings(...)` writes the current config back on close. |
| 3. User edit via config dialog | Any time the user clicks rqt's wrench | `triggerConfiguration()` opens the config dialog (see below). On OK, the dialog's config replaces the widget's; rqt persists it to the perspective at shutdown via layer 2. |

**User flow for "load a YAML file and keep it"**: open config dialog → Import
YAML... → pick file → OK → (rqt close or perspective save) → perspective now
contains that config. Next rqt open, `restoreSettings` brings it back. No
special handling needed — the dialog's import path only updates the dialog's
in-memory model, and the same OK-then-save mechanism persists it.

### Config dialog capabilities (matches annunciator)

`config_dialog.cpp` exposes:

- **Grid dims**: two `QSpinBox`es for rows × cols; applying either resizes
  the pane list (truncate / append empty panes).
- **Pane list** (`QListWidget`): select a pane to edit; **+** / **−**
  buttons add/remove panes. Pane label = `base` (or `(empty)` if unset).
- **Pane editor** (right pane of dialog):
  - **Base topic**: `QComboBox` (editable) populated from a snapshot of
    `node->get_topic_names_and_types()` at dialog-open — see stability rule
    1. Each entry is a `(base, transport)` pair parsed from the advertised
    topic name/type. Free-form text entry is allowed so the operator can
    type a base topic that isn't currently advertised.
  - **Transport**: `QComboBox` (`raw`, `compressed`, `compressedDepth`,
    `theora`, `ffmpeg`). Auto-filled when the user picks a dropdown entry
    from the base combo.
  - **Warn threshold**: `QDoubleSpinBox` (seconds).
  - **Error threshold**: `QDoubleSpinBox` (seconds).
- **Import YAML...** button (`QFileDialog::getOpenFileName`): replaces the
  dialog's in-memory `GridConfig` with the file's contents, repopulates the
  pane list. Parse errors surface via `QMessageBox::warning`.
- **Export YAML...** button (`QFileDialog::getSaveFileName`): writes the
  dialog's current `GridConfig` to the chosen path. Useful for staging
  configs on disk and committing them to platform repos.
- **OK / Cancel** (`QDialogButtonBox`): OK atomically applies the whole
  config — one reconfigure on the Qt main thread (stability rule 2). Cancel
  discards the dialog's changes; existing subscriptions are untouched.

No live mutation of subscriptions while the dialog is open. The dialog edits
an in-memory `GridConfig` copy; subscription churn happens only once, in
`CameraGridWidget::load_config` triggered by OK.

## Dependencies (package.xml)

```xml
<buildtool_depend>ament_cmake</buildtool_depend>
<buildtool_depend>ament_cmake_ros</buildtool_depend>

<depend>rclcpp</depend>
<depend>rqt_gui</depend>
<depend>rqt_gui_cpp</depend>
<depend>qt_gui_cpp</depend>
<depend>pluginlib</depend>
<depend>image_transport</depend>
<depend>sensor_msgs</depend>
<depend>cv_bridge</depend>
<depend>libqt5-widgets</depend>
<depend>yaml-cpp</depend>
<depend>ament_index_cpp</depend>

<exec_depend>ffmpeg_image_transport</exec_depend>

<test_depend>ament_cmake_gtest</test_depend>
<test_depend>ament_lint_auto</test_depend>
<test_depend>ament_lint_common</test_depend>

<export>
  <build_type>ament_cmake</build_type>
  <rqt_gui plugin="${prefix}/plugin.xml"/>
</export>
```

`ffmpeg_image_transport` as `exec_depend` is the secondary goal: `rosdep
install` on a fresh salmon pulls the H.265 decoder plugin. Binary is available
on jazzy (`ros-jazzy-ffmpeg-image-transport`), verified.

## CMake skeleton

```cmake
cmake_minimum_required(VERSION 3.10)
project(rqt_camera_grid)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rqt_gui_cpp REQUIRED)
find_package(qt_gui_cpp REQUIRED)
find_package(pluginlib REQUIRED)
find_package(image_transport REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(Qt5 COMPONENTS Widgets REQUIRED)

set(CMAKE_AUTOMOC ON)

add_library(${PROJECT_NAME} SHARED
  src/camera_grid_plugin.cpp
  src/camera_grid_widget.cpp
  src/camera_pane_widget.cpp
  src/staleness_tracker.cpp
  src/config_model.cpp
  src/config_dialog.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC include)
ament_target_dependencies(${PROJECT_NAME}
  rclcpp rqt_gui_cpp qt_gui_cpp pluginlib image_transport sensor_msgs cv_bridge
  ament_index_cpp)
target_link_libraries(${PROJECT_NAME} Qt5::Widgets yaml-cpp)

pluginlib_export_plugin_description_file(rqt_gui plugin.xml)

install(TARGETS ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(FILES plugin.xml plugin_description.xml DESTINATION share/${PROJECT_NAME})
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_staleness_tracker test/test_staleness_tracker.cpp src/staleness_tracker.cpp)
  target_include_directories(test_staleness_tracker PRIVATE include)
  ament_add_gtest(test_config_model test/test_config_model.cpp src/config_model.cpp)
  target_include_directories(test_config_model PRIVATE include)
  target_link_libraries(test_config_model yaml-cpp)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
```

## Implementation Phases

### Phase 1 — This PR

1. **Scaffolding**: `package.xml`, `CMakeLists.txt`, `plugin.xml`, `plugin_description.xml`, `README.md`.
2. **`staleness_tracker.{hpp,cpp}`**: pure C++ state machine.
3. **`config_model.{hpp,cpp}`**: `PaneConfig`, `GridConfig` structs + `yaml-cpp` load/save; defaults.
4. **`camera_pane_widget.{hpp,cpp}`**: `QFrame` subclass with child `QLabel` for image + top-strip label; `image_transport::Subscriber` member; border color via palette; uses `cv_bridge::toCvShare` for non-rgb8 frames, zero-copy `QImage` view otherwise.
5. **`camera_grid_widget.{hpp,cpp}`**: `QGridLayout` container; constructs panes from `GridConfig`; owns a shared `image_transport::ImageTransport`; 1 Hz `QTimer` ticks all panes.
6. **`camera_grid_plugin.{hpp,cpp}`**: `rqt_gui_cpp::Plugin` subclass + `PLUGINLIB_EXPORT_CLASS` macro; `saveSettings`/`restoreSettings`/`triggerConfiguration`.
7. **`config_dialog.{hpp,cpp}`**: grid-dims spinboxes + pane table with dropdown-or-free-form `(base, transport)` fields, populated via `node->get_topic_names_and_types()`.
8. **`config/default_camera_grid.yaml`**: empty 2×2 placeholder.
9. **Tests**: `test_staleness_tracker.cpp` (transitions + boundary ticks), `test_config_model.cpp` (YAML roundtrip + validation), `test_pane_lifecycle.cpp` (construct/destruct `CameraPaneWidget` rapidly with dummy `Image` messages via `QT_QPA_PLATFORM=offscreen` — the regression test for stability rule 6).
10. **Repo root `README.md`**: add `rqt_camera_grid` entry AND the missing `rqt_operator_log` entry.

### Phase 2 — Follow-up issues (file separately)

- Click-to-expand a pane to fill the grid.
- Per-pane FPS limiter (QElapsedTimer-based drop).
- Recording-status overlay coordinated with `rqt_operator_log`.
- Auto-threshold from observed frame rate (if manual defaults prove awkward).
- Standalone (non-rqt) entry point if operators want a bare window. Non-trivial in C++ (`QApplication` + `rclcpp::Node` + event-loop bridging); not worth it unless asked for.

## Files to Change

| File | Change |
|------|--------|
| `rqt_camera_grid/**` | New C++ package (all files listed above) |
| `README.md` | Add `rqt_camera_grid` entry; add missing `rqt_operator_log` entry |
| `.agent/work-plans/PLAN_ISSUE-20.md` | This plan |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Add a new package | Repo root `README.md` | Yes |
| Add `ffmpeg_image_transport` rosdep | PR description verifies `rosdep install` resolves it on a fresh salmon shell | Yes (call out in PR) |
| Define perspective schema | Document schema in package `README.md` | Yes |
| First C++ package in this repo | Note the divergence from Python precedent in package `README.md`; confirm CI (`./ui_ws/build.sh`) picks up ament_cmake tests alongside ament_python | Yes |

## Open Questions

1. **Qt dep spelling in `package.xml`**: `libqt5-widgets`? `qtbase5-dev`? ROS 2
   jazzy's `rosdep` keys vary across distros. Crib the exact key from
   `rqt_image_view`'s `package.xml` on jazzy before first push of the build.
2. **Default config content**: Ship an empty 2×2 placeholder, or ship a
   commented bizzyboat-style example (clearly tagged as "example")? The
   "platform-agnostic" principle argues for empty; usability argues for a
   commented example. Leaning empty.
3. **Rate label source**: Self-measured from incoming-frame stamps (simple,
   reflects what operators care about — is *this display* keeping up?) vs.
   subscribing to an existing topic-statistics source. Default to
   self-measured unless there's a reason not to.

## Risk / Complexity Notes

- **First C++ package in this repo**: adds ament_cmake to the build surface
  (this repo has only been ament_python so far). Verify `./ui_ws/build.sh`
  and the test runner handle the mixed layout without extra flags.
- **MOC + Qt5** add build time relative to the Python plan. Acceptable.
- **Thread safety**: image_transport callbacks arrive on a ROS executor
  thread; UI updates happen on the Qt main thread. See stability caveat
  item 3 — signal-based marshaling with `ConstSharedPtr` payloads, not a
  `QMetaObject::invokeMethod` copy of a `cv::Mat` view.
- **Reference-implementation crashes**: `rqt_image_view` has field-observed
  instability (topic-refresh crashes). The stability caveat section lists
  six rules to prevent inheriting those failure modes; the regression tests
  in rule 6 are the most important anti-regression signal.

## Estimated Scope

Single PR. Package size comparable to `rqt_image_view` (~1500–2500 LoC
including tests and CMake). Nice-to-haves deferred keeps this reviewable.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus 4.7 (1M context)`
