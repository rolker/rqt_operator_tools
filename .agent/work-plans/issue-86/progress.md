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
