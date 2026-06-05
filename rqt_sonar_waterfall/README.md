# rqt_sonar_waterfall

An [rqt](https://wiki.ros.org/rqt) plugin that renders a scrolling **backscatter
waterfall** from `marine_acoustic_msgs/RawSonarImage`, with client-side image
processing and an optional sonar-control panel.

> **Status:** under active development on issue
> [#39](https://github.com/rolker/rqt_operator_tools/issues/39). This is the
> package scaffold — the plugin loads and shows an empty waterfall canvas; the
> data path, view knobs, and control panel are being filled in.

## Design

The viewer is built around a geometry-agnostic waterfall (one row per ping) fed
by a `RowExtractor` strategy, so it can cover both sonar geometries from the
same `RawSonarImage` contract:

| Geometry | `beam_count` | Waterfall row | Status |
|----------|--------------|---------------|--------|
| Sidescan | 1 | the per-ping sample series (port\|starboard) | this package (#39) |
| Multibeam backscatter | M | one value per beam (reduce samples) | deferred ([#40](https://github.com/rolker/rqt_operator_tools/issues/40)) |

## Topics

- **Subscribes** (`marine_acoustic_msgs/RawSonarImage`): a port and a starboard
  topic, selected in the toolbar (e.g. `edgetech_sonar`'s
  `sonar_image_{port,starboard}_{low,high}`).
- **Optional sonar controls** (`marine_radar_control_msgs`): when a control
  **state** topic (`RadarControlSet`) is selected, the panel builds the
  advertised controls dynamically and publishes `RadarControlValue` on the
  derived `change_state` topic — the same scheme as `rqt_marine_radar`. Sources
  that advertise no control set (e.g. `edgetech_sonar`) simply have no panel.

`garmin_sidescan` is expected to publish `RawSonarImage` and advertise its
controls via `marine_radar_control_msgs`; it will not display here until its
`RawSonarImage` output lands.

## Usage

```bash
ros2 run rqt_sonar_waterfall rqt_sonar_waterfall
# or load "Sonar Waterfall" from the rqt Plugins → Robot Tools menu
```
