---
issue: 86
---

# Issue #86 — rqt_sonar_waterfall: mark a sidescan target → publish marine_interfaces/Contact to operator bag (Massabesic near-term, subset of #59)

## Plan Authored
**Status**: complete
**When**: 2026-06-28 00:00 +0000
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-86/plan.md` at `b0f8e66`
**Branch**: feature/issue-86 at `b0f8e66`
**Phases**: 2 (Phase 1: marine_perception_tools contact_builder export; Phase 2: rqt_sonar_waterfall marking feature)

### Open questions
- [ ] Verify sensor frame axis convention for athwartship ground range (which TF axis is across-track for Bizzyboat's sidescan).
- [ ] Confirm exact operator bag recording config file path (plan points to `bizzyboat_project11/config/bizzyboat.yaml`).

## Issue Review
**Status**: complete
**When**: 2026-06-28 00:00 +0000
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #86
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Near-term operator tool scoped appropriately for the Massabesic survey window.
Publishes the already-defined `marine_interfaces/Contact` (bypassing the unsettled
`TargetAnnotation` / `marine_targets` C0 message). Depends on #58 (closed,
geometry code available in `waterfall_model.hpp`) and `marine_interfaces/Contact`
(defined). Explicit out-of-scope list (contact_manager, TargetAnnotation, #59)
keeps it tight.

### Scope Assessment

**Well-scoped?** Yes — four focused wiring steps: (1) enrich rows with per-ping
pose, (2) add mark-mode to `waterfall_widget`, (3) georeference the pick, (4)
publish Contact + display overlay. All four are well-bounded and fit a single PR.

**Right repo?** Yes — `rqt_operator_tools` under `layers/main/ui_ws/src/`.
Project-specific UI code; correctly placed in the project repo.

**Dependencies**:
- #58 (slant→ground geometry) — closed and merged; `ground_range()`, `project_row()`
  and `WaterfallRow.altitude` are available in `waterfall_model.hpp`.
- `marine_interfaces/Contact` — defined; `make_box_contact()` in
  `marine_perception_tools` already builds one and is Qt-free + unit-tested.
- #59 (full-fidelity target marking) — stays open as the future goal; this issue
  is explicitly a non-blocking subset.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Operator explicitly clicks/drags to mark; overlay gives immediate visual confirmation; topic name makes bag content discoverable |
| Capture decisions, not just implementations | Action needed | Three open design decisions (shared-lib vs. local builder; TF vs. odom pose source; box vs. single-click granularity) must be recorded in the plan before implementation |
| A change includes its consequences | Watch | If `WaterfallRow` gains a pose field, all five existing waterfall model tests need updating; operator bag recording config must be verified to capture the new contact topic |
| Only what's needed | OK | Explicitly excludes `TargetAnnotation`, `contact_manager`, and #59 scope; reuses existing geometry and Contact-construction code |
| Improve incrementally | OK | Clear subset of #59; ships minimum viable operator capability for the near-term field window |
| Test what breaks | Watch | Georeferencing logic (pixel → ground range + pose → lat/lon) is the most regression-prone path; needs unit tests; mark-mode UI interaction is harder to test but at minimum deserves a `waterfall_widget` test stub |
| Modularity and Decoupling (project) | Watch | `marine_perception_tools::make_box_contact()` is Qt-free and unit-tested; duplicating it in `rqt_sonar_waterfall` would create two diverging Contact-construction paths incompatible over time with `contact_manager` (#167); shared-lib or direct link is strongly preferred |
| Standards Compliance (project) | Watch | ADR-0008: new publisher and `package.xml` dependency on `marine_interfaces` (and possibly `tf2_ros`) must follow ROS 2 Rolling conventions; verify `find_package` + `ament_target_dependencies` usage |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0002 — Worktree isolation | Yes | Already in worktree `issue-rqt_operator_tools-86` ✓ |
| 0008 — Follow ROS 2 conventions | Yes | New ROS 2 publisher, new topic, new `package.xml` dep (`marine_interfaces`, `tf2_ros` or `nav_msgs`); must follow ROS 2 Rolling conventions for node/publisher lifecycle |
| 0001 — Adopt ADRs | Watch | The shared-lib vs. local-builder choice is a cross-repo architectural decision; if a new shared Qt-free lib is created it warrants a brief ADR or at minimum an explicit plan-level rationale |

### Consequences

- If `WaterfallRow` gains a pose field (`geometry_msgs/msg/Pose` or TF stamp):
  update `waterfall_model.hpp`, `waterfall_buffer` tests, `waterfall_widget` tests,
  and `test_waterfall_buffer.cpp`.
- New published topic must appear in the operator bag recording configuration
  (verify the existing config file path and add the contact topic).
- If a shared contact-construction lib is created: `marine_perception_tools`
  CMakeLists and `package.xml` may change; the new lib needs its own package +
  tests; both downstream packages (`marine_perception_tools`, `rqt_sonar_waterfall`)
  gain a `find_package` dep.
- Displaying mark overlays in `waterfall_widget` (a `QOpenGLWidget`) requires
  care with the GL context — overlay draw must happen inside `paintGL()` or a
  correctly-ordered `QPainter` overlay, not mixed arbitrarily.

### Recommendations

- **Resolve shared-lib vs. local builder first** (open design point 1): the
  simplest approach is to have `rqt_sonar_waterfall` directly depend on
  `marine_perception_tools` and call `make_box_contact()` — no new lib, one dep
  added. A separate shared lib is only warranted if a third consumer appears. The
  plan should record this choice explicitly.
- **Confirm pose source before coding** (open design point 2): verify whether
  `earth→<sensor frame>` TF is published live in the operator stack or whether
  odom/`nav_msgs/Odometry` is the right subscription. This determines whether
  `tf2_ros::Buffer` or a plain subscriber is needed.
- **Box mark granularity** (open design point 3): recommend box-drag → Contact
  BOX extent, matching the offline `sidescan_canvas` behavior and
  `marine_perception_tools::make_box_contact()` API.
- **Operator bag config**: identify and update the bag recording config file
  in the same PR so the contact topic is captured from day one — not a follow-up.
- **GL overlay approach**: confirm whether `paintGL()` QPainter or a sibling
  overlay `QWidget` is used; the latter avoids mixing QPainter and raw GL calls
  in the same context.

### Actions
- [ ] Record shared-lib vs. local-builder decision in the plan (recommend: direct dep on `marine_perception_tools::make_box_contact()`).
- [ ] Confirm and record pose source (TF `earth→sensor` vs. odom) in the plan before implementation.
- [ ] Confirm box-drag mark granularity (vs. single-click); align with `make_box_contact()` API.
- [ ] Update operator bag recording config to include the new contact topic in the same PR.
- [ ] Add unit test for georeferencing path (pixel → ground range + pose → lat/lon).
- [ ] Update affected `WaterfallRow` tests if the struct gains a pose field.
- [ ] Follow ADR-0008: add `marine_interfaces` (and `tf2_ros`/`nav_msgs`) to `package.xml` + CMakeLists with correct `ament_target_dependencies`.

## Plan Review
**Status**: complete
**When**: 2026-06-28 13:50 +0000
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-86/plan.md` at `b0f8e66`
**PR**: PR-less (--issue mode; reviewed local plan file)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) `combine_rows()` builds a fresh `WaterfallRow` and only copies select fields — it must also propagate the new `sensor_frame`/`sensor_to_earth`/`has_pose`; otherwise combined port+starboard sidescan rows lose the pose before `post_row()` and become un-markable. Add `src/waterfall_model.cpp` (`combine_rows`) + `test/test_combine_rows.cpp` to Files to Change — `plan.md:50` / `plan.md:88`
- [ ] (must-fix) Frame semantics for `make_box_contact` corners are muddled: REP-105 `earth` is ECEF, not ENU ("earth frame ENU" is contradictory). `make_box_contact`'s contract takes map-frame ENU metres; ECEF X/Y deltas yield a rotated box whose `shape.dimensions` are not true athwartship/alongtrack metres. Build the BOX extent in a local ENU/map frame (set `frame` accordingly) and resolve only the geodetic centroid via `Geocentric::Reverse` for `geo_pose` — `plan.md:64-71`
- [ ] (suggestion) Pixel→row mapping lives entirely in `WaterfallWidget` (owns `buffer_`, ring, scroll/history); `SonarWaterfallPlugin` holds only `QPointer<WaterfallWidget>` and no rows. Step 8's "find rows spanning the dragged Y-range" in `on_box_marked()` will have to replicate widget-internal scroll/ring state. Prefer resolving rect→rows in the widget (expose a `rowAt(y)`/buffer accessor or emit resolved rows) rather than emitting bare `QRectF rect_in_widget` — `plan.md:56` / `plan.md:64`
- [ ] (suggestion) Verify `earth` (ECEF) TF is actually broadcast in the operator stack before locking `lookupTransform("earth", sensor_frame)`; if only `map`/`odom` is published the lookup always misses and every row is un-markable. Tie this to Open Question #1 (athwartship axis convention) — `plan.md:53`
- [ ] (suggestion) Phase 1 (`marine_perception_tools` `contact_builder` export) is a cross-repo prerequisite with no tracked issue/PR or sequencing gate; #86's PR won't build until it merges and is available in the workspace. Note Phase 1's own issue/PR and the merge-before-#86 ordering in the plan — `plan.md:31`
- [ ] (suggestion) Open Question #2 partly resolved: `bizzyboat.yaml` does contain the `record: topics:` block (the plan's guess is correct); but confirm whether the contact belongs in the operator bag vs the separate sonar/main bag streams before editing — `plan.md:139`

