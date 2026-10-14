# rqt_coverage

Survey-area coverage and quality scoreboard for the operator station, fed by the
`coverage_assessment` node (rolker/cube_bathymetry, `coverage_assessment` package).
Part of the Ocean Discovery Day scoreboard (rolker/unh_marine_simulation#87).

Two views of the same data:

- **Glance**: per area, two big numbers, the percent of the area covered and the percent
  covered to the survey standard, plus a status line. Readable across a room.
- **Detail**: per area, the standard in force (`name`, `a`, `b`), cell size and cell
  counts, the two percentages, the last assessment time, and a per-source (per boat)
  table of covered, to-standard and unique credit.

With more than one area in view, the glance view adds a tile on top for **all areas
together**, and the detail view a heading with the same numbers.

## Input

| Topic (default) | Type | QoS |
|---|---|---|
| `coverage_assessment` | `marine_interfaces/CoverageAssessment` | reliable, transient_local, keep last 100 (what the scorer offers; a volatile or best_effort subscription would not receive the latched areas) |

The plugin keeps the latest assessment per `area.id`. A late joiner is replayed up to the
last 100 assessments oldest first, so repeats of an area arrive before its newest; the last
one to arrive is held. Arrival order decides, not `header.stamp`, which is on the scorer's
clock and goes backwards when a sim restarts. Changing the topic clears the areas of the old
one.

A late joiner's first picture is the scorer's latched last assessment, whose true age is
unknown here: the status line counts from when it arrived (`received 3 s ago`), and the
detail view shows `header.stamp` as the last assessment time on the scorer's clock.

## Scores

All percentages are of the area's `area_total` (the rasterised polygon). When `area_total`
is 0 the percentages show `--`.

- **covered** = `area_covered / area_total`.
- **to standard** = `area_meeting_standard / area_total`, so it never exceeds covered.
- **All areas** = the sum over the areas in view (its title says how many areas are in the
  sum and how many selected ones are still waiting; its status line is that of the oldest
  area, so one stale area marks the roll-up): `sum(covered) / sum(total)` and the same
  for the standard, so a big area weighs more than a small one. Areas with no total are
  skipped. The message carries no overlap between areas, so a cell inside two overlapping
  areas is counted twice, and each area is judged against its own standard.
- **combined** (off by default) = `100 * (w_spec * to_standard + w_covered * (covered -
  to_standard))`, defaults `w_spec = 1.0` (cells to standard count fully) and `w_covered =
  0.5` (covered below the standard count half). Weights are 0 to 1; anything else falls
  back to the default.

If the scorer breaks the message's invariant (`meeting <= covered <= total`, a source covering more than the assessment, unique credit summing to more than what is covered, or a
non-finite or negative area), the plugin clamps the values and the status line says so
instead of showing a clean-looking number.

With many areas the glance tiles keep a readable minimum height and the panel scrolls.
If a publisher on the topic does not offer reliable + transient_local, the notice line says
`QoS mismatch` (otherwise that looks the same as "waiting for the scorer"). Repeated ids in
`selected_areas` are dropped.

## "No new assessment for N s"

The scorer publishes only when a grid or the water surface changed; otherwise the last
assessment stays published. A quiet interval is therefore normal when the boats are
stopped or the survey is finished. After the aging window (default 60 s) a tile dims and
its status line reads `no new assessment for 75 s`, with the old numbers kept visible.
The age is measured from when the assessment arrived here, not from `header.stamp`, which
is on the scorer's clock (sim time in the demo). Silence cannot tell an idle survey from
a dead scorer; a liveness signal is proposed in rolker/cube_bathymetry#169.

## Settings

Saved with the rqt perspective (`config_yaml` in the plugin instance settings). The wrench
opens the dialog; the view can also be toggled from the plugin's own button.

| Setting | Default | Meaning |
|---|---|---|
| `topic` | `coverage_assessment` | Topic to subscribe to (resolved in the rqt node's namespace). |
| `view` | `glance` | `glance` or `detail`. |
| `selected_areas` | empty (all) | Area ids to show; an id not received yet shows `waiting for first assessment`. |
| `font_scale` | 1.0 | 0.5 to 4.0, multiplies the size the layout picks. |
| `show_combined` | off | Show the combined score. |
| `weight_spec`, `weight_covered` | 1.0, 0.5 | Combined-score weights. |
| `aging_timeout` | 60 s | Seconds without a new assessment before the aging mark; 0 disables. |

## Areas never go away

The topic is latched, so an area the scorer no longer holds stays in the plugin. Use the
**Areas** button: *Forget area X* or *Clear all areas*. After starting a fresh assessment
(`reset_cube_nodes.sh --consumer /coverage_assessment`, see the `cube_bathymetry`
README) clear the plugin too, or the old areas stay until overwritten. The plugin has no
reset button of its own.

## Running

In rqt: *Plugins > Robot Tools > Survey Coverage*. As its own window (wall display):

```bash
ros2 run rqt_coverage survey_coverage --view glance --always-on-top
# --config <settings.yaml>   the same keys as the settings table
# --topic <topic>            override the topic
```

## Tests

`./ui_ws/test.sh rqt_coverage` from a layer worktree. The model tests need no display or
ROS graph; the widget smoke test runs with `QT_QPA_PLATFORM=offscreen` (set by the test).
