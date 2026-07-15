# Agent Guide: rqt_operator_tools

> ROS 2 rqt plugins for the operator station of autonomous survey boats
> (BizzyBoat/IzzyBoat): monitoring and device control over lossy links.

## Workflow

GitHub-origin repo: `rolker/rqt_operator_tools` (local checkouts may also carry a
`gitcloud` field mirror — on a dev machine, GitHub `origin` governs). Default
branch is **`jazzy`**. All changes go through worktree + PR per the workspace
rules; the `no-commit-to-branch` pre-commit hook blocks direct commits to `jazzy`.

**When checked out inside a
[ROS 2 Agent Workspace](https://github.com/rolker/ros2_agent_workspace)** (layer:
`ui_ws`), workflow rules (worktree vs. field mode, branch naming) come from the
workspace `AGENTS.md`. Detect the active mode from the workspace root:

```bash
.agent/scripts/field_mode.sh --describe layers/main/ui_ws/src/rqt_operator_tools
```

**Standalone use**: only this repo's own conventions apply. Also read the root
`AGENTS.md` here — it carries the review context (robustness bar, Qt/ROS
threading, QoS, perspective persistence) that GitHub Copilot review consumes.

## Package Inventory

| Package | Lang / build | What it is | Key interfaces (verified in source) |
|---|---|---|---|
| `rqt_annunciator` | Python / ament_python | Dark-until-problem annunciator panel | YAML-config indicators: `/diagnostics` (`diagnostic_msgs`) and/or arbitrary topics with threshold expressions (`config/default_annunciator.yaml`) |
| `rqt_boat_state` | Python / ament_python | Control/power/environment gauge panel | Config-driven subs; defaults target the BizzyBoat stack: `/bizzy/odom`, `cmd_vel`, `helm`, `mavros/state`, `marine/heartbeat`, `mavros/rc/{out,in}`, `mavros/battery`, `sound_speed/*` (`config_model.py`) |
| `rqt_operator_log` | Python / ament_python | Operator logbook with rosbag2 (mcap) recording + recovery | Pub+sub `std_msgs/String` on a namespaced log topic (`bag_manager.make_log_topic`); bags under `~/operator_logs/<date>/`, `storage_id='mcap'` |
| `rqt_camera_grid` | C++ / ament_cmake | Multi-stream `image_transport` grid with per-pane staleness border | One `image_transport::Subscriber` per pane from `{base, transport}` config, sensor-data QoS; per-pane `warn_s`/`error_s` staleness |
| `rqt_sonar_waterfall` | C++ / ament_cmake | Scrolling sidescan backscatter waterfall (GPU) + target marking | Subs: port/stbd `marine_acoustic_msgs/RawSonarImage` (SensorDataQoS), `sensor_msgs/Range` nadir depth, `RadarControlSet`; pubs: `RadarControlValue` on derived `.../change_state`, `marine_interfaces/Contact` on `sonar_waterfall/contacts` (default, reliable KeepLast(100)) |
| `rqt_marine_sonar` | C++ / ament_cmake | Water-column echogram ("curtain", depth vs. ping, GPU) | Sub: selected `RawSonarImage` topic, SensorDataQoS; reuses `rqt_sonar_waterfall`'s exported decode/colormap stack |
| `marine_control_widgets` | C++ / ament_cmake | ROS-free Qt widget library rendering any `marine_control_interfaces/ControlSet` | Library only — no node, no topics |
| `marine_control_bridge_client` | C++ / ament_cmake | No-Qt helper for udp_bridge dynamic device wiring | Sub: `<bridge_node>/bridge_info` (QoS depth 1, transient_local); service clients: `<bridge_node>/{remote_subscribe,remote_advertise,remove_subscribe,remove_advertise}` |
| `rqt_marine_control` | C++ / ament_cmake | Generic device-control panel over the bridge | Per tab: sub `ControlSet` on chosen state topic (reliable, depth 10, ADR-0003 D5), pub `ControlValue` on derived `.../change` topic |

