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
| `/marine/platforms` | `marine_interfaces/PlatformList` | reliable, volatile, keep last 5 (as `platform_send.py` publishes it and CAMP subscribes) |

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

- The transducer (amber marker) at the top centre, at depth 0 (when depth 0 is inside the
  fixed range; with `depth_min` above 0 the marker is not drawn and the swath lines enter
  from the top edge).
- A swath line from the transducer to the port-most and starboard-most OK beams.
- The OK detections joined as the profile, with a dot per beam.
- Beams flagged not OK are left out of the profile and the swath. With `show_flagged` they
  are drawn as dim red dots.
- Grid and labels in metres: depth down the left edge, across-track distance from the
  centre line along the bottom (the same step on both axes).
- The title (the `title` setting, else the platform's name, else the topic) top left, in the
  platform colour when there is one, and a status line beside it (see below).
- With `history` above 0, the last N pings fade in behind the current one (oldest
  faintest), as profile lines only; they dim with the current ping when it goes stale.

Both axes use the **same metres per pixel**, so a beam at 30 degrees looks like 30 degrees.
The widget's shape decides how much across-track is visible.

## Platform colour

With four boats on one display, each view should say which boat it shows. The widget
subscribes to `/marine/platforms` and finds the platform whose `platform_namespace` owns the
detections topic: `/urchin/sensors/mbes/detections` belongs to the platform with namespace
`urchin` (a leading slash is optional on either side; the longest matching namespace wins).
On a match:

- the swath lines, the profile (and its dots and history) and the title are drawn in the
  platform's `color` (flagged beams stay red);
- an empty `title` setting shows the platform's `name` instead of the topic.

Without a match the colours above are unchanged and the topic is the title. The lookup uses
the topic as actually subscribed (the node's namespace and any remap applied), so a remapped
view is identified by the data it shows. A topic with no namespace (`detections`) never
matches, and the default relative `sensors/mbes/detections` only matches in an rqt node whose
namespace is a platform's: otherwise give the full topic, `/urchin/sensors/mbes/detections`.
A platform whose colour has
`a == 0` has no colour (as in CAMP): the name is still used, the colours stay default. Other
alpha values are ignored; the colour is drawn opaque.

Each boat's `platform_send` publishes its own one-platform list on the shared topic, so the
widget keeps every platform it has seen, by namespace (the newest entry per namespace wins),
rather than the last list. It updates live: a changed colour or a newly listed platform is
used on the next list, and a changed topic is looked up again at once. A boat whose list stops
arriving keeps its last colour. The lists arrive about once a second, so a view started
before its boat is listed is default-coloured for that long.

The `color` setting (`#rrggbb`, empty = off) overrides the platform colour, for a sonar that
is not a listed platform. A value that is not `#rrggbb` is dropped to "off" when a saved
perspective or `--config` file is read, and refused in the settings dialog.

The status line ends with where the colour came from: `colour: platform urchin`,
`colour: manual override`, `colour: default (no platform match)`,
`colour: default (platform urchin has no colour)`, or `colour: default (<why /marine/platforms
cannot be followed>)` when `marine_interfaces` is missing or the subscription fails (the plot
still works).

The lookup itself is `rqt_ping_profile/platform_lookup.py` (pure Python, no Qt or ROS
imports) for other per-vehicle plugins to reuse: `match_platform(platform_list, topic)` and
`PlatformDirectory` for merging lists.

## Depth axis

- **fixed** (default): `depth_min` to `depth_max` (0 to 30 m) fill the height. Instances
  with the same range are directly comparable. Anything deeper is cut off at the bottom of
  the plot; across-track beyond the width is cut off at the sides.
- **auto**: the scale follows the ping, from the transducer (0 m) to the deepest OK
  detection plus 10 % headroom (never zoomed in past 1 m). It also widens, if needed, so the **whole swath is always
  visible** (the larger of what depth and width require). To keep it from jumping with
  every ping, it grows at once but shrinks only when the need falls below 70 % of the
  current extent. History pings do not take part in the scaling and may be cut off.

## Status line

| Text | Meaning |
|---|---|
| `colour: ...` | Always last: where the colour comes from (see Platform colour). Not a problem; it does not turn the line amber. |
| `waiting for the first ping on <topic>` | Subscribed, nothing received yet. |
| `NO PING FOR 7 s` | No usable ping for more than `stale_timeout`; the picture dims. Measured on this machine's monotonic clock from the last good ping, not from `header.stamp`, which is on the sonar's clock (sim time in the demo). |
| `unusable ping: <why>` / `unusable ping ignored: <why>` | The last message could not be drawn: no beams, arrays of different lengths, or no usable sound speed. Nothing is drawn from it; the last good ping stays on screen, and goes stale in the normal way. Logged at most once every 10 s, whatever the reason. |
| `no OK detections in the last ping` | Every beam was flagged. |
| `N invalid beams dropped` | Beams left out: non-finite or negative travel time, non-finite angle, an angle of 90 degrees or more either side of straight down (not a downlooking beam), or an absurd range (over 1000 km). A ping with no usable beam at all is an unusable ping. |
| `cannot subscribe to <topic>: ...` | The topic name is not valid; fix it in the settings. |

## Settings

Saved with the rqt perspective (`config_yaml` in the plugin instance settings). The wrench
opens the dialog.

| Setting | Default | Meaning |
|---|---|---|
| `topic` | `sensors/mbes/detections` | `SonarDetections` topic (resolved in the rqt node's namespace). |
| `title` | empty | Name shown in the corner (a boat name); empty shows the matched platform's name, else the topic. |
| `color` | empty (off) | `#rrggbb` colour override for the swath, profile and title; wins over the platform colour. Empty uses the platform colour from `/marine/platforms`. A value that is not `#rrggbb` is treated as off. |
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
# --color '#ff8800'          colour override ('' = none, clears one from --config)
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

## Threading and message hand-off

The subscription callback runs on the ROS thread. It stamps the receipt time, parks the
message in a one-slot mailbox (a newer message replaces an older one that the GUI has not
taken yet) and wakes the Qt thread; the store and every widget are touched on the Qt thread
only. A busy GUI therefore skips pings instead of queueing them, and a late-handled ping
does not look fresh.

The `PlatformList` callback uses a mailbox too, but keyed by namespace: a burst of lists
keeps the newest entry of every boat instead of only the newest list, because each boat
publishes its own one-boat list on the shared topic and "newest list wins" would drop boats.