## Implementation
**Status**: complete
**When**: 2026-06-28 16:10 +0000
**By**: Claude Opus

**Branch**: feature/issue-86
**Phase**: Phase 2 (`rqt_sonar_waterfall` waterfall marking). Phase 1
(`marine_perception_tools::contact_builder` export) was already merged and is
available in the workspace.

### What changed

Added box-drag target marking to the live sidescan waterfall. The operator
toggles **Mark target**, drags a box over a target, and the plugin publishes a
`marine_interfaces/msg/Contact` (`ORIGIN_HUMAN`, `STATUS_PROPOSED`, `Shape::BOX`)
so the operator bag captures it.

- **New topic / type**: `sonar_waterfall/contacts` (relative; resolves under the
  rqt node namespace) — `marine_interfaces/msg/Contact`, reliable, KeepLast(100).
- **Per-ping pose plumbing**: `WaterfallRow` gains `sensor_frame`,
  `sensor_to_earth` (`geometry_msgs/Transform`), `has_pose`. The extractor stamps
  `sensor_frame` from `RawSonarImage.header.frame_id`; `post_row()` resolves
  `earth`(ECEF)`<-sensor_frame` from TF at the ping stamp (zero-timeout,
  non-blocking; a TF miss leaves the row displayed but un-markable).
