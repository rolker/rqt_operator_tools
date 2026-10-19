# Plan: rqt plugin: a boat's current ping seen from the front

## Issue

https://github.com/rolker/rqt_operator_tools/issues/113

## Context

Part of the Ocean Discovery Day display (rolker/unh_marine_simulation#87). Every boat
publishes `sensors/mbes/detections`, `marine_acoustic_msgs/SonarDetections` (simulator
`mbes_sim` and real sonars alike). The wanted view is the latest ping seen from the front
of the boat: swath lines out to the outer beams and the OK detections as the bathymetry
profile under the boat. It is an operator tool that outlives the demo, so it is built to the
standard of `rqt_coverage` (issue #111): model apart from the widget, settings dialog,
YAML settings in the rqt perspective, standalone entry point, tests with fake node/clock.

## Approach

New ament_python package `rqt_ping_profile`, laid out like `rqt_coverage`. Plugin class
`rqt_ping_profile/PingProfilePlugin`, label "Ping Profile", group "Robot Tools".

1. `profile_model.py` (Qt-free, ROS-free; duck-typed message):
   - `Ping.from_msg`: validates that `two_way_travel_times`, `rx_angles` and `flags` have the
     same non-zero length and `sound_speed` is positive and finite, else raises
     `BadPing(reason)`. Per beam: range = twtt * sound_speed / 2, across = range * sin(rx),
     depth = range * cos(rx). Across is positive to starboard (the message convention:
     `rx_angles` + to starboard, - to port; mbes_sim agrees). Beams with a non-finite or
     negative travel time, a non-finite angle or an angle of 90 degrees or more are dropped and counted; a ping with no usable beam is a `BadPing`. Beams are sorted by
     across-track so the polyline is ordered whatever the sonar's beam order. `tx_angles`
     (along-track steering) is ignored: depth below the transducer is range * cos(rx).
   - Swath edges = the outermost OK beams. Profile = OK beams. Flagged beams are kept
     (setting `show_flagged` draws them as dim dots; default off).
   - `PingStore`: latest ping, last-N history (setting), receive time (injected clock),
     staleness against a timeout, bad-message reason, counts.
   - `DepthScale`: fixed (min, max) or auto. Auto tracks the needed depth extent and the
     needed half swath width, with 10% headroom; grows at once (the whole swath is always
     visible), shrinks only when the need is below 70% of the current extent, so it does not
     jump every ping. `metres_per_pixel(...)` makes depth and across-track share one scale
     (angles read true); in auto mode it is the larger of the depth and the width demand.
   - `Viewport`: metres to pixels. Port is on the viewer's right (see below).
   - `nice_step`/`ticks`: 1-2-5 ticks in metres.
   - `ProfileConfig`: topic (default `sensors/mbes/detections`), title, depth_mode,
     depth_min, depth_max, stale_timeout (5 s), history (0), show_flagged; `normalized`,
     `to_yaml`, `from_yaml` lenient like `CoverageConfig`.
2. `ping_profile_widget.py`: a QWidget that paints the view; ROS callback only emits a
   Qt signal that wakes the Qt thread, the message itself going through a one-slot
   newest-wins mailbox stamped with its receipt time (generation-checked), best_effort keep-last-1 subscription, coalesced
   redraw, 0.5 s timer for staleness, painting guarded so an error is logged once and never
   raised. Bad messages: one log line per 10 s whatever the reason, draw nothing.
3. `config_dialog.py`, `ping_profile_plugin.py`, `ping_profile_standalone.py`
   (`ros2 run rqt_ping_profile ping_profile`), `plugin.xml`, `package.xml`, `setup.py`.
4. Tests: geometry (angles, sign, scaling), scale (auto, hysteresis, fixed clipping), store
   (latest, history, staleness, bad message), config round trip, offscreen widget smoke
   with a stub node and PNG grab, a real-rclpy delivery test.
5. Docs: package README (data, convention, settings table), root README row. The repo has no
   `.agents/README.md` (its AGENTS.md says a deep guide is its own issue), so there is no
   inventory to update; the gap is noted, not filled here.

## Convention: port and starboard

Seen from the front (looking at the bow from ahead) the boat's port side is on the
viewer's RIGHT, starboard on the LEFT. Across-track is positive to starboard in the data, so
screen x = centre - across / metres_per_pixel. The axis ends are labelled PORT and STBD so
the picture never needs the convention explained.

## Principles

| Principle | Consequence |
|---|---|
| Robustness on open water | A bad or empty message draws nothing, logs once, never raises |
| Settings persist via the perspective | `config_yaml` in instance settings, like rqt_coverage |
| Qt/ROS threading | Callback emits a queued signal; widgets touched on the Qt thread only |
| Sensor QoS best_effort | Subscription is best_effort, keep last 1 |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0008 ROS 2 conventions | Yes | ament_python, BSD-3-Clause, package.xml format 3, plugin.xml |
| 0009 Python package policy | Yes | Only apt/rosdep deps (`python3-yaml`) |
| 0013 progress.md vocabulary | Yes | Entries appended per phase |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| New package in repo | Root README package list | Yes |
| New ROS package | ui_ws rebuild on the display machine after merge | Note in PR |
| Settings (new plugin) | Package README settings table | Yes |

## Documentation & Instruction Impact

- Root `README.md` gets the new plugin; the package README is written from the code.
- No `.agents/README.md` exists in this repo; candidate for its own issue.

## Estimated Scope

Single PR: about 5 modules, 5 test files.

## Open Questions

None; the display conventions were fixed by the owner (settings, fixed 0-30 m default,
port on the viewer's right, last-N history default 0).

## Implementation Notes

- Review-code round 1 (two Claude lenses + codex; gemini timed out) changes: one-slot
  mailbox with receipt time taken on the ROS thread; an all-invalid ping is rejected instead
  of erasing the last good one; the title elides so the status stays on screen; log limit is
  global (one per 10 s); auto-scale resets on a topic change and never zooms in past 1 m;
  beams at 90 degrees or more are dropped; history dims when stale; paint save/restore is
  exception-safe and a paint error is shown; bool settings rejected; `_closed` guard.
- Not done: per-side fallback of an inverted fixed depth range (both sides reset to the
  default, documented); a sound-speed fallback for sonars that leave it 0 (ping rejected,
  reason shown).
