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
   - `AreaScore` dataclass built from a message: `area_id`, `name`, `standard_name`,
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
   - `Store`: latest per `area.id`. Replacement rule: a message replaces the held one
     if its `header.stamp` is newer or equal (equal: last received wins, mirroring the
     `SurveyArea` rule); an older stamp is dropped, which makes the replayed history
     (oldest first, repeats of an area) converge to the newest regardless of order.
     Records wall-clock receipt time per area. `forget(area_id)` and `clear()`.
   - Global roll-up over a chosen set of areas: pooled, area-weighted
     (`sum covered / sum total`, `sum meeting / sum total`), so a big area counts more
     than a small one; areas with total 0 are skipped. Per-area is shown too.
   - Combined score = `w_spec * pct_to_spec + w_covered_below * (pct_covered -
     pct_to_spec)`, as a percent of the area (defaults 1.0 and 0.5, per the issue:
     to-spec counts fully, covered-below-spec half). Weights are validated to finite,
     0..1, else the default; applied to the pooled and the per-area numbers alike.
   - Staleness: `age = now_wall - received_wall`; state `FRESH` / `STALE` (age >
     `stale_timeout`, default 5 s; 0 disables) / `NEVER`. Receipt time, not
     `header.stamp`, because the stamp is assessment time on whatever clock the scorer
     runs (sim time in the demo) and a wall-clock comparison against it would show
     false staleness; the detail view still prints the stamp as "last assessment" in
     the message's own time. Assumes the scorer republishes each pass (it does, every
     `assessment_period`, default 1 s); a note in the README says a scorer that only
     publishes on change would read as stale.
   - `CoverageConfig` dataclass with YAML round-trip (`to_yaml` / `from_yaml`, unknown
     keys ignored, bad values fall back to defaults): `topic` (default
     `coverage_assessment`), `view` (`glance` | `detail`), `selected_areas` (list of
     ids; empty = all), `font_scale` (0.5..4.0, default 1.0), `show_combined` (bool,
     default false), `weight_spec`, `weight_covered`, `stale_timeout`, `drop_stale_after`
     (see 3).
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
     third big number when `show_combined`. A stale tile greys out and shows "STALE
     12 s" instead of hiding the last number, so a dead scorer is never mistaken for a
     steady score. A `None` ratio renders "--".
   - **Detail view** — per area: name/id, standard in force (`name`, a, b), cell size
     and cell counts (total / covered / meeting), the two percentages, the combined
     score if enabled, last assessment stamp, age since receipt with the staleness
     indicator, and a per-source table (source, covered %, to-spec %, unique %, each of
     the area total, plus m^2 and a "clamped" marker when the sanitiser changed
     anything). Sources keyed by the `source` string, never by index; a source that
     vanishes between messages simply drops out of the table.
   - A "Forget area" action (context menu on a tile/row) calls `Store.forget`, because
     areas never disappear from a latched topic and a removed area would otherwise sit
     there forever; plus "Clear all". After a scorer reset the old replay is still in
     the latch only until it is overwritten, so the README documents Clear.
3. **`coverage_plugin.py`** — rqt `Plugin` wrapper like `BoatStatePlugin`: persist the
   config via `instance_settings.set_value('config_yaml', ...)`, restore with a
   guarded try/except that logs a warning and keeps defaults, `shutdown_plugin` calls
   `widget.shutdown()`, `trigger_configuration` opens `config_dialog.py`.
4. **`config_dialog.py`** — view selector, topic, area list (checkable, filled from
   the areas seen so far, plus a free-text id box so an area not yet received can be
   preselected), font scale spin box, combined-score checkbox and two weight spin
   boxes, stale timeout. Also a toolbar-style quick toggle (view) on the widget itself
   to follow the repo's perspective-persisted toolbar-setting convention; the view is
   saved in the same config.
5. **`coverage_standalone.py`** + console script `coverage = ...` for a windowed
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
  `area.id`; an out-of-order older stamp is dropped; equal stamp last-wins; forget and
  clear; two areas coexisting; a source list that changes shape between messages.
- `test_staleness.py`: fresh, stale at the boundary (not stale at exactly the timeout,
  as in `rqt_boat_state`), disabled timeout, never-received; uses an injected clock, and
  a message whose `header.stamp` is hours off wall clock is still FRESH (receipt-time
  rule).
- `test_config_model.py`: YAML round-trip, unknown keys ignored, malformed YAML and bad
  values fall back to defaults, view and font-scale clamping.
- `test_widget_smoke.py` (offscreen Qt, same pattern as `rqt_annunciator`'s
  `test_resize_invariants.py`, `QT_QPA_PLATFORM=offscreen` already set in CI): build the
  widget against a minimal stub node, feed fabricated messages through `_handle_message`
  (no ROS graph), check both views show the expected strings for 1 and 3 areas, stale
  styling appears after the timer tick with an injected clock, and the stub node records
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
| Human control and transparency | Stale tiles say STALE rather than freezing; clamped data is flagged; weights are visible settings |
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

- Global score: pooled area-weighted (planned) or mean of per-area percentages? Pooled is proposed because per-area means over-weight tiny areas.
- Combined score definition: planned as `w_spec*to_spec + w_covered*(covered - to_spec)` with 1.0 / 0.5; confirm that scale (percent of area, 0-100) is what the scoreboard wants.
- Default view: glance for the scoreboard, or detail? Planned default glance.
- Stale timeout default 5 s (scorer period default 1 s): acceptable?
- Package name `rqt_coverage` vs `rqt_survey_coverage`: planned the shorter name.
- Should the plugin offer a "Reset scorer" button (lifecycle calls)? Planned NO: out of scope, README points at `reset_cube_nodes.sh`.

## Estimated Scope

Single PR (about 7 modules, 5 test files).