- **Widget mark mode**: `set_mark_mode()`, drag state + `mousePress/Move/Release`,
  an in-progress drag-rect overlay in `paintGL()`, and — per the plan-review
  suggestion — the widget resolves the rect → rows itself (`range_at_x()`,
  `rows_in_y_range()`) and emits `boxMarked(const MarkBox &)` carrying the
  across-track range bounds + spanned rows, so the plugin never replicates
  scroll/ring geometry.
- **Georeference** (`contact_georef.{hpp,cpp}`, pure/Qt-free, unit-tested):
  `georeference_box()` builds the BOX extent in the sensor's local frame and
  resolves the geodetic centroid via `GeographicLib::Geocentric::Reverse`.
- **Contact builder**: links `marine_perception_tools::contact_builder` and calls
  `make_box_contact()` (no second builder); fills `geo_pose` lat/lon/alt after.

### Must-fix resolutions

- **Must-fix #1 (frame semantics)**: `earth` is ECEF, `make_box_contact()` takes
  map-frame ENU metres. Split the concerns: the BOX **extent** is built directly
  in the sensor's local frame (athwartship width from the across-track ground-range
  span; alongtrack length from vessel travel between the first/last marked ping)
  so `shape.dimensions` are true metres with `frame` = the sensor frame; only the
  **geodetic centroid** is resolved ECEF→geodetic via `Geocentric::Reverse` for
  `geo_pose`. No ECEF deltas are fed as corners. (Axis convention assumes REP-103
  body x-fwd/y-port — documented; verify the across-track side in field.)
