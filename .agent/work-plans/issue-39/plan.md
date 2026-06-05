# Plan: rqt_sonar_waterfall — rqt waterfall viewer for RawSonarImage (sidescan)

## Issue

https://github.com/rolker/rqt_operator_tools/issues/39

## Context

Sidescan sources publish (or soon will) `marine_acoustic_msgs/RawSonarImage`
(`edgetech_sonar` today; `garmin_sidescan` in development). `rviz_sonar_image`
covers RViz fan/curtain rendering, but there is no live operator-facing
waterfall inside rqt. This adds a new `rqt_sonar_waterfall` package to the
operator-tools repo, modeled on `rqt_camera_grid`'s structure (modern
`ament_cmake`, pure-logic modules + gtest, AUTOMOC) and `rqt_marine_radar`'s
rqt-plugin + dynamic-control mechanics.

`RawSonarImage.image.data` is row-major `samples_per_beam × beam_count`.
Sidescan = `beam_count == 1`. The waterfall is built one row per ping. The
viewer is designed around a `RowExtractor` abstraction so multibeam backscatter
(`beam_count > 1`, collapse samples per beam) can be added later without a
rewrite — that extractor is deferred to #40; only the sidescan strategy ships here.

## Approach

1. **Package skeleton** — `rqt_sonar_waterfall/` mirroring `rqt_camera_grid`:
   `CMakeLists.txt`, `package.xml`, `plugin.xml`, `resource/`, `scripts/`,
   `include/`, `src/`, `test/`, `README.md`. Register via
   `pluginlib_export_plugin_description_file(rqt_gui plugin.xml)`.
2. **`waterfall_model` (pure logic, no Qt/ROS)** — decode + assembly, gtest-first:
   - `decode_samples(SonarImageData) -> std::vector<float>` honoring `dtype` +
     `is_bigendian` (INT16 first; switch covers UINT8/16/INT8/16/32/FLOAT32).
   - `WaterfallRow { std::vector<float> intensities; float range_max; rclcpp::Time stamp; }`.
   - `RowExtractor` interface; `SingleBeamExtractor` (requires `beam_count==1`):
     passthrough samples, range axis = `sound_speed·samples_per_beam/(2·sample_rate)`.
   - `PortStarboardCombiner` — pair a port + stbd row (port reversed) into one
     centered row; tolerate single-channel.
   - `WaterfallBuffer` — fixed-height ring buffer of rows + dirty tracking.
3. **`WaterfallWidget` (QWidget)** — paints the `WaterfallBuffer` as a scrolling
   `QImage`; applies client-side gain/contrast + color map (`color_map` adapted
   from `rviz_sonar_image`: greyscale + bronze); freeze; range-axis ruler.
4. **`SonarWaterfallPlugin : rqt_gui_cpp::Plugin`** — port + starboard
   `RawSonarImage` topic combos with live discovery (refresh via node graph),
   subscriptions, settings save/restore, control wiring.
5. **Controls panel**:
   - *View knobs* (always on): gain, contrast, color map, range scale, history
     length, port/stbd enable, freeze.
   - *Sonar controls* (optional): transmit on/off + range published to
     **configurable** topic names (params, defaulting to the garmin interface);
     the section auto-hides when those topics are absent from the graph.
6. **Tests** — gtest over `decode_samples`, `SingleBeamExtractor`,
   `PortStarboardCombiner`, `WaterfallBuffer` (synthetic `RawSonarImage`);
   one offscreen widget smoke test (`QT_QPA_PLATFORM=offscreen`).
7. **Docs** — README: subscribed topics, controls, dtype support, deferred
   multibeam (#40).

## Files to Change

| File | Change |
|------|--------|
| `rqt_sonar_waterfall/CMakeLists.txt` | New ament_cmake build (AUTOMOC, gtests) |
| `rqt_sonar_waterfall/package.xml` | Deps: rclcpp, rqt_gui(_cpp), qt_gui_cpp, pluginlib, marine_acoustic_msgs, Qt5 |
| `rqt_sonar_waterfall/plugin.xml` | rqt_gui plugin registration (group "Robot Tools") |
| `include/rqt_sonar_waterfall/*.hpp` | waterfall_model, row_extractor, waterfall_buffer, color_map, waterfall_widget, sonar_waterfall_plugin |
| `src/*.cpp` | Implementations |
| `test/test_*.cpp` | decode, extractor, combiner, buffer, widget smoke |
| `resource/rqt_sonar_waterfall`, `scripts/rqt_sonar_waterfall` | ament marker + launcher |
| `rqt_sonar_waterfall/README.md` | Topics, controls, dtype, #40 pointer |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Test what breaks | Sample decode (dtype/endianness) + row assembly are pure-logic and gtest-covered; widget gets an offscreen smoke test |
| Only what's needed | Multibeam extractor deferred to #40; ship the sidescan strategy behind a small abstraction, not speculative multibeam code |
| A change includes its consequences | README documents the topic/control contract; garmin display gated on its RawSonarImage conversion (noted, not silently broken) |
| Follow ROS 2 conventions (ADR-0008) | Mirrors rqt_camera_grid/rqt_marine_radar layout, pluginlib export, ament gtest |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 ROS 2 conventions | Yes | Standard ament_cmake rqt plugin; reuses repo idioms |
| ADR-0002 Worktree isolation | Yes | Work in `feature/issue-39` layer worktree |
| ADR-0013 progress.md vocabulary | Yes | Plan/implementation entries appended to progress.md |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| Add new package to repo | repo README package list | Yes |
| Sonar-control topic names | garmin_sidescan ROS interface (external repo) | No — params + docs; coordinate when garmin lands |
| Color map source | reference `rviz_sonar_image` color_map | Yes — adapt, attribute |

## Open Questions

- Color map: copy/adapt `rviz_sonar_image`'s `color_map` into this package, or
  factor it into a shared lib? Default: adapt-and-attribute now (no new shared
  package) to keep scope contained.
- Default sonar-control topic names — confirm against the garmin node's eventual
  interface (`~/transmit`, `~/range`); kept as parameters until then.

## Estimated Scope

Single PR. Sizable but self-contained; multibeam extractor is a separate PR (#40).
