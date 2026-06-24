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

### Resolved source decisions (verified against the stack, 2026-06-23)

- **Odom source = `mru_transform`, not mavros.** On BizzyBoat `mru_transform_node`
  publishes `nav_msgs/Odometry` (fusing the SBG MRU + GPS) and owns the
  `map`→`odom`→`base_link` TF; the nav stack consumes it remapped to **`/bizzy/odom`**
  (`detections_to_pointcloud` reads it for speed-over-ground). `mavros/local_position/odom`
  is a secondary bridged source. The panel defaults its odom topic to **`/bizzy/odom`**
  (configurable). `mru_transform` follows REP-103, so its odom is **ENU** — the
  ENU default is correct; the NED bits (`imu_link_ned`, `setpoint_velocity.mav_frame: BODY_NED`)
  are internal to the SBG driver / mavros setpoint path, not the nav odom.
- **Commanded-speed topic type = `geometry_msgs/TwistStamped`.** `helm_manager`
  publishes `out/cmd_vel` as `TwistStamped` and the whole piloting chain +
  `mavros/setpoint_velocity/cmd_vel` use `TwistStamped`. (Open question resolved.)
- **`IndicatorLevel` is duplicated, not imported.** It is a trivial 4-value enum
  (`OK/WARN/ERROR/STALE`); duplicating the few lines avoids a runtime coupling
  `rqt_boat_state → rqt_annunciator`. Flag a shared-module consolidation as a V2
  candidate. (Open question resolved.)
- **Environment sources** (must-fix expansion below): sound speed =
  `marine_interfaces/SoundSpeed` (`sound_speed/sound_speed`, `float64 sound_speed` m/s,
  NaN if unavailable); water temperature = `sensor_msgs/Temperature`
  (`sound_speed/temperature`, `float64 temperature` °C). Both configurable, both
  stale-gated like every other source.

## Approach

1. **Package skeleton** — new `rqt_boat_state/` alongside `rqt_annunciator/`;
   `package.xml` (format 3), `setup.py`, `setup.cfg`, `plugin.xml`,
   `resource/rqt_boat_state`. **License convention (ADR-0008 §5):** the repo
   declares the license in `package.xml` only — **zero** `.py` files carry SPDX
   headers, so do **not** add per-file SPDX headers. Mirror `rqt_annunciator`
   exactly: same `<license>` value, same (header-less) file style, and the same
   `ament_copyright` / `python3-pytest` `<test_depend>`s so the copyright test
   passes identically.

2. **Qt-free data model** (`config_model.py`) — defines a local 4-value
   `IndicatorLevel` enum (`OK/WARN/ERROR/STALE`, duplicated from `rqt_annunciator`
   to avoid cross-plugin coupling; V2 consolidation candidate) and a
   `BoatStateConfig` dataclass: topic names (odom default `/bizzy/odom`), RC
   channel map (`{throttle: [0,1], steering: [2,3]}`), velocity convention
   (ENU/NED + body/ground, default ENU), COG min-speed gate (m/s), speed-arc max,
   battery warn/critical V, per-source staleness timeouts.
   `resolve_authority(fcu_mode, piloting_mode)` → `AuthoritySource` enum
   (AUTONOMY/JOYSTICK/STANDBY/RC/HOLD/DISARMED) + banner label + color token.
   Config serializes to/from YAML for `instance_settings`.

3. **`TrendPlot` widget** (`trend_plot.py`) — QPainter sparkline: ring buffer
   (default **720 samples at 1 per 10 s = 2 h**), per-bucket min/max, optional
   threshold shading; session-only. Shared by the battery-voltage, sound-speed,
   and water-temp panels.

4. **Gauge widgets** (`gauges/`, 4 files) — one file per gauge, all QPainter, no new deps:
   - `heading_gauge.py` — compass rose; COG arrow gated by min-speed; actual yaw (ENU→compass, `heading=(90−yaw)mod360`); no commanded overlay in V1
   - `speed_gauge.py` — arc gauge, knots primary/m/s secondary; actual SOG = ‖odom twist‖; ghost needle for commanded speed (from `cmd_vel` TwistStamped)
   - `center_zero_gauge.py` — shared base reused by **both** Steering and Throttle; actual from `rc/out` µs→−1..1; ghost from active source
   - `battery_gauge.py` — V / % / A readouts + WARN/ERR lamps + `TrendPlot` for voltage

5. **Environment panel** (`environment_panel.py`) — non-gauge readout row for the
   `control/power/**environment**` scope: a `sound_speed` numeric readout (m/s,
   from `marine_interfaces/SoundSpeed` on `sound_speed/sound_speed`; show "—" when
   `sound_speed` is NaN) + its `TrendPlot`, and a `water_temp` readout (°C, from
   `sensor_msgs/Temperature` on `sound_speed/temperature`) + its `TrendPlot`. No
   alarm thresholds (auto-scaling trends); stale-gated like all sources.