- **Must-fix #2 (combine_rows pose propagation)**: `combine_rows()` now propagates
  `sensor_frame`/`sensor_to_earth`/`has_pose` from a present side (port preferred),
  so combined port+stbd rows stay markable. Covered by `test_combine_rows.cpp`.

### Operator-bag config (cross-repo follow-up — NOT in this PR)

`bizzyboat.yaml` lives in a **separate** repo (`unh_echoboats_project11`), not in
this worktree, so it can't be changed atomically here. It has three `record:`
blocks (main `/**/` logger, operator-station bag, `/**/sonar_logger`). The Contact
is published operator-side, so the resolved absolute topic must be added to the
**operator-station bag**'s `record: topics:` there as a follow-up.

### Build / test

- `colcon build --packages-up-to rqt_sonar_waterfall` (the worktree had no
  prebuilt underlays; built the 11-package dependency chain from layer src, heavy
  non-marine deps already in jazzy). **rqt_sonar_waterfall builds clean.**
- `colcon test`: **271 tests, 0 failures, 49 skipped.** New gtests pass:
  `test_contact_georef` (5/5), `test_combine_rows` pose tests (8/8 total),
  `test_single_beam_extractor` frame test (8/8). The 3 new `test_waterfall_widget`
  mark-mode tests are **environment-skipped** (no GL context headless — same as all
  pre-existing widget GL tests); they compile and are wired to the box→rows→signal
  path and run where a GL context is available.
- `ament_uncrustify` + `ament_cpplint` **clean on all touched files**.
- One env note: `find_package(GeographicLib)` needed the system Find-module dir
  (`/usr/share/cmake/geographiclib`) added to `CMAKE_MODULE_PATH` (it ships a Find
  module, not a Config) — handled in `CMakeLists.txt`.

### Deferred (scoped out, noted in plan)

- Persistent overlay of confirmed contacts → #59 (the waterfall scrolls, so a
  fixed widget-pixel rect would mislead; the published Contact + `RCLCPP_INFO` is
  the durable confirmation for this near-term tool).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-28 16:42 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-86 at `3479f73`
**Mode**: pre-push
**Depth**: Deep (reason: 200+ lines, 17 files, coordinate math + executor-thread TF/concurrency)
**Must-fix**: 1 | **Suggestions**: 4
**Round**: 1 | **Ship**: continue — one genuine correctness must-fix, low-effort fix

Static analysis (cpplint + uncrustify) clean; reviewed against local `origin/jazzy`
(offline, may be slightly stale). Two disjoint-lens Claude Adversarial passes;
Copilot off (default). Both prior plan-review must-fixes (#1 ECEF/ENU split, #2
`combine_rows` pose propagation) confirmed resolved and tested. Plan adherence strong.

Dismissed as false positives (verified): `mark_counter_` atomic race (slot runs on
GUI thread via AutoConnection, single-threaded access); int32 `sec` cast "2038
overflow" (matches `builtin_interfaces/Time.sec` int32 by design); poses/stamps
length assertion + publisher QoS + quaternion normalization (defensive-only).

