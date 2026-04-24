# Work Plan: Issue #20 — rqt_camera_grid (C++)

**Issue**: [feat: rqt_camera_grid — multi-stream image grid with staleness border](https://github.com/rolker/rqt_operator_tools/issues/20)
**Branch**: `feature/issue-20`
**Repo**: `rolker/rqt_operator_tools` (default branch: `jazzy`)

## Summary

Add a new `rqt_camera_grid` package to this repo: a **C++ rqt plugin**
(`rqt_gui_cpp`) that displays N `image_transport` streams in a configurable
grid, with a per-pane staleness border (neutral / amber / red) that matches
`rqt_annunciator`'s dark-until-problem convention. Secondary goal: declare
the image_transport plugins (`compressed`, `compressedDepth`, `theora`,
`ffmpeg`) as `exec_depend` so a fresh `rosdep install` on an operator
station pulls every transport the config dialog advertises, including the
H.265 decoder needed by the bizzyboat / izzyboat rollout
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
│   ├── plugin.xml                                # single file: pluginlib class export + rqt GUI descriptor (matches rqt_image_view)
│   ├── README.md                                 # design section: language choice, config model, perspective schema
│   ├── config/default_camera_grid.yaml           # 2×2 empty placeholder
│   ├── include/rqt_camera_grid/
│   │   ├── camera_grid_plugin.hpp                # : public rqt_gui_cpp::Plugin
│   │   ├── camera_grid_widget.hpp                # : public QWidget
│   │   ├── camera_pane_widget.hpp                # : public QFrame
│   │   ├── staleness_tracker.hpp                 # pure logic, uses rclcpp::Time (no Qt)
│   │   ├── grid_layout.hpp                       # pure geometry (W,H,R,C,A)->rects
│   │   └── config_model.hpp                      # PaneConfig / GridConfig structs + yaml I/O
│   ├── src/
│   │   ├── camera_grid_plugin.cpp                # PLUGINLIB_EXPORT_CLASS + rqt save/restore
│   │   ├── camera_grid_widget.cpp                # resizeEvent -> grid_layout; perspective I/O
│   │   ├── camera_pane_widget.cpp                # image_transport::Subscriber, QImage render, border
│   │   ├── staleness_tracker.cpp
│   │   ├── grid_layout.cpp                       # pure fn; no Qt/ROS
│   │   ├── config_model.cpp                      # yaml-cpp load/save
│   │   └── config_dialog.cpp                     # grid-dims + per-pane (base, transport) editor
│   └── test/
│       ├── test_staleness_tracker.cpp            # gtest
│       ├── test_config_model.cpp                 # gtest
│       ├── test_grid_layout.cpp                  # gtest
│       ├── test_pane_lifecycle.cpp               # gtest (Qt offscreen)
│       └── CMakeLists.txt (or inline)            # ament_add_gtest
```

## Design Decisions

### Per-pane config: `{base, transport}` struct, not a single topic string

```yaml
grid: { rows: 2, cols: 2 }
panes:
  - { base: /bizzy/sensors/cameras/oak_forward/image_raw,   transport: ffmpeg,     warn_s: 2.0, error_s: 5.0 }
  - { base: /bizzy/sensors/cameras/oak_starboard/image_raw, transport: ffmpeg,     warn_s: 2.0, error_s: 5.0 }
  - { base: /bizzy/sensors/cameras/oak_aft/segmentation,    transport: compressed, warn_s: 2.0, error_s: 5.0 }
  - { base: /some/usb_cam/image_raw,                        transport: raw,        warn_s: 2.0, error_s: 5.0 }
```

```cpp
it_.reset(new image_transport::ImageTransport(node_));
image_transport::TransportHints hints(node_.get(), pane.transport);
sub_ = it_->subscribe(
         pane.base,
         rmw_qos_profile_sensor_data,   // QoS — see "Subscription QoS" below
         std::bind(&CameraPaneWidget::on_image, this, _1),
         image_transport::VoidPtr(),
         &hints,                         // TransportHints is a pointer in this overload
         rclcpp::SubscriptionOptions());
```

The base topic may never be advertised (bizzyboat publishes only the `/ffmpeg`
sibling). `image_transport::Subscriber` builds the full name as
`<base>/<suffix>` and subscribes directly — it does not require the base to
exist.

### Subscription QoS: `sensor_data`, not default

Every image subscriber in this workspace uses `rclcpp::SensorDataQoS()` (see
`sea_surface_segmentation/src/{sea_surface_layer,segments_to_pointcloud}.cpp`,
`cube_bathymetry/src/{cube_bathymetry_node,detections_to_pointcloud}.cpp`).
Publishers match. The default `image_transport::Subscriber` overload that
takes a `queue_size` produces a **reliable, keep-last-N** QoS, which will not
match best-effort publishers — no frames delivered, staleness goes red and
stays red.

We use the QoS-aware `subscribe()` overload (verified in
`/opt/ros/jazzy/include/image_transport/image_transport/image_transport.hpp`):

```cpp
Subscriber subscribe(
    const std::string & base_topic, rmw_qos_profile_t custom_qos,
    const Subscriber::Callback & callback,
    const VoidPtr & tracked_object,
    const TransportHints * transport_hints,
    const rclcpp::SubscriptionOptions options);
```

Pass `rmw_qos_profile_sensor_data` for `custom_qos`. If we ever need to tweak
depth (e.g., `depth = 1` because we only paint the latest frame), clone the
profile and mutate:

```cpp
rmw_qos_profile_t qos = rmw_qos_profile_sensor_data;
qos.depth = 1;
```

Tests confirm the subscription shape (reliability = BestEffort, depth = 5
default) in `test_pane_lifecycle.cpp` via `get_actual_qos()` on the created
subscription.

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

No Qt dependency inside the class; uses `rclcpp::Time` so staleness math
stays consistent with the node clock under `use_sim_time`. Pane widget
owns an instance, calls `mark_frame()` from `on_image()`, and `tick()`
from the 1 Hz timer. `tick()` treats a negative age (time moved backwards,
e.g. sim clock restart) as `Error`, so the indicator stays conservative
through clock discontinuities until the next frame arrives.

**Default thresholds**: `warn_s = 2.0, error_s = 5.0`. Chosen for
operator-station consistency: these are the green-at-2s / yellow-at-5s
boundaries operators already see in other staleness-indicating tools
on the operator station, so moving between tools doesn't require
recalibrating expectations. This is a conscious deviation from the
issue's suggested rate-based defaults ("warn at 3× the period, error
at 10×") — cross-tool consistency on the operator station outweighs
per-stream-rate adaptation for v1. If operators later find fixed
thresholds don't match actual stream behavior, adaptive thresholds remain
an easy follow-up (see Phase 2).

### Image rendering: single-copy via QPixmap::fromImage

For `sensor_msgs::msg::Image` with `encoding == "rgb8"`:

```cpp
QImage view(msg->data.data(), msg->width, msg->height, msg->step, QImage::Format_RGB888);
pixmap_ = QPixmap::fromImage(view);   // one copy to the display format
```

`QPixmap::fromImage` is documented to return a pixmap that is a copy of the
input, so the temporary `view` aliasing the ROS buffer does not need to
outlive the conversion — the returned pixmap owns its pixels. Scaling to
the pane's image rect is deferred to `paintEvent` via a cached scaled
pixmap (rebuilt on new frame or rect change), keeping repaints cheap.

For `bgr8` and other common encodings, convert via `cv_bridge` (fallback
path). `ffmpeg_image_transport` decodes to `bgr8` by default, so the
`cv_bridge` path is exercised for our primary deployment —
`cv_bridge::toCvCopy(msg, "rgb8")` (`toCvShare` cannot help across encoding
conversion), then wrap the `cv::Mat` buffer as a `QImage` and hand it to
`QPixmap::fromImage` inside the try scope so the `cv_ptr` outlives the
fromImage copy.

**Unsupported-encoding policy**: `rgb8` and `bgr8` render. `mono8` renders
via `cv_bridge::toCvCopy(msg, "rgb8")` (automatic grayscale → RGB
conversion). Any other encoding (`16UC1`, `32FC1`, depth formats, unknown
strings) triggers a one-time `RCLCPP_WARN` per pane naming the encoding, and
the pane renders a plain dark background with its topic label and staleness
border still operational. One pane with an exotic encoding never crashes or
empties the grid.

### Grid layout: image-aspect-aware, outside-padding-only

Goal: **minimize empty space *between* image panes**. Slack from aspect-ratio
mismatch between the widget and the camera images goes to the outer edges of
the grid, not between neighboring panes.

Do **not** use a plain `QGridLayout` with uniform cell sizes — that puts slack
everywhere. Instead, `CameraGridWidget` overrides `resizeEvent` and computes
child geometries directly.

**Algorithm** (widget size `W × H`, grid `R × C`, target image aspect `A`):

1. **Pick target aspect `A`**: use the median of observed per-pane image
   aspect ratios. Until any frames arrive, default to `16 / 9`. On
   `load_config` (reconfigure), target_aspect is reset to the `16 / 9`
   default so the new panes don't inherit the previous config's median —
   the first new pane's `firstFrameSeen` will adjust it.
2. **Compute tight cell size preserving `A`**:
   - `cell_h_by_height = H / R`; `cell_w_candidate = cell_h_by_height * A`.
   - If `C * cell_w_candidate ≤ W`: height-limited. `cell_w = cell_w_candidate`,
     `cell_h = cell_h_by_height`. Horizontal slack `= W - C*cell_w`.
   - Else: width-limited. `cell_w = W / C`, `cell_h = cell_w / A`. Vertical
     slack `= H - R*cell_h`.
3. **Distribute slack to outer edges only**: split horizontal slack equally
   between the left of column 0 and the right of column C−1. Same for vertical
   slack above row 0 and below row R−1. Inner cell boundaries sit at
   `outer_pad + i * cell_w` / `outer_pad + j * cell_h` — shared, no gap.
4. **Edge cells get widget geometry that includes the outer padding on their
   outside edge(s)**; their image render rect is anchored toward the grid
   interior (`Qt::AlignRight` for the left column, `Qt::AlignLeft` for the
   right column, etc.). Inner cells have no outer padding — their widget
   geometry equals their image render rect.
5. **Staleness border is drawn around the image render rect, not the full
   cell widget**. Otherwise edge-cell borders would be off-center. The outer
   padding region of an edge cell paints the plain dark background only.
6. **Mixed per-pane aspects**: panes whose observed aspect differs from the
   target `A` letterbox *within their cell's image render rect* (standard
   `Qt::KeepAspectRatio` inside the rect). This keeps mixed deployments
   working without sacrificing the clean-fit of the majority.
7. **Recompute layout** in `resizeEvent` and whenever any pane's observed
   aspect changes the median.
8. **Logical pixels**: all `W`, `H`, cell sizes, and child geometries are in
   Qt logical pixels (`widget->width()` / `widget->height()`), not device
   pixels. Qt handles device-pixel scaling via
   `QWidget::devicePixelRatioF()` at paint time — we never multiply by it in
   the layout math. Verifies correctly on 4K operator displays.
9. **First-frame relayout**: each pane starts with the target aspect `A`
   (initially 16/9). When its first `Image` arrives, the pane reports its
   observed aspect to `CameraGridWidget`, which updates the median and
   triggers one relayout. Subsequent frames on that pane do not re-trigger
   relayout unless the pane's observed aspect changes the median (expected
   to be rare). Acceptable one-time visual jump when a pane first comes
   online; predictable behavior from then on.

Example, 2×2 of 16:9 cameras in a 1920×800 widget:

- Tight: `cell_h = 400`, `cell_w = cell_h * 16/9 ≈ 711`. `2*711 = 1422 ≤ 1920`,
  height-limited. Horizontal slack `= 1920 - 1422 = 498`.
- Left column widget spans `x ∈ [0, 711 + 249)`, image rect spans
  `x ∈ [249, 960)`. Right column widget spans `x ∈ [960, 1920)`, image rect
  spans `x ∈ [960, 1671)`. Inner vertical boundary at `x = 960` — shared.
  Empty-between-images width: `0`. Empty-at-outside width: `249` left +
  `249` right.

This is implementable in ~50 lines of `resizeEvent` logic; tests live in
`test_grid_layout.cpp` (pure function: `(W, H, R, C, A) → per-cell geometry`).

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

### Config dialog: keep logic testable without a `QApplication`

Qt widget testing is painful. Avoid it by pushing the dialog's non-Qt logic
into free functions in `config_model.{hpp,cpp}`, where gtest can exercise
them directly:

- **`void resize_panes(GridConfig&, int new_rows, int new_cols)`** — applies
  the row-major rule: truncate trailing panes when shrinking, append
  default-constructed `PaneConfig{}` entries when growing. Every dialog
  operation that mutates `config_` (`onAddPane`, `onRemovePane`,
  `onImportYaml`, rows/cols spinbox change) calls this to maintain the
  `panes.size() == rows*cols` invariant, so each grid cell always has
  exactly one editable list entry. The widget also calls it in
  `load_config` (logging a warning via `RCLCPP_WARN` before if the
  incoming config has more panes than cells).
- **`GridConfig parse_yaml(const std::string& text)`** — already the
  `config_model` `from_yaml` entry point, but ensure it throws a typed
  `ConfigParseError` (derived from `std::runtime_error`) with a descriptive
  message for the dialog's `QMessageBox::warning` to surface. The dialog's
  Import path becomes a 5-line try/catch around this function.
- **`std::vector<std::pair<std::string, std::string>> parse_image_topics(
    const std::map<std::string, std::vector<std::string>>& topic_types)`**
  — takes the output shape of `node->get_topic_names_and_types()` and
  returns `(base, transport)` pairs. Pure function (no ROS runtime deps
  beyond STL types, but the input string values are ROS type names that
  are checked against expected types per transport). Each suffix is only
  reported if the advertised type also matches
  (e.g. `/ffmpeg` must advertise `ffmpeg_image_transport_msgs/msg/FFMPEGPacket`)
  — otherwise a non-image publisher with a colliding topic name would
  appear in the dropdown. The dialog snapshots topics once at open, hands
  the map to this function, and populates the combo from the result.

With those extracted, `test_config_model.cpp` covers:

- YAML roundtrip (valid configs, covered already).
- **Parse errors**: malformed YAML, missing `grid`, missing `panes`,
  `grid.rows < 1`, non-numeric thresholds, unknown transport string. Each
  asserts `ConfigParseError` with a substring match on the message.
- **Pane resize**: all four growth/shrink combinations on `rows`/`cols`,
  including the `len(panes) > rows*cols` load-time truncate path.
- **Topic parsing**: ffmpeg sibling without base, compressed sibling with
  base, `sensor_msgs/Image` on the base, unknown type (filtered out), mixed
  same-namespace topics.

The dialog itself — the Qt wiring of spinboxes, combos, buttons — remains
untested at unit level (the repo's Python packages do the same for
annunciator's `config_dialog.py`). Integration happens at manual acceptance.

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
<build_depend>qtbase5-dev</build_depend>
<depend>yaml-cpp</depend>
<depend>ament_index_cpp</depend>

<exec_depend>compressed_image_transport</exec_depend>
<exec_depend>compressed_depth_image_transport</exec_depend>
<exec_depend>theora_image_transport</exec_depend>
<exec_depend>ffmpeg_image_transport</exec_depend>

<test_depend>ament_cmake_gtest</test_depend>
<test_depend>ament_lint_auto</test_depend>
<test_depend>ament_lint_common</test_depend>

<export>
  <build_type>ament_cmake</build_type>
  <rqt_gui plugin="${prefix}/plugin.xml"/>
</export>
```

The four image_transport plugin packages as `exec_depend` are the secondary
goal: `rosdep install` on a fresh salmon pulls every transport the config
dialog advertises (`raw`, `compressed`, `compressedDepth`, `theora`,
`ffmpeg`), so the user can never pick a transport whose plugin isn't
installed. `ffmpeg_image_transport` is the primary deployment target
(H.265 decoder binary available on jazzy as
`ros-jazzy-ffmpeg-image-transport`, verified).

The Qt5 key is `qtbase5-dev` (verified against
`/opt/ros/jazzy/share/rqt_image_view/package.xml`). `rqt_image_view` uses it
as a `build_depend` only; runtime Qt libs come transitively through
`rqt_gui_cpp` / `qt_gui_cpp`, so we mirror that shape rather than adding an
`exec_depend` on Qt directly.

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
# Per-target C/C++ standard enforcement, matching the current ros2 pkg
# create template on Rolling (superseded the older CMAKE_CXX_STANDARD guard).
target_compile_features(${PROJECT_NAME} PUBLIC c_std_99 cxx_std_17)
target_include_directories(${PROJECT_NAME} PUBLIC include)
ament_target_dependencies(${PROJECT_NAME}
  rclcpp rqt_gui_cpp qt_gui_cpp pluginlib image_transport sensor_msgs cv_bridge
  ament_index_cpp)
target_link_libraries(${PROJECT_NAME} Qt5::Widgets yaml-cpp)

ament_export_include_directories(include)
ament_export_libraries(${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME})
ament_export_dependencies(
  rclcpp rqt_gui_cpp qt_gui_cpp pluginlib image_transport sensor_msgs cv_bridge
  ament_index_cpp Qt5 yaml-cpp
)

pluginlib_export_plugin_description_file(rqt_gui plugin.xml)

install(TARGETS ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(FILES plugin.xml DESTINATION share/${PROJECT_NAME})
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
  ament_add_gtest(test_grid_layout test/test_grid_layout.cpp src/grid_layout.cpp)
  target_include_directories(test_grid_layout PRIVATE include)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
```

## Implementation Phases

### Phase 1 — This PR

1. **Scaffolding**: `package.xml`, `CMakeLists.txt`, `plugin.xml` (single file — pluginlib class export + rqt `qtgui` block in one, matching `rqt_image_view`), `README.md`.
2. **`staleness_tracker.{hpp,cpp}`**: pure C++ state machine.
3. **`config_model.{hpp,cpp}`**: `PaneConfig`, `GridConfig` structs + `yaml-cpp` load/save; defaults.
4. **`camera_pane_widget.{hpp,cpp}`**: `QFrame` subclass with child `QLabel` for image + top-strip label; `image_transport::Subscriber` member; border color via palette; uses `cv_bridge::toCvCopy` for non-rgb8 frames, wraps the ROS buffer in a QImage view for rgb8; both paths feed `QPixmap::fromImage` (single-copy to display format).
5. **`camera_grid_widget.{hpp,cpp}`**: container that constructs panes from `GridConfig`; owns a shared `image_transport::ImageTransport`; 1 Hz `QTimer` ticks all panes; overrides `resizeEvent` to apply the aspect-aware layout (see "Grid layout"). **Not** `QGridLayout` — child geometries set manually.
6. **`camera_grid_plugin.{hpp,cpp}`**: `rqt_gui_cpp::Plugin` subclass + `PLUGINLIB_EXPORT_CLASS` macro; `saveSettings`/`restoreSettings`/`triggerConfiguration`.
7. **`config_dialog.{hpp,cpp}`**: grid-dims spinboxes + pane table with dropdown-or-free-form `(base, transport)` fields, populated via `node->get_topic_names_and_types()`.
8. **`config/default_camera_grid.yaml`**: empty 2×2 placeholder.
9. **Tests**: `test_staleness_tracker.cpp` (transitions + boundary ticks), `test_config_model.cpp` (YAML roundtrip + validation), `test_grid_layout.cpp` (pure geometry: `(W, H, R, C, A) → per-cell rects`; asserts zero inner gap, slack-to-outside, mixed-aspect letterboxing), `test_pane_lifecycle.cpp` (≥1000 construct/destruct cycles of `CameraPaneWidget` under `QT_QPA_PLATFORM=offscreen`; asserts no crash and no Qt thread-teardown warnings captured via `qInstallMessageHandler` — stability rule 6 regression test; memory-leak detection deferred to ASan/Valgrind).
10. **Repo root `README.md`**: add `rqt_camera_grid` entry AND the missing `rqt_operator_log` entry.

### Phase 2 — Follow-up issues (file separately)

- Click-to-expand a pane to fill the grid.
- Per-pane FPS limiter (QElapsedTimer-based drop).
- Recording-status overlay coordinated with `rqt_operator_log`.
- Adaptive staleness thresholds (if fixed 2s/5s defaults prove misaligned
  with actual stream cadence — revisit after field use).
- Standalone (non-rqt) entry point if operators want a bare window. Non-trivial in C++ (`QApplication` + `rclcpp::Node` + event-loop bridging); not worth it unless asked for.

## Files to Change

| File | Change |
|------|--------|
| `rqt_camera_grid/**` | New C++ package (all files listed above) |
| `rqt_camera_grid/scripts/demo_transform.py` | Python demo: OpenCV transform node for fanning one webcam into visually distinct streams |
| `rqt_camera_grid/launch/demo_webcam_grid.launch.py` | Python launch: v4l2_camera + five demo_transform.py instances |
| `rqt_camera_grid/config/demo_webcam_grid.yaml` | 2×3 demo grid config matching the launch's six sources |
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

_None — all planning decisions resolved. See `Resolved During Planning`._

## Resolved During Planning

- **Qt5 rosdep key** (was Open Question #1): `qtbase5-dev` as
  `<build_depend>`, verified against
  `/opt/ros/jazzy/share/rqt_image_view/package.xml`. Runtime Qt libs come
  transitively through `rqt_gui_cpp` / `qt_gui_cpp`; no runtime Qt dep
  needed.
- **Subscription QoS** (was review finding 2): `rmw_qos_profile_sensor_data`
  via the QoS-aware `image_transport::ImageTransport::subscribe` overload.
  Image publishers typically use best-effort sensor-data QoS; the default
  (reliable, keep-last-1) would silently fail to match them.
- **Staleness thresholds** (was review finding 1): fixed `warn=2s, error=5s`,
  chosen for operator-station consistency with other staleness-indicating
  tools. Conscious deviation from the issue's suggested rate-based defaults
  — cross-tool consistency over per-stream adaptation for v1.
- **Grid layout** (was review finding 3): aspect-aware, slack-to-outside
  algorithm replacing `QGridLayout`; see "Grid layout" section.
- **Config dialog testability** (was review finding 5): non-Qt logic
  factored into free functions in `config_model`; see "Config dialog: keep
  logic testable" section.
- **`test_pane_lifecycle` contract** (was review finding 4): iteration
  count, image fixture, and bounded-RSS assertions spelled out in Phase 1
  step 9.
- **Default config content**: empty 2×2 placeholder. Platform-agnostic
  principle wins; operators copy one of the platform-shipped configs out
  of the boat's config directory when they want a populated grid.
- **Rate label source**: self-measured from incoming-frame timestamps
  (EWMA over ~1s window). Reflects what operators actually care about —
  is *this display* keeping up — without depending on an external
  statistics topic that may or may not be running.
- **plugin.xml / plugin_description.xml**: single `plugin.xml` file serves
  both pluginlib (`<class ... base_class_type="rqt_gui_cpp::Plugin">`) and
  rqt GUI (`<qtgui>` block). Verified against
  `/opt/ros/jazzy/share/rqt_image_view/plugin.xml` — same structure,
  single file. No separate `plugin_description.xml` needed.
- **Unsupported image encodings**: log-once-warn per pane + dark
  placeholder. See "Image rendering" section.
- **High-DPI**: layout math in logical pixels; Qt handles device-pixel
  scaling. See "Grid layout" item 8.
- **First-frame relayout**: each pane's first `Image` triggers one
  relayout; subsequent frames don't retrigger unless the pane's aspect
  changes the median. See "Grid layout" item 9.

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
