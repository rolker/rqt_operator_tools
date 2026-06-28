# Plan: rqt_sonar_waterfall — mark sidescan target → Contact to operator bag

## Issue

https://github.com/rolker/rqt_operator_tools/issues/86

## Context

Operator can view sidescan live in `rqt_sonar_waterfall` but cannot mark a target. The
slant→ground geometry (#58) is already available in `waterfall_model.hpp`; the Contact
construction logic lives in `marine_perception_tools::make_box_contact()` (Qt-free,
unit-tested) but is only internal to `sidescan_core`. `WaterfallRow` carries `stamp`
and `altitude` but no per-ping vessel pose; `WaterfallWidget` has no mouse/mark hooks.

**Operator decisions (locked):**
1. Contact builder = shared exported Qt-free library in `marine_perception_tools`.
2. Pose source = TF `earth→sensor_frame` at each ping's stamp (tf2_ros).
3. Mark granularity = box-drag → Contact BOX extent (matching offline sidescan_canvas).

**Design rationale (ADR-level):** Extracting `make_box_contact()` into an exported
library target within `marine_perception_tools` avoids duplicating Contact construction
logic and keeps both contact sources forward-compatible with `contact_manager` (#167).
No new package is warranted for this near-term scope; an exported ament target within
the existing package is sufficient. This is recorded here per the operator's request.

This is a **two-repo, two-phase** change: Phase 1 touches `marine_perception_tools`
(separate PR, prerequisite); Phase 2 is the main `rqt_operator_tools` PR for issue #86.

## Approach

### Phase 1 — `marine_perception_tools`: export `contact_builder` library

1. **Extract contact_builder** — move `MapPoint` + `make_box_contact()` out of
   `src/contact_store.hpp/cpp` into `include/marine_perception_tools/contact_builder.hpp`
   + `src/contact_builder.cpp`. The contact_store files delegate to the new header.
2. **Add + export the library** — new `contact_builder` ament target in
   `CMakeLists.txt` with deps `marine_interfaces`, `geographic_msgs`, `builtin_interfaces`;
   add `install(TARGETS contact_builder EXPORT export_contact_builder ...)`,
   `ament_export_targets(export_contact_builder HAS_LIBRARY_TARGET)`, and
   `ament_export_dependencies(marine_interfaces geographic_msgs builtin_interfaces)`.
3. **Wire sidescan_core** — `sidescan_core` links `contact_builder` (replaces inline impl).
4. **Test** — existing `test_contact_store.cpp` continues to pass; update include paths if needed.

### Phase 2 — `rqt_sonar_waterfall`: waterfall marking feature

5. **Enrich WaterfallRow with per-ping pose** — add `std::string sensor_frame`,
   `geometry_msgs::msg::Transform sensor_to_earth{}`, `bool has_pose = false` to
   `WaterfallRow`; populate `sensor_frame` from `RawSonarImage::header.frame_id` in
   `SingleBeamExtractor::extract()`. **(must-fix #2)** `combine_rows()` builds a
   fresh `WaterfallRow`, so it must propagate these three pose fields from a
   present side (port preferred) or a combined port+stbd row loses its frame
   before `post_row()` and becomes un-markable. Updated in `waterfall_model.cpp`
   + covered by `test_combine_rows.cpp`.
6. **TF lookup in post_row()** — add `tf2_ros::Buffer tf_buffer_` +
   `tf2_ros::TransformListener tf_listener_` to `SonarWaterfallPlugin`; in `post_row()`
   call `tf_buffer_.lookupTransform("earth", row.sensor_frame, stamp_as_time_point)` and
   store result in `row.sensor_to_earth` / `row.has_pose`. Non-blocking: skip if TF misses
   (row goes in the buffer with `has_pose=false`; a missed-pose row is un-markable).
7. **Mark mode in WaterfallWidget** — add `bool mark_mode_` toggle + drag state
   (`mark_start_`, `mark_current_`, `bool marking_`); implement `mousePressEvent`,
   `mouseMoveEvent`, `mouseReleaseEvent`; draw in-progress drag rectangle via
   QPainter overlay in `paintGL()` (compatibility profile allows QPainter post-GL
   draw, already used for range labels). **(plan-review suggestion)** The widget
   owns `buffer_`/ring/scroll, so it resolves the rect → rows itself and emits a
   `boxMarked(const MarkBox &)` carrying the across-track range bounds + the
   spanned rows (each with its pose). The plugin never replicates scroll/ring
   geometry from a bare `QRectF`. Helpers: `range_at_x()`, `rows_in_y_range()`.
8. **Georeference + publish** — pure `contact_georef.{hpp,cpp}`
   (`georeference_box()`, Qt-free, unit-tested) plus slot `on_box_marked(const
   MarkBox &)` on `SonarWaterfallPlugin`. **(must-fix #1)** REP-105 `earth` is
   ECEF, but `make_box_contact()` takes map-frame ENU metres — feeding ECEF deltas
   as corners yields a rotated box with wrong dimensions. So the two concerns are
   split:
   - The BOX **extent** is built directly in the sensor's local frame (x =
     alongtrack, y = athwartship metres): athwartship width = across-track ground
     range span (from `display_half_width_`/`display_is_ground_`); alongtrack
     length = vessel travel between the first/last marked ping's `sensor_to_earth`
     translation. These `MapPoint` corners go to
     `marine_perception_tools::make_box_contact(corners, id, "sidescan", frame, stamp)`
     so `shape.dimensions` are true metres; `frame` = the sensor frame.
   - Only the geodetic **centroid** is resolved: the centroid's sensor-frame
     offset is rotated into ECEF by the middle ping's pose, added to its ECEF
     origin, and converted ECEF→geodetic via `GeographicLib::Geocentric::Reverse`
     to fill `geo_pose.position` (lat/lon/alt). Axis convention assumes REP-103
     body (x fwd, y port/left) — see Open Questions.
   - Publish on `sonar_waterfall/contacts` (relative; resolves under the rqt node
     namespace).
9. **Mark-mode toolbar button** — "Mark Target" toggle button in `build_controls_bar()`;
   connect to `widget_->set_mark_mode(bool)`.
10. **Contact overlay** — only the in-progress drag rectangle is drawn (QPainter
    overlay in `paintGL()`). A persistent overlay of confirmed contacts is
    **deferred to #59**: the waterfall scrolls, so a fixed widget-pixel rect would
    drift from the data and mislead; the published Contact (+ `RCLCPP_INFO` log) is
    the durable confirmation for this near-term tool.
11. **Operator bag config** — **(cross-repo follow-up, NOT in this PR)** The bag
    config `bizzyboat.yaml` lives in a *different* repo
    (`unh_echoboats_project11`), which is not part of the `rqt_operator_tools`
    worktree, so it cannot be changed atomically in #86's PR. `bizzyboat.yaml`
    has three `record:` blocks (main `/**/` logger, the operator-station bag, and
    `/**/sonar_logger`); the marked Contact is published operator-side, so the
    resolved absolute topic must be added to the **operator-station bag**'s
    `record: topics:` list in that repo. Tracked as a follow-up there.
12. **Tests**:
    - `test_waterfall_widget.cpp`: add mark-mode test — synthesize mouse events on
      offscreen widget, assert `boxMarked` signal fires with correct rect.
    - New `test_contact_georef.cpp`: unit test the pixel→ground-range→world pipeline
      as a pure function (no GL, no TF).
    - Update `test_waterfall_buffer.cpp` if `WaterfallRow` struct additions break
      initializer lists (should be default-initialized, so likely no change).

## Files to Change

| File | Change |
|------|--------|
| `marine_perception_tools/include/marine_perception_tools/contact_builder.hpp` | **New** — exported `MapPoint` + `make_box_contact()` header |
| `marine_perception_tools/src/contact_builder.cpp` | **New** — extracted implementation |
| `marine_perception_tools/src/contact_store.hpp` | Remove duplicated declarations, `#include contact_builder.hpp` |
| `marine_perception_tools/src/contact_store.cpp` | Delegate `make_box_contact` to contact_builder |
| `marine_perception_tools/CMakeLists.txt` | Add + export `contact_builder` target |
| `rqt_sonar_waterfall/include/.../waterfall_model.hpp` | Add `sensor_frame`, `sensor_to_earth`, `has_pose` to `WaterfallRow` |
| `rqt_sonar_waterfall/include/.../waterfall_widget.hpp` | Add `boxMarked` signal, mark-mode API, drag state |
| `rqt_sonar_waterfall/include/.../sonar_waterfall_plugin.hpp` | Add tf2 members, contact publisher, mark button |
| `rqt_sonar_waterfall/src/waterfall_widget.cpp` | Mouse event handlers, drag overlay in paintGL |
| `rqt_sonar_waterfall/src/sonar_waterfall_plugin.cpp` | TF setup, `on_box_marked()`, mark button wiring |
| `rqt_sonar_waterfall/src/row_extractor.cpp` | Populate `sensor_frame` from `RawSonarImage.header.frame_id` |
| `rqt_sonar_waterfall/src/waterfall_model.cpp` | **(must-fix #2)** `combine_rows()` propagates the 3 pose fields |
| `rqt_sonar_waterfall/include/.../contact_georef.hpp` | **New** — `georeference_box()` + `GeorefBox` (ENU extent / geodetic centroid split) |
| `rqt_sonar_waterfall/src/contact_georef.cpp` | **New** — pure georeference impl (GeographicLib::Geocentric::Reverse) |
| `rqt_sonar_waterfall/CMakeLists.txt` | Add `geometry_msgs`, `marine_interfaces`, `geographic_msgs`, `tf2`, `tf2_ros`, `marine_perception_tools::contact_builder`, GeographicLib |
| `rqt_sonar_waterfall/package.xml` | Add `geometry_msgs`, `marine_interfaces`, `geographic_msgs`, `tf2`, `tf2_ros`, `marine_perception_tools`, `geographiclib` depends |
| `rqt_sonar_waterfall/test/test_waterfall_widget.cpp` | Add mark-mode tests (drag→signal; off-mode + non-metric negatives) |
| `rqt_sonar_waterfall/test/test_combine_rows.cpp` | **(must-fix #2)** pose-propagation tests |
| `rqt_sonar_waterfall/test/test_single_beam_extractor.cpp` | `sensor_frame` from header test |
| `rqt_sonar_waterfall/test/test_contact_georef.cpp` | **New** — georeferencing unit test (extent + geodetic centroid) |
| Operator bag config (`bizzyboat_project11/config/bizzyboat.yaml`) | **Cross-repo follow-up** (different repo; not in this PR — see step 11) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Operator drags box explicitly; mark-mode button makes mode visible; overlay confirms mark; topic name is discoverable |
| Capture decisions, not just implementations | Three design decisions recorded here (shared lib, TF pose, box-drag) |
| A change includes its consequences | WaterfallRow tests updated; bag config updated in same PR; contact_store tests preserved |
| Only what's needed | No contact_manager backend, no TargetAnnotation, no UI complexity beyond a toggle button |
| Test what breaks | Georeferencing path (pixel→world) unit-tested; mark-mode interaction tested with offscreen widget |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 (ROS 2 conventions) | Yes | `package.xml` uses `<depend>` for new ROS deps; `ament_target_dependencies` for new find_packages; topic name follows ROS 2 naming |
| ADR-0002 (worktree isolation) | Yes | Already in `feature/issue-86` worktree |
| ADR-0001 (adopt ADRs) | Watch | Cross-repo shared lib choice recorded as plan-level rationale per operator instruction; does not require a formal ADR |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `WaterfallRow` gains 3 fields | `test_waterfall_buffer.cpp` initializers | Yes (step 12) |
| `row_extractor.cpp` reads `header.frame_id` | `test_single_beam_extractor.cpp` mock messages | Yes (implicit; verify at implementation time) |
| New contact publisher on `sonar_waterfall/contacts` | Operator bag recording config | Yes (step 11) |
| `contact_store.hpp` moves to exported header | `sidescan_core` include paths, `test_contact_store.cpp` | Yes (Phase 1, step 4) |

## Open Questions

- [ ] **Athwartship axis convention (verify in field).** `georeference_box()`
  assumes the sidescan sensor frame is REP-103 body (x forward = alongtrack, y
  left = port, z up), so a port return (negative display range) maps to sensor
  +Y. The extent (box dimensions) is correct regardless of this; only the
  across-track *side* of the resolved geo centroid depends on it. Documented in
  `contact_georef.hpp`; confirm against the live Garmin sidescan mount before
  trusting which side of the track contacts land on.
- [x] **Bag config path.** Resolved: `bizzyboat.yaml` in `unh_echoboats_project11`
  (a *separate* repo) has the `record: topics:` blocks. Because it is cross-repo
  it is a follow-up there, not part of #86's PR (see step 11). The Contact is
  published operator-side → operator-station bag block.
- [x] **`earth` (ECEF) broadcast.** Per the issue brief the operator/boat TF tree
  broadcasts `earth → <prefix>/map → … → base_link` with the sidescan frame off
  base_link via tf_static, so `lookupTransform("earth", sensor_frame)` resolves
  at runtime. A TF miss is non-fatal (row stays un-markable).

## Estimated Scope

Two PRs: **Phase 1** (`marine_perception_tools`, small refactor, ~50 LOC) → **Phase 2** (`rqt_operator_tools` #86 main PR, ~400–600 LOC).