### Findings
- [x] (must-fix) Non-uniform scale: `range_at_x()` uses only the newest row's `display_half_width_` for all spanned rows, so a box marked with "Uniform scale" off + varying per-ping range yields a silently wrong Contact — `rqt_sonar_waterfall/src/waterfall_widget.cpp:296`
- [x] (suggestion) Carry original `builtin_interfaces/Time` (sec/nanosec) through `WaterfallRow` for an exact TF lookup stamp instead of re-splitting the flattened double — `rqt_sonar_waterfall/src/sonar_waterfall_plugin.cpp:709`
- [x] (suggestion) Make the world frame (`"earth"`) configurable via settings rather than hard-coded — `rqt_sonar_waterfall/src/sonar_waterfall_plugin.cpp:715`
- [x] (suggestion) Strengthen `MarkModeDragEmitsBox…` to assert the spanned-row count/indices, not just non-empty — `rqt_sonar_waterfall/test/test_waterfall_widget.cpp:240`
- [x] (suggestion) Add an operator status cue when a drag lands on un-markable (no-pose) rows — `rqt_sonar_waterfall/src/waterfall_widget.cpp:307`
- [x] (governance) File a tracking issue for the cross-repo operator-bag `record:` update (new `sonar_waterfall/contacts` topic) in `unh_echoboats_project11` (deferred: needs a GitHub issue in another repo — `gh` is unauthenticated in this sub-agent and the handoff scopes me to this worktree; the host should file it)

## Implementation
**Status**: complete
**When**: 2026-06-28 17:15 +0000
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-86 at `5773921`
**Addressed**: Local Review (Pre-Push) of 2026-06-28 16:42 +0000 (branch `feature/issue-86` at `3479f73`)
**Commits**: `918059b`, `ec1e362`, `5773921`

Addressed the one must-fix and the four suggestions from the pre-push review;
deferred the cross-repo governance item (issue-filing) to the host. Built the
full dependency chain (core `marine_interfaces`/`marine_control`, sensors
`depthai_marine`/`sea_surface_segmentation`/`marine_radar_control_msgs`, ui
`marine_perception_tools` et al.) and rebuilt `rqt_sonar_waterfall`:
`colcon test` → 272 tests, 0 failures, 50 skipped. cpplint + uncrustify clean
on all touched files.

> **Caveat for the re-review:** the offscreen-GL gtests in
> `test_waterfall_widget` (including the new non-uniform-scale mark test and the
> strengthened spanned-row assertions) **self-skip** here — this runner has no
> OpenGL 3.3 context, so they compile but do not execute. Their logic is
> unverified at runtime in this environment; run on a GL-capable host to exercise
> them.