6. **`AuthorityBanner` widget** (`authority_banner.py`) — single colored bar +
   label from `resolve_authority()`; greys commanded markers when `rc/in` is
   stale while mode is RC/MANUAL.

7. **`BoatStateWidget`** (`boat_state_widget.py`) — main container; manages all
   ROS subscriptions including the two environment topics (signals pattern from
   `AnnunciatorWidget`); 1 Hz stale timer; distributes data to the gauges, the
   authority banner, and the environment panel.

8. **`BoatStatePlugin`** (`boat_state_plugin.py`) — `rqt_gui_py::Plugin`
   subclass; `save_settings`/`restore_settings` serialize `BoatStateConfig` to
   YAML in `instance_settings`; `trigger_configuration` opens `ConfigDialog`.

9. **`ConfigDialog`** (`config_dialog.py`) — settings dialog covering all
   `BoatStateConfig` fields (incl. the environment topics); documents ENU/NED
   convention and default ArduRover channel map (SERVO1→idx 0, SERVO3→idx 2) with
   inline help text.

10. **Unit tests** (`test/`) — `test_config_model.py` (authority resolution table,
    channel-map µs→−1..1 clamp, config round-trip YAML), `test_trend_plot.py`
    (ring-buffer wraparound, min/max per bucket, 2 h window sizing), `test_staleness.py`
    (stale gate, RC-in freshness gating logic, SoundSpeed-NaN handling in isolation).

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
| `rqt_boat_state/rqt_boat_state/trend_buffer.py` | New — Qt-free ring buffer (added: keeps test_trend_plot Qt-free) |
| `rqt_boat_state/rqt_boat_state/trend_plot.py` | New — QPainter sparkline over trend_buffer |
| `rqt_boat_state/rqt_boat_state/gauges/` (5 files: `__init__`, `heading_gauge`, `speed_gauge`, `center_zero_gauge`, `battery_gauge`) | New |
| `rqt_boat_state/rqt_boat_state/environment_panel.py` | New — sound-speed + water-temp readouts + trends |
| `rqt_boat_state/rqt_boat_state/authority_banner.py` | New |
| `rqt_boat_state/rqt_boat_state/boat_state_widget.py` | New |
| `rqt_boat_state/rqt_boat_state/boat_state_plugin.py` | New |
| `rqt_boat_state/rqt_boat_state/boat_state_standalone.py` | New — console entry point (added: mirrors rqt_annunciator) |
| `rqt_boat_state/rqt_boat_state/config_dialog.py` | New |
| `rqt_boat_state/test/` (3 files) | New — Qt-free unit tests (68 tests) |

`package.xml` `<exec_depend>` list: `python_qt_binding`, `rclpy`, `rqt_gui`,
`rqt_gui_py`, `nav_msgs`, `geometry_msgs`, `mavros_msgs`, `sensor_msgs`,
`marine_interfaces`, `std_msgs`, `ament_index_python`, `python3-yaml`.
`<test_depend>`: `ament_copyright`, `python3-pytest` (mirroring `rqt_annunciator`).
No `rqt_annunciator` dep — `IndicatorLevel` is duplicated, not imported.

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Authority banner makes active driver visible at all times; staleness greyout shows data age; all topics configurable |
| Only what's needed | No new Qt libs; no new package deps (duplicate the 4-value `IndicatorLevel` rather than couple to `rqt_annunciator`); no premature V2 features |
| Test what breaks | Tests cover authority resolution (safety-critical logic), staleness gating, channel-map math — not Qt framework glue |
| A change includes its consequences | `package.xml` exec_depends and plugin.xml ship in the same PR; ENU/NED convention documented in config dialog |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 (ROS 2 conventions) | Yes — new package | `package.xml` format 3, license declared in `package.xml` (no per-file SPDX headers — repo has none; mirror `rqt_annunciator`), ament_python, `<exec_depend>`/`<test_depend>` tags |
| ADR-0003 (project-agnostic workspace) | No — changes land in project repo `rqt_operator_tools` | N/A |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `marine_interfaces/Helm` msg fields | `config_model.py` field paths | No — follow-up if msg changes |
| `marine_interfaces/SoundSpeed` msg fields | `environment_panel.py` field paths | No — follow-up if msg changes |

## Open Questions (resolved at plan-review checkpoint, 2026-06-23)

- [x] `IndicatorLevel` import vs. own palette → **duplicate** the 4-value enum (no `rqt_annunciator` dep); V2 consolidation candidate.
- [x] `cmd_vel` type → **`geometry_msgs/TwistStamped`** (verified: `helm_manager out/cmd_vel` + `mavros/setpoint_velocity/cmd_vel`).
- [x] odom source/frame → **`/bizzy/odom`** from `mru_transform` (ENU, REP-103); topic configurable, default `/bizzy/odom`.

## Estimated Scope

Single PR — all files are new (no modifications to existing packages). ~21 new
files; commit incrementally (skeleton → Qt-free core + tests → gauges →
authority banner → environment panel → plugin/dialog) to keep the PR reviewable.