Python packages (`rqt_annunciator`, `rqt_boat_state`, `rqt_operator_log`) are
`rqt_gui_py::Plugin` subclasses; C++ plugin packages are `rqt_gui_cpp::Plugin`
subclasses. `marine_control_widgets` and `marine_control_bridge_client` are
plain shared libraries (no plugin.xml).

## Architecture Overview

**Operator-station context.** Every plugin runs at the shore/operator station of
a live autonomous boat; topics arrive over lossy links via `udp_bridge`. Streams
can stall, degrade, or vanish mid-survey — staleness indication and
degraded-stream tolerance are features, not polish. `rqt_camera_grid` has an
explicitly high robustness bar (a crash costs on-water time).

**marine_control device-control triad** (unh_marine_autonomy ADR-0003):

- `marine_control_widgets` — pure Qt rendering of a `ControlSet` (one input
  widget per `ControlItem` type). ROS-free, reusable outside rqt.
- `marine_control_bridge_client` — pure rclcpp (no Qt): discovers controllable
  devices from `udp_bridge` `bridge_info` and connects/disconnects their
  state/change topics on request via the local bridge's remote_subscribe /
  advertise services.
- `rqt_marine_control` — the rqt plugin gluing both together: pick a `ControlSet`
  state topic, render it, publish `ControlValue` changes.

**Shared sonar rendering stack.** `rqt_sonar_waterfall` exports its library
(`ament_export_targets`/`ament_export_libraries` in its CMakeLists) — the sample
decode, `marine_colormap` palettes, and `GpuColorMap` GL renderer — and
`rqt_marine_sonar` depends on it (`<depend>rqt_sonar_waterfall</depend>`). Both
render via `QOpenGLWidget` + `QOpenGLFunctions_3_3_Core` (OpenGL 3.3 core).

**Plugin registration.** Each plugin package exports
`<rqt_gui plugin="${prefix}/plugin.xml"/>` in `package.xml`; `plugin.xml` maps
the class name to the library/module and puts it in the rqt menu group
**Robot Tools**. Python packages install the ament resource marker via
`setup.py` `data_files`; C++ packages via `install(FILES resource/... plugin.xml ...)`.

## Key Files to Read First

1. `AGENTS.md` — review context (threading, QoS, perspective conventions)
2. `rqt_sonar_waterfall/README.md` and `rqt_camera_grid/README.md` — the two
   packages with deep design docs (config schema, QoS rationale, layout design)
3. `rqt_marine_control/src/marine_control_plugin.cpp` — the canonical
   ROS-callback→GUI-thread marshalling pattern (`QMetaObject::invokeMethod` +
   `Qt::QueuedConnection`, lifetime comments)
4. `marine_control_bridge_client/src/bridge_control_client.cpp` — bridge wiring
5. `rqt_boat_state/rqt_boat_state/config_model.py` — config/defaults pattern for
   the Python plugins (dataclass + YAML + perspective serialization)

## Build & Test

```bash
# From the layer workspace (layers/main/ui_ws/) or a layer worktree
colcon build --symlink-install --packages-select <package>
# Worktrees: prefer ./ui_ws/build.sh [pkg] / ./ui_ws/test.sh [pkg]
source ../../../.agent/scripts/setup.bash && colcon test --packages-select <package> && colcon test-result --verbose
```

- Lint runs inside `colcon test` (ament_lint_auto for CMake packages,
  flake8/pep257 hooks for Python) — there is no separate lint step.