### Actions
- [x] (must-fix) Non-uniform scale: `range_at_x()` now takes the georeferenced row's half-width — the representative (vertical-middle) spanned row when uniform scale is off, the shared display half-width when on — so a box marked with uniform scale off over varying per-ping range reports the marked row's true extent — `rqt_sonar_waterfall/src/waterfall_widget.cpp` (`918059b`)
- [x] (suggestion) Carry the original `builtin_interfaces/Time` through `WaterfallRow::stamp_time` (set in row_extractor, propagated in combine_rows) and key the TF lookup on it instead of re-splitting the double — `rqt_sonar_waterfall/src/sonar_waterfall_plugin.cpp` (`ec1e362`)
- [x] (suggestion) World TF frame made operator-configurable via a toolbar Frame field (default `earth`), persisted per perspective, read under a mutex on the executor thread — `rqt_sonar_waterfall/src/sonar_waterfall_plugin.cpp` (`ec1e362`)
- [x] (suggestion) `MarkModeDragEmitsBox…` now asserts the exact spanned-row count and contiguous oldest-first indices via per-row stamp tags — `rqt_sonar_waterfall/test/test_waterfall_widget.cpp` (`5773921`)
- [x] (suggestion) Transient operator cue drawn when a drag lands on un-markable (no-pose) rows; auto-clears and clears on the next drag/mode toggle — `rqt_sonar_waterfall/src/waterfall_widget.cpp` (`918059b`)
- [x] (governance) Tracking issue for the cross-repo operator-bag `record:` update — `unh_echoboats_project11` (deferred: cross-repo GitHub issue; `gh` unauthenticated and out of this worktree's scope — host to file)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-28 17:34 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-86 at `b0c0d79`
**Mode**: pre-push
**Depth**: Deep (reason: coordinate math + executor-thread TF, ~1040 LOC, 13 files)
**Must-fix**: 0 | **Suggestions**: 8
**Round**: 2 | **Ship**: recommended — no must-fix; Round-1 must-fix + 4 suggestions all resolved and independently verified

Re-review of the post-address-findings diff. Static analysis (ament_cpplint + ament_uncrustify)
clean on all 13 files. Two disjoint-lens Claude Adversarial passes (Deep); Copilot off (default).
Reviewed against local `origin/jazzy` (offline, may be slightly stale). Independently verified the
render geometry: `upload_texture`/GPU ring stretches `ring_filled_` rows across the full viewport
(newest at top), so `rows_in_y_range` screen-Y→index mapping is sound; the non-uniform-scale fix
(`range_at_x` taking the georeferenced row's half-width) is geometrically correct. Georef
quaternion/ECEF math confirmed against tests (identity + 90° + offset centroid). All five Round-1
findings confirmed resolved. Both adversarial passes converged; no new must-fix. Verified
`make_box_contact`'s map-frame contract against `marine_perception_tools/src/contact_store.cpp`:
the plugin passes a sensor frame + box-local corners (suggestion #1), but the Contact is
self-consistent and `geo_pose` (the archival authority per Contact.msg) is correctly resolved, so
it is a convention/interoperability suggestion, not a correctness must-fix for the operator-bag goal.

### Findings
- [ ] (suggestion) Contact uses a moving sensor frame for `header.frame_id`/`kinematics.pose` while other producers use stable `bizzy/map` ENU; works here because `geo_pose` is resolved (the archival authority), but a consumer reading `kinematics.pose` as map coords without TF-resolving at `header.stamp` mis-places it — `rqt_sonar_waterfall/src/sonar_waterfall_plugin.cpp:859`
- [ ] (suggestion) Single-spanned-ping mark → `alongtrack=0` → zero-length box published silently; guard a zero-extent box — `rqt_sonar_waterfall/src/contact_georef.cpp:618`
- [ ] (suggestion) Half-width "middle row" (`box.rows[size/2]`, all rows) and centroid "middle pose" (`poses[size/2]`, pose-filtered) diverge under non-uniform scale + partial TF coverage; comments claim they're the same ping — `rqt_sonar_waterfall/src/waterfall_widget.cpp:252`
- [ ] (suggestion) `source = "sidescan"` drops port/stbd provenance; Contact.msg convention is `"<sensor>.<stream>"` — `rqt_sonar_waterfall/src/sonar_waterfall_plugin.cpp:866`
- [ ] (suggestion) Contact publisher QoS reliable KeepLast(100) without `transient_local`; late recorder/viewer misses prior marks — `rqt_sonar_waterfall/src/sonar_waterfall_plugin.cpp:738`
- [ ] (suggestion) Zero/unknown `stamp_time` falls through to tf2 "latest" (`Time(0)`); gate on non-zero stamp — `rqt_sonar_waterfall/src/sonar_waterfall_plugin.cpp:808`
- [ ] (suggestion) Hardcoded `/usr/share/cmake/geographiclib` module path is distro/version-specific; prefer a ROS-exported Find module — `rqt_sonar_waterfall/CMakeLists.txt:20`
- [ ] (suggestion) Doc nits: `contact_georef.hpp` dimension axis order reversed; plugin comments say "executor thread" but `on_box_marked` runs on the GUI thread — `rqt_sonar_waterfall/include/rqt_sonar_waterfall/contact_georef.hpp:202`
- [ ] (governance, carried) Host to file cross-repo tracking issue: add `sonar_waterfall/contacts` to operator-station bag `record:` in `unh_echoboats_project11` (`gh` unauthenticated, out of worktree scope)
