# rqt_ping_profile

A boat's current multibeam ping seen from the front, for the operator station and the
Ocean Discovery Day display (rolker/unh_marine_simulation#87). Lines from the transducer
out to the outermost OK beams show the swath width; the OK detections joined as a line
draw the profile of the bottom under the boat at that moment. Four instances side by
side, one per boat, show why coverage builds fast in deep water and slowly in the shallows.

One plugin instance shows one topic, so an rqt perspective holds one instance per boat.

## Input

| Topic (default) | Type | QoS |
|---|---|---|
| `sensors/mbes/detections` | `marine_acoustic_msgs/SonarDetections` | best_effort, volatile, keep last 1 (matches reliable and best_effort publishers; only the newest ping matters) |

The topic is relative, so it resolves in the rqt node's namespace; a full name such as
`/boat2/sensors/mbes/detections` works too. The fields used:

- `two_way_travel_times[i]` (s), `rx_angles[i]` (rad) and `flags[i].flag` (0 = OK) for each
  beam, and `ping_info.sound_speed` (m/s). `tx_angles`, `intensities` and the rest are ignored.
- `header.stamp` is kept but not used for staleness (see below).

For beam `i`:

```
range  = two_way_travel_times[i] * sound_speed / 2
across = range * sin(rx_angles[i])     # metres, positive to starboard
depth  = range * cos(rx_angles[i])     # metres below the transducer
```

Depth is below the transducer. Tide and draft offsets are not applied, and along-track
steering (`tx_angles`) is ignored: a ping is drawn as a cross-section.

## Conventions

**Angle sign.** `SonarDetections.rx_angles` is documented as "+ to starboard, - to port for
a downlooking sonar". `mbes_sim` agrees (its port-most beam has the negative angle), so a
positive angle puts the beam on the boat's starboard side.

**Port and starboard on screen.** The view looks at the boat from the front, looking at the
bow from ahead. From there the boat's **port side is on the viewer's right** and starboard
on the viewer's left, so `screen_x = centre - across / metres_per_pixel`. The axis ends are
labelled `STBD` (left) and `PORT` (right) so the picture does not need the convention
explained.

**Beam order.** Beams are sorted by across-track before the profile is drawn, so the line
is correct whatever order the sonar reports them in.

## What is drawn

- The transducer (amber marker) at the top centre, at depth 0.
- A swath line from the transducer to the port-most and starboard-most OK beams.
- The OK detections joined as the profile, with a dot per beam.
- Beams flagged not OK are left out of the profile and the swath. With `show_flagged` they
  are drawn as dim red dots.
- Grid and labels in metres: depth down the left edge, across-track distance from the
  centre line along the bottom (the same step on both axes).
- The title (the `title` setting, else the topic) top left, and a status line beside it
  that is empty while all is well.
- With `history` above 0, the last N pings fade in behind the current one (oldest
  faintest), as profile lines only.

Both axes use the **same metres per pixel**, so a beam at 30 degrees looks like 30 degrees.
The widget's shape decides how much across-track is visible.

## Depth axis

- **fixed** (default): `depth_min` to `depth_max` (0 to 30 m) fill the height. Instances
  with the same range are directly comparable. Anything deeper is cut off at the bottom of
  the plot; across-track beyond the width is cut off at the sides.
- **auto**: the scale follows the ping, from the transducer (0 m) to the deepest OK
  detection plus 10 % headroom. It also widens, if needed, so the **whole swath is always
  visible** (the larger of what depth and width require). To keep it from jumping with
  every ping, it grows at once but shrinks only when the need falls below 70 % of the
  current extent. History pings do not take part in the scaling and may be cut off.

## Status line

| Text | Meaning |
|---|---|
| `waiting for the first ping on <topic>` | Subscribed, nothing received yet. |
| `NO PING FOR 7 s` | No usable ping for more than `stale_timeout`; the picture dims. Measured on this machine's monotonic clock from the last good ping, not from `header.stamp`, which is on the sonar's clock (sim time in the demo). |
| `unusable ping: <why>` / `unusable ping ignored: <why>` | The last message could not be drawn: no beams, arrays of different lengths, or no usable sound speed. Nothing is drawn from it; the last good ping stays on screen, and goes stale in the normal way. Logged once per distinct reason, then at most every 10 s. |
| `no OK detections in the last ping` | Every beam was flagged. |
| `N invalid beams dropped` | Beams with a non-finite or negative travel time or a non-finite angle were left out. |
| `cannot subscribe to <topic>: ...` | The topic name is not valid; fix it in the settings. |

## Settings

Saved with the rqt perspective (`config_yaml` in the plugin instance settings). The wrench
opens the dialog.

| Setting | Default | Meaning |
|---|---|---|
| `topic` | `sensors/mbes/detections` | `SonarDetections` topic (resolved in the rqt node's namespace). |
| `title` | empty | Name shown in the corner (a boat name); empty shows the topic. |
| `depth_mode` | `fixed` | `fixed` or `auto`. |
| `depth_min`, `depth_max` | 0 m, 30 m | Fixed range, metres below the transducer (-1000 to 20000; an empty or inverted range falls back to the default). Ignored in auto. |
| `stale_timeout` | 5 s | Seconds without a usable ping before the staleness mark; 0 disables. |
| `history` | 0 | Earlier pings faded behind the current one (0 to 50). |
| `show_flagged` | off | Draw not-OK beams as dim dots. |

Unusable values in a saved perspective fall back to the default instead of failing.

## Running

In rqt: *Plugins > Robot Tools > Ping Profile*. As its own window (wall display):

```bash
ros2 run rqt_ping_profile ping_profile --topic /boat2/sensors/mbes/detections --title "Boat 2" --depth-mode auto
# --config <settings.yaml>   the same keys as the settings table
# --always-on-top
```

The standalone spins its node on a background executor thread, as `rqt_gui_py` does; the
subscription callback only emits a queued Qt signal, so every widget access stays on the Qt
thread.

## Tests

`./ui_ws/test.sh rqt_ping_profile` from a layer worktree. The model tests (geometry, scale,
store, settings) need no display or ROS graph; the widget smoke test runs with
`QT_QPA_PLATFORM=offscreen` (set by the test), and the integration test runs real rclpy on
a private DDS domain.
