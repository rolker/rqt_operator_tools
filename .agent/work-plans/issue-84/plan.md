# Plan: rqt_boat_state — live control/power/environment gauge panel

## Issue

https://github.com/rolker/rqt_operator_tools/issues/84

## Context

No `rqt_boat_state` package exists yet. The operator currently reads boat state
from scattered raw topics; this panel consolidates heading, speed, steering,
throttle, battery, and environment into a single glanceable QPainter gauge panel.
`rqt_annunciator` provides the staleness/palette conventions to follow.

`marine_interfaces/Helm` carries throttle/rudder commands (−1..1); `mavros_msgs/State`
carries FCU mode; `std_msgs/String` on `piloting_mode` carries helm-manager mode.
`mavros_msgs` is installed system-wide (`ros-jazzy-mavros-msgs`).

## Approach

1. **Package skeleton** — new `rqt_boat_state/` alongside `rqt_annunciator/`;
   `package.xml` (format 3, BSD-3-Clause SPDX), `setup.py`, `setup.cfg`,
   `plugin.xml`, `resource/rqt_boat_state`.

2. **Qt-free data model** (`config_model.py`) — `BoatStateConfig` dataclass:
   topic names, RC channel map (`{throttle: [0,1], steering: [2,3]}`), velocity
   convention (ENU/NED + body/ground), COG min-speed gate (m/s), speed-arc max,
   battery warn/critical V, per-source staleness timeouts. `resolve_authority(fcu_mode, piloting_mode)` → `AuthoritySource` enum
   (AUTONOMY/JOYSTICK/STANDBY/RC/HOLD/DISARMED) + banner label + color token.
   Config serializes to/from YAML for `instance_settings`.

3. **`TrendPlot` widget** (`trend_plot.py`) — QPainter sparkline: ring buffer
   (default 7200 samples at 1/10 s ≈ 2 h), per-bucket min/max, optional
   threshold shading; session-only. Shared by battery-voltage, sound-speed,
   water-temp panels.

4. **Gauge widgets** (`gauges/`) — one file per gauge, all QPainter, no new deps:
   - `heading_gauge.py` — compass rose; COG arrow gated by min-speed; actual yaw (ENU→compass); no commanded overlay in V1
   - `speed_gauge.py` — arc gauge, knots primary/m/s secondary; actual SOG; ghost needle for commanded speed
   - `center_zero_gauge.py` — shared base for Steering + Throttle; actual from `rc/out` µs→−1..1; ghost from active source
   - `battery_gauge.py` — V / % / A readouts + WARN/ERR lamps + `TrendPlot` for voltage

5. **`AuthorityBanner` widget** (`authority_banner.py`) — single colored bar +
   label from `resolve_authority()`; greys commanded markers when `rc/in` is
   stale while mode is RC/MANUAL.

6. **`BoatStateWidget`** (`boat_state_widget.py`) — main container; manages all
   ROS subscriptions (signals pattern from `AnnunciatorWidget`); 1 Hz stale
   timer; distributes data to child widgets.

7. **`BoatStatePlugin`** (`boat_state_plugin.py`) — `rqt_gui_py::Plugin`
   subclass; `save_settings`/`restore_settings` serialize `BoatStateConfig` to
   YAML in `instance_settings`; `trigger_configuration` opens `ConfigDialog`.

8. **`ConfigDialog`** (`config_dialog.py`) — settings dialog covering all
   `BoatStateConfig` fields; documents ENU/NED convention and default ArduRover
   channel map (SERVO1→idx 0, SERVO3→idx 2) with inline help text.

9. **Unit tests** (`test/`) — `test_config_model.py` (authority resolution table,
   channel-map µs→−1..1 clamp, config round-trip YAML), `test_trend_plot.py`
   (ring-buffer wraparound, min/max per bucket), `test_staleness.py` (stale gate,
   RC-in freshness gating logic in isolation).

## Files to Change

| File | Change |
|------|--------|
| `rqt_boat_state/package.xml` | New — format 3, depends listed below |
| `rqt_boat_state/setup.py` | New — ament_python, plugin.xml in data_files |
| `rqt_boat_state/setup.cfg` | New |
| `rqt_boat_state/plugin.xml` | New — "Robot Tools" group |
| `rqt_boat_state/resource/rqt_boat_state` | New — ament marker |
| `rqt_boat_state/rqt_boat_state/__init__.py` | New |
| `rqt_boat_state/rqt_boat_state/config_model.py` | New |
| `rqt_boat_state/rqt_boat_state/trend_plot.py` | New |
| `rqt_boat_state/rqt_boat_state/gauges/` (5 files) | New |
| `rqt_boat_state/rqt_boat_state/authority_banner.py` | New |
| `rqt_boat_state/rqt_boat_state/boat_state_widget.py` | New |
| `rqt_boat_state/rqt_boat_state/boat_state_plugin.py` | New |
| `rqt_boat_state/rqt_boat_state/config_dialog.py` | New |
| `rqt_boat_state/test/` (3 files) | New — Qt-free unit tests |

`package.xml` `<exec_depend>` list: `python_qt_binding`, `rclpy`, `rqt_gui`,
`rqt_gui_py`, `rqt_annunciator` (for `IndicatorLevel` import), `nav_msgs`,
`geometry_msgs`, `mavros_msgs`, `sensor_msgs`, `marine_interfaces`, `std_msgs`,
`ament_index_python`, `python3-yaml`.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Authority banner makes active driver visible at all times; staleness greyout shows data age; all topics configurable |
| Only what's needed | No new Qt libs; reuse `IndicatorLevel` from `rqt_annunciator`; no premature V2 features |
| Test what breaks | Tests cover authority resolution (safety-critical logic), staleness gating, channel-map math — not Qt framework glue |
| A change includes its consequences | `package.xml` exec_depends and plugin.xml ship in the same PR; ENU/NED convention documented in config dialog |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 (ROS 2 conventions) | Yes — new package | `package.xml` format 3, SPDX, license headers, ament_python, `<exec_depend>` tags |
| ADR-0003 (project-agnostic workspace) | No — changes land in project repo `rqt_operator_tools` | N/A |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `marine_interfaces/Helm` msg fields | `config_model.py` field paths | No — follow-up if msg changes |
| `rqt_annunciator` `IndicatorLevel` | `rqt_boat_state` import + palette | No — tracked by the dependency |

## Open Questions

- [ ] Should `rqt_boat_state` import `IndicatorLevel` from `rqt_annunciator`, or define its own color palette independently? (Dependency vs. coupling trade-off.)
- [ ] `mavros/setpoint_velocity/cmd_vel` type: `geometry_msgs/Twist` or `geometry_msgs/TwistStamped`? Verify against live mavros2 setup before wiring the speed ghost needle.
- [ ] Confirm ENU/NED default for this vessel's odom topic — setting defaults to ENU (ROS standard) but bizzy may differ.

## Estimated Scope

Single PR — all files are new (no modifications to existing packages).
