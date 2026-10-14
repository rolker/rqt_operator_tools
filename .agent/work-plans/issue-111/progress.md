---
issue: 111
---

# Issue #111 — rqt plugin: survey-area coverage and quality from coverage_assessment

## Issue Review
**Status**: complete
**When**: 2026-10-14 12:27 -04:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Issue**: #111
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Actions
- [ ] Watch: issue says "per area" but umbrella unh_marine_simulation#87 expects a global score; plan must define the global roll-up (area-weighted sum over areas) alongside the per-area tiles.
- [ ] Watch: staleness must be judged by wall-clock receipt time, not header.stamp (assessment time; sim clock may differ from wall time). Plan must pick one and say why.
- [ ] Watch: latched history replays up to 100 older repeats oldest first; keep-latest must compare header.stamp (not arrival order) per area.id, and tolerate area_total == 0 (guard the divide) and violated invariants (clamp/flag, never crash).
- [ ] Watch: areas never disappear from a latched topic; after a scorer reset a removed area lingers. Plan needs a clear/forget control and a per-area staleness mark.
- [ ] Watch: sources order is stable per message but a source set can change between runs; key on source string, not index.
- [ ] Recommendation: no repo PRINCIPLES.md or .agents/README.md exists; plan should add the plugin to the root README and note the gap, not fill it here.

## Plan Authored
**Status**: complete
**When**: 2026-10-14 12:28 -04:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Plan**: `.agent/work-plans/issue-111/plan.md` at `ad878d2`
**Branch**: feature/issue-111 at `ad878d2`
**Phases**: single

### Open questions
- [ ] Global score: pooled area-weighted vs mean of per-area percentages
- [ ] Combined score scale and default weights (1.0 / 0.5)
- [ ] Default view (glance planned)
- [ ] Aging window default 60 s; scorer heartbeat wanted? (scorer publishes only on change)
- [ ] Package name rqt_coverage
- [ ] Reset-scorer button (planned no)