- GUI-widget tests (e.g. `rqt_sonar_waterfall`'s offscreen smoke test) need
  `QT_QPA_PLATFORM=offscreen` on headless machines.
- Every package has unit tests under `<pkg>/test/` (gtest for C++, pytest for
  Python).

### Running a single plugin

```bash
rqt --standalone rqt_camera_grid                      # by package/plugin name
ros2 run rqt_sonar_waterfall rqt_sonar_waterfall      # installed standalone wrappers
ros2 run rqt_marine_sonar rqt_marine_sonar
ros2 run rqt_marine_control rqt_marine_control
ros2 run rqt_operator_log operator_log                # Python console_scripts
ros2 run rqt_annunciator annunciator
ros2 run rqt_boat_state boat_state
rqt --force-discover                                  # if a new plugin isn't listed
```

Hardware-free exercisers: `rqt_camera_grid` `demo_webcam_grid.launch.py`
(webcam → 6 transformed streams), `rqt_sonar_waterfall`
`scripts/synthetic_sidescan.py`, `rqt_annunciator`
`launch/test_annunciator.launch.py` + `diagnostic_test_publisher`.

## CI

`.github/workflows/ci.yml` — build+test gate on push/PR to `jazzy`, in a
`ros:jazzy-ros-core` container with `QT_QPA_PLATFORM=offscreen` job-wide.
Several dependencies are source siblings, not rosdep binaries, so CI clones
them (jazzy branch, depth 1) and prunes to the needed packages:

| Cloned repo | Kept package(s) | Needed by |
|---|---|---|
| `rolker/unh_marine_radar` | `marine_radar_control_msgs` | `rqt_sonar_waterfall` control panel |
| `rolker/marine_colormap` | whole repo | waterfall/echogram colormaps |
| `rolker/marine_control` | whole repo (`marine_control_interfaces`) | control triad |
| `rolker/udp_bridge` | `udp_bridge_interfaces` | `marine_control_bridge_client` |
| `rolker/unh_marine_autonomy` | `marine_interfaces`, `marine_contacts` | waterfall target marking |

`colcon test` runs only this repo's packages (list derived from the repo path,
so new packages are covered automatically). The workspace `make build` is the
full integration gate; this per-repo CI is the lighter check (workspace
ADR-0004/0005).

Pre-commit (`.pre-commit-config.yaml`): whitespace/YAML/XML hygiene, cmake-lint,
yamllint, `no-commit-to-branch` on `jazzy`. black/flake8 are deliberately off
pending the license-header cleanup.

## Common Pitfalls

- **Qt threading**: ROS callbacks run off the GUI thread. Never touch widgets
  from a callback — marshal with `QMetaObject::invokeMethod(...,
  Qt::QueuedConnection)` (C++, see `rqt_marine_control`, waterfall
  `on_control_set`) or enqueue + drain from a GUI-thread timer
  (`rqt_marine_sonar::dataCallback`). Mind object lifetime: `QPointer` guards,
  subscription-id stamping (`SonarWaterfallPlugin::subscribe`).
- **QoS mismatches silently receive nothing**: sensor streams (sonar, camera,
  nadir depth) publish best-effort — subscribe with `rclcpp::SensorDataQoS()`.
  Conversely `ControlSet` state is reliable (ADR-0003 D5) and `bridge_info` is
  transient_local; match each producer.
- **Two different change-topic derivations**: `rqt_sonar_waterfall` (radar
  convention) rewrites a final `/state` segment to `/change_state`;
  `rqt_marine_control` (ADR-0003) rewrites trailing `state` to `change`.
  Don't unify or cross-apply them.
- **Plugin resource paths**: a plugin only appears in rqt if the `plugin.xml`
  export, the ament resource-index marker, and (C++) the installed library all
  line up. After adding one, run `rqt --force-discover` once.
- **GPU/GL**: waterfall and echogram require an OpenGL 3.3 core context; GL
  resources must be created/destroyed with the owning `QOpenGLWidget` context
  current (`GpuColorMap` owns no FBO by design). Headless runs need
  `QT_QPA_PLATFORM=offscreen`.
- **UI state persists via rqt perspectives** (`saveSettings`/`restoreSettings`);
  new user-facing settings should follow the perspective-persisted toolbar
  convention, not ROS parameters.
- **rqt_operator_log durability**: entries go durably to a sidecar first; the
  mcap segment stays 0 bytes until finalized — don't "fix" that by removing the
  sidecar. `rosbag2_storage_mcap` is a required exec dep or recording fails
  with "No storage could be initialized".
- **Stale root README**: it currently lists only 5 of the 9 packages — trust
  `package.xml` files over the README until it is updated.
