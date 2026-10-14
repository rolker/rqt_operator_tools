# Plan: rqt plugin: survey-area coverage and quality from coverage_assessment

## Issue

https://github.com/rolker/rqt_operator_tools/issues/111

## Context

The `coverage_assessment` node (cube_bathymetry, cube#164) publishes one
`marine_interfaces/CoverageAssessment` per survey area, on `coverage_assessment`,
reliable + transient_local, depth 100. The message comments are the contract: areas
are m^2 on one raster; `area_total` can be 0; invariant `area_meeting_standard <=
area_covered <= area_total`; `sources[]` is stable-ordered, keyed by the `source`
string; `header.stamp` is assessment time, not receipt time. A late joiner gets up to
the last 100 assessments oldest first, so older repeats of an area arrive before the
newest. The repo has Python plugins (`rqt_boat_state`, `rqt_annunciator`) with a
Qt-free `config_model.py`, a widget that hands ROS callbacks to the Qt thread through
a `Signal`, a plugin wrapper that persists a YAML config in `instance_settings`, a
config dialog behind the rqt wrench, and a standalone entry point. This is scoreboard
item 6 of rolker/unh_marine_simulation#87 (scoring expected global).

## Approach

New ament_python package `rqt_coverage` in this repo, laid out like `rqt_boat_state`
(`package.xml`, `setup.py`, `setup.cfg`, `plugin.xml`, `resource/rqt_coverage`,
`test/`, `README.md`). Plugin class `rqt_coverage/CoveragePlugin`, group "Robot Tools",
label "Survey Coverage". Modules:

1. **`score_model.py` (Qt-free, ROS-free logic)** — everything the unit tests hit:
   - `AreaScore` dataclass built from a message: `area_id` (the title; `SurveyArea` has no name field), `standard_name`, `standard_a`, `standard_b`,
     `total`, `covered`, `meeting`, `stamp_ns`, `sources` (list of `(source, covered,
     meeting, unique)`), `cell_size`. A `from_msg(msg)` that takes the message
     duck-typed.
   - Ratios: `pct_covered = covered/total`, `pct_to_spec = meeting/total` (fraction of
     the whole area, so it never exceeds pct_covered), both guarded: `total <= 0` or
     non-finite gives `None` ("n/a" in the UI, never a crash or a fake 0 %). Values are
     sanitised: non-finite or negative areas become 0, and the invariant is enforced by
     clamping (`meeting <= covered <= total`) with a `clamped` flag shown in the detail
     view, since a violated invariant means a scorer bug the operator should not read as
     a clean number.
   - Cell counts for the detail view = area / cell_size^2, rounded; `None` if cell_size
     is not positive.
   - `Store`: latest per `area.id`. Replacement rule: the last message to arrive
     replaces the held one: the topic is reliable from one scorer
     and the latched replay is oldest first, so arrival order is the truth, while
     `header.stamp` is on the scorer's clock and goes backwards on a sim restart (a stamp
     comparison, with or without a time window, froze the display on old data). The
     stamp is kept for display.
     Records wall-clock receipt time per area. `forget(area_id)` and `clear()`.
   - Global roll-up over a chosen set of areas: pooled, area-weighted
     (`sum covered / sum total`, `sum meeting / sum total`), so a big area counts more
     than a small one; areas with total 0 are skipped. Overlapping areas are possible
     (the scorer's `coverage_grid` handles overlap) and the message carries no overlap
     area, so a cell inside two areas is counted twice in the pool, and each area is
     judged against its own standard; the pooled tile labels itself "sum of areas" and
     the README states both caveats. Per-area is shown too.
   - Combined score = `w_spec * pct_to_spec + w_covered_below * (pct_covered -
     pct_to_spec)`, as a percent of the area (defaults 1.0 and 0.5, per the issue:
     to-spec counts fully, covered-below-spec half). Weights are validated to finite,
     0..1, else the default; applied to the pooled and the per-area numbers alike.
   - Staleness ("time since the last assessment arrived"): `age = now_wall - received_wall`;
     state `FRESH` / `AGING` (age > `aging_timeout`, default 60 s; 0 disables) / `NEVER`.
     Receipt time, not `header.stamp`, because the stamp is assessment time on whatever
     clock the scorer runs (sim time in the demo); the detail view still prints the stamp
     as "last assessment" in the message's own time. **The scorer publishes only when a
     grid or the water surface changed (`coverage_assessment_node.hpp` `assessAndPublish`
     is gated on `grids_changed_ || force_assessment_ || surface_changed`; the last
     assessment otherwise "stays published"), so a quiet interval is normal when boats
     are stopped or the survey is done.** The indicator therefore reads "no new
     assessment for 75 s" (amber, number kept visible), never "scorer dead", and the
     README says that silence cannot distinguish an idle survey from a dead scorer
     without a heartbeat (open question). Window: a 60 s default is a guess to tune on
     the water.
   - `CoverageConfig` dataclass with YAML round-trip (`to_yaml` / `from_yaml`, unknown
     keys ignored, bad values fall back to defaults): `topic` (default
     `coverage_assessment`), `view` (`glance` | `detail`), `selected_areas` (list of
     ids; empty = all), `font_scale` (0.5..4.0, default 1.0), `show_combined` (bool,
     default false), `weight_spec`, `weight_covered`, `aging_timeout`.

2. **`coverage_widget.py`** — `QWidget` owning the subscription. Subscribes with
   `QoSProfile(reliability=RELIABLE, durability=TRANSIENT_LOCAL, history=KEEP_LAST,
   depth=100)` (the AGENTS.md "sensor streams are best_effort" note does not apply:
   this topic is reliable and latched, and a volatile subscriber would not receive the
   latched areas). The callback only emits `_msg_received = Signal(object)`; all store
   and widget access is on the Qt thread (queued signal), as in `rqt_boat_state`. A
   `QTimer` (1 s) refreshes ages/staleness. `load_config` tears down and recreates the
   subscription when the topic changed; a message queued before teardown is dropped by
   a generation check. Widgets:
   - **Glance view** — header line with the area name, two big numbers ("covered %" and
     "to standard %"), each font-scaled by `font_scale` and by widget size (readable
     across a room); with more than one selected area, one tile per area plus a pooled
     "All areas" tile first (when more than one area is shown). The combined score is a
     third big number when `show_combined`. An aging tile dims and shows "no update 75 s" instead of hiding the last number, so an old number is never mistaken for a live one.
     A `None` ratio renders "--".
   - **Detail view** — per area: name/id, standard in force (`name`, a, b), cell size
     and cell counts (total / covered / meeting), the two percentages, the combined
     score if enabled, last assessment stamp, age since receipt with the staleness
     indicator, and a per-source table (source, covered %, to-spec %, unique %, each of
     the area total, plus m^2 and a "clamped" marker when the sanitiser changed
     anything). Sources keyed by the `source` string, never by index; a source that
     vanishes between messages simply drops out of the table.
   - An "Areas" toolbar button (menu: "Forget area X", "Clear all areas"; works in both views) calls `Store.forget`, because
     areas never disappear from a latched topic and a removed area would otherwise sit
     there forever; After a scorer reset the old replay is still in
     the latch only until it is overwritten, so the README documents Clear.
3. **`coverage_plugin.py`** — rqt `Plugin` wrapper like `BoatStatePlugin`: persist the
   config via `instance_settings.set_value('config_yaml', ...)`, restore with a
   guarded try/except that logs a warning and keeps defaults, `shutdown_plugin` calls
   `widget.shutdown()`, `trigger_configuration` opens `config_dialog.py`.
4. **`config_dialog.py`** — view selector, topic, area list (checkable, filled from
   the areas seen so far, plus a free-text id box so an area not yet received can be
   preselected), font scale spin box, combined-score checkbox and two weight spin
   boxes, aging window. Also a small toolbar on the widget itself (view toggle, Areas menu) following the
   repo's perspective-persisted toolbar-setting convention; the view is saved in the same
   config.
5. **`coverage_standalone.py`** + console script `survey_coverage = ...` (`coverage` would shadow the Python coverage tool's name) for a windowed
   scoreboard on a wall display (`--view`, `--topic`, `--config`, `--always-on-top`,
   same spin-via-QTimer pattern as the annunciator).
6. **Docs and registration** — package `README.md` (topic, QoS, settings table, the
   score definitions, the staleness rule, Clear/Forget), one line in the repo root
   `README.md` package list. `package.xml` execs: `python_qt_binding`, `rclpy`,
   `rqt_gui`, `rqt_gui_py`, `marine_interfaces`, `python3-yaml`; test deps
   `python3-pytest`, `ament_copyright`.

Tests (pytest, all runnable without a display unless noted; each fails if the behaviour
regresses):
- `test_score_model.py`: ratios incl. `total == 0` -> None; invariant clamping and the
  clamped flag; negative/NaN/inf areas; the pooled roll-up weights big areas more than
  small ones and skips zero-total ones; combined score with default and custom weights
  and with invalid weights falling back; cell counts.
- `test_store.py`: replay of 100 oldest-first repeats converges to the newest per
  `area.id`; a backwards stamp (sim restart) is accepted; last arrival wins; forget and
  clear; two areas coexisting; a source list that changes shape between messages.
- `test_aging.py`: fresh, aging past the boundary (not aging at exactly the timeout,
  as in `rqt_boat_state`), disabled timeout, never-received; uses an injected clock, and
  a message whose `header.stamp` is hours off wall clock is still FRESH (receipt-time
  rule).
- `test_config_model.py`: YAML round-trip, unknown keys ignored, malformed YAML and bad
  values fall back to defaults, view and font-scale clamping.
- `test_widget_smoke.py` (offscreen Qt, same pattern as `rqt_annunciator`'s
  `test_resize_invariants.py`, `QT_QPA_PLATFORM=offscreen` already set in CI): build the
  widget against a minimal stub node, feed fabricated messages through `_handle_message`
  (no ROS graph), check both views show the expected strings for 1 and 3 areas, the aging
  indicator appears after the timer tick with an injected clock, and the stub node records
  a subscription with exactly the reliable / transient_local / depth-100 QoS.
- Messages are fabricated with the real `marine_interfaces` types where importable,
  else duck-typed namespaces, so the logic tests do not need the message package built.

## Files to Change

| File | Change |
|------|--------|
| `rqt_coverage/package.xml`, `setup.py`, `setup.cfg`, `plugin.xml`, `resource/rqt_coverage` | New package scaffolding, copied from `rqt_boat_state` conventions |
| `rqt_coverage/rqt_coverage/score_model.py` | Qt-free scores, store, staleness, config |
| `rqt_coverage/rqt_coverage/coverage_widget.py` | Subscription, glance + detail views, refresh timer |
| `rqt_coverage/rqt_coverage/coverage_plugin.py`, `config_dialog.py`, `coverage_standalone.py` | rqt wrapper, settings dialog, standalone entry |
| `rqt_coverage/test/test_*.py` | Tests listed above |
| `rqt_coverage/README.md` | Verified topic/QoS/settings/score definitions |
| `README.md` | Add `rqt_coverage` to the package list |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Test what breaks | Tests target replay ordering, divide-by-zero, invariant violations, stale data and QoS, the failure modes that matter on the water |
| Only what's needed | Python, no custom painting library; combined score, forget action and standalone are in the issue or needed for the latched topic |
| A change includes its consequences | README for package and repo list in the same PR |
| Human control and transparency | Aging tiles show the time since the last assessment rather than looking live; clamped data is flagged; weights are visible settings |
| Workspace vs. project separation | Nothing project-specific in the workspace repo; no BizzyBoat names in the package |
| Improve incrementally | One package, one PR |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0008 ROS 2 conventions | Yes | ament_python package, BSD-3-Clause, package.xml format 3, plugin.xml export |
| 0009 Python package policy | Yes | Only apt/rosdep deps (`python3-yaml`); no pip |
| 0013 progress.md vocabulary | Yes | Entries appended per phase |
| 0018 local CI | Yes | Full-scope `ci_local.sh` attestation before merge |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| New package in repo | Root README package list, repo CI picks it up by colcon | Yes |
| New ROS package | ui_ws rebuild on the operator station after merge | Note in PR |
| Settings (new plugin) | Package README settings table, persisted via perspective | Yes |

## Documentation & Instruction Impact

- **Stale docs** (must land in this PR): root `README.md` package list gets the new plugin; the new package README is written from the message comments and code, not from memory.
- **Agent-instruction candidates** (proposals only): the repo has no `.agents/README.md`; a deep guide is worth its own issue, not this PR. Candidate pitfall for it: latched topics need keep-latest-by-stamp and a forget action.

## Open Questions

Resolved by the owner (all as planned): global score is pooled, area-weighted; combined
score 1.0 (to spec) and 0.5 (covered below spec), percent of area; default view glance;
aging window default 60 s; package name `rqt_coverage`; no reset button (README points at
`reset_cube_nodes.sh`). Scorer heartbeat is filed separately as
https://github.com/rolker/cube_bathymetry/issues/169 and is not part of this PR.

Review suggestions adopted: modules and public classes carry docstrings; tiles and rows
title themselves with `area.id`, because `SurveyArea` has no name field.

## Estimated Scope

Single PR (about 7 modules, 5 test files).

## Implementation Notes

- Arrival order replaced the stamp comparison in `Store` (and the interim replay-window rule): sim-time restarts make the stamp go backwards while the live scorer is fine.
- `AGING` replaced `STALE` in all names and strings: the scorer publishes only on change, so silence is not proof of a fault.
- The widget takes an injectable clock (monotonic seconds) so aging is tested without sleeping.
- Lint: `ament_flake8`/`ament_pep257` are not part of this repo's test run (`rqt_boat_state` fails the strict import-order and D213 rules too); docstrings are complete, import order is not rearranged.
- Review-code round 1 changes: topic change clears the store and a failed subscribe shows a retryable notice; the pooled tile and heading take the oldest area's status; redraws are coalesced to one per event-loop turn; a rendering error is shown and logged, not raised; source coverage is clamped to the assessment's; cell counts survive extreme cell sizes; area ids render as plain text; status reads "received N s ago" because a late joiner's first picture is a latched assessment of unknown age; the combined column is a widget so hiding it frees its width.
- Review-code round 2 changes: `selected_areas` is de-duplicated; glance tiles have a minimum height and the panel scrolls; unique credit summing above the covered area is flagged; the notice line is plain text and reports a QoS mismatch (subscription event callback, queued to the Qt thread); the standalone drains ready callbacks for 3 ms per 33 ms tick instead of one; the aging spin box keeps one decimal.