## Plan Review
**Status**: complete
**When**: 2026-10-14 12:28 -04:00
**By**: Claude Code Agent (Claude Sonnet 5.5) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-111/plan.md` at `6d2d69f`
Independence uncertain: this review ran in the same context that authored the plan, and no dispatch header was present.
**PR**: PR-less
**Verdict**: approve-with-suggestions (must-fix items folded into the plan at `6d2d69f`)

### Findings
- [x] (must-fix, folded) Staleness assumed the scorer republishes every period; `assessAndPublish` is gated on grids/surface change, so a stopped boat or finished survey would read as a dead scorer. Indicator redefined as "no new assessment for N s", default 60 s, README caveat, heartbeat raised as an open question — `plan.md` Approach 1
- [x] (must-fix, folded) Pooled score double-counts cells in overlapping areas and mixes standards; now stated in the plan and labelled "sum of areas" in the UI — `plan.md` Approach 1
- [ ] (suggestion) Python lint (flake8/pep257) runs inside colcon test per CI; keep modules docstring-complete — `plan.md` Approach
- [ ] (suggestion) Per-area selection by id when areas have no friendly name: show `area.id` as the title fallback — `plan.md` Approach 2

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-14 12:53 -04:00
**By**: Claude Code Agent (Claude Sonnet 5.5)
**Verdict**: changes-requested

**Branch**: feature/issue-111 at `9e14656`
**Mode**: pre-push
**Depth**: Deep (reason: 21 files, +2110 lines, new package)
**Must-fix**: 8 | **Suggestions**: 6
**Round**: 1 | **Ship**: continue — several correctness and stale-data must-fixes, one a layout defect

Specialists: static analysis (ament_flake8/pep257: style only, repo does not enforce), 2 Claude adversarial passes (Lens A, Lens B), cross-model gemini + codex (both ran). Cross-model findings corroborated Claude Lens A and B on items 1, 2, 4.

### Findings
- [x] (must-fix) `AreaScore.cells` raises ZeroDivisionError/OverflowError for extreme cell_size and the detail redraw repeats it every second (Lens B, gemini, codex) — `score_model.py:115`
- [x] (must-fix) Topic change keeps the old topic's areas on screen as live data (Lens A, Lens B, codex) — `coverage_widget.py:260`
- [x] (must-fix) A failing `create_subscription` leaves the panel looking live and a re-apply is skipped (Lens B, codex) — `coverage_widget.py:259`
- [x] (must-fix) Pooled glance tile and detail heading take their status from the newest area, hiding a stale area (Lens A, Lens B, gemini) — `coverage_widget.py:388`
- [x] (must-fix) Hidden combined column keeps a third of the row (gemini) — `coverage_widget.py:100`
- [x] (must-fix) Source covered clamped against area total, not against the assessment's covered as the message contract says (gemini) — `score_model.py:141`
- [x] (must-fix) Stamp comparison with a 2 s replay window freezes or overwrites with stale data in both directions (gemini) — `score_model.py:200`
- [x] (must-fix) A rendering exception escapes a Qt slot (Lens B) — `coverage_widget.py:469`
- [x] (suggestion) Detail setHtml resets the scroll position every second (gemini) — `coverage_widget.py:458` (to verify) (deferred: not reproduced; test_detail_scroll_position_survives_the_refresh_timer shows the scroll position is kept, so no change)
- [x] (suggestion) Late joiner's latched assessment shows as freshly updated (Lens A) — `coverage_widget.py:316` (deferred: true age is unknowable here; status now reads "received N s ago" and the README says so; marking the latched burst separately left for owner)
- [x] (suggestion) Pooled title silent about selected areas still waiting (Lens A, Lens B) — `coverage_widget.py:363`
- [x] (suggestion) QLabel rich-text detection mangles ids with markup; menu text mnemonic (Lens A, Lens B) — `coverage_widget.py:91`
- [x] (suggestion) Per-message full redraw during a 100-message replay (Lens B) — `coverage_widget.py:428`
- [x] (suggestion) Standalone: unreadable --config not reported, ros_args without program name, message class not re-imported after marine_interfaces is built (Lens B, gemini) — `coverage_standalone.py:79` (partly deferred: --config and ros_args fixed; re-importing marine_interfaces after startup left out, the plugin logs a warning and needs a restart)

## Implementation
**Status**: complete
**When**: 2026-10-14 12:53 -04:00
**By**: Claude Code Agent (Claude Sonnet 5.5)

**Branch**: feature/issue-111 at `98c8bbb..9a1660e`
**Addressed**: Local Review (Pre-Push) round 1
**Commits**: 98c8bbb, 62382e8, b1fddac, 9a1660e

### Actions
- [x] cell counts survive extreme cell sizes — `score_model.py:115`
- [x] topic change clears the old topic's areas — `coverage_widget.py:260`
- [x] failed subscribe shows a notice and is retried on re-apply — `coverage_widget.py:259`
- [x] roll-up status follows the oldest area (glance and detail) — `coverage_widget.py:388`
- [x] hidden combined column frees its width — `coverage_widget.py:100`
- [x] source covered clamped to the assessment's covered — `score_model.py:141`
- [x] store keeps the last arrival; stamp comparison removed — `score_model.py:200`
- [x] rendering errors contained and shown — `coverage_widget.py:469`
- [x] pooled title counts waiting areas, ids render as plain text, redraws coalesced, standalone reports bad --config and keeps program name
- [x] detail scroll reset — (deferred: not reproduced, test added)
- [x] late-joiner age — (deferred: unknowable; wording and README changed)
- [x] message class re-import after startup — (deferred: needs restart, warning is logged)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-14 13:01 -04:00
**By**: Claude Code Agent (Claude Sonnet 5.5)
**Verdict**: changes-requested

**Branch**: feature/issue-111 at `314c39a`
**Mode**: pre-push
**Depth**: Deep (reason: new package, 2100+ lines)
**Must-fix**: 3 | **Suggestions**: 6
**Round**: 2 | **Ship**: recommended — three mechanical must-fixes, down from eight, none a design question; fix and ship

Fresh Claude Lens A and Lens B plus gemini and codex re-read the diff cold. Claude found no must-fix; codex found the three below. Two gemini must-fixes dismissed: source meeting above the assessment's is legal per CoverageContribution.msg, and Forget with an explicit area selection leaving a waiting tile is the selection working.

### Findings
- [ ] (must-fix) Repeated ids in selected_areas are shown and counted twice in the roll-up (codex, gemini, Lens A) — `score_model.py:364`
- [ ] (must-fix) Twenty areas force the glance panel past 3000 px with no scrolling (codex) — `coverage_widget.py:475`
- [ ] (must-fix) Sum of sources' unique areas above the assessment's covered is not flagged (codex) — `score_model.py:144`
- [ ] (suggestion) Notice label is rich text (gemini, Lens B) — `coverage_widget.py:229`
- [ ] (suggestion) A QoS mismatch with the publisher looks like "waiting" (Lens B) — `coverage_widget.py:66`
- [ ] (suggestion) Standalone spin_once handles one callback per 33 ms tick (Lens A, Lens B) — `coverage_standalone.py:55`
- [ ] (suggestion) Standalone subscribes to the default topic before applying its config (gemini) — `coverage_standalone.py:45`
- [ ] (suggestion) Aging spin box rounds to whole seconds (Lens A) — `config_dialog.py:73`
- [ ] (suggestion) Source meeting above the assessment's flagged as inconsistent (gemini) — `score_model.py:143` (dismissed: legal per CoverageContribution.msg)
