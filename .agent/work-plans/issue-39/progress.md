---
issue: 39
---

# Issue #39 — rqt_sonar_waterfall: rqt waterfall viewer for RawSonarImage (sidescan) with optional controls

## Plan Authored
**Status**: complete
**When**: 2026-06-05 (local)
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-39/plan.md` at `1af8581`
**PR**: https://github.com/rolker/rqt_operator_tools/pull/41 (`[PLAN]` prefix)
**Phases**: single (multibeam extractor split to #40)

### Open questions
- [ ] Color map: adapt `rviz_sonar_image`'s `color_map` into this package (default) vs factor into a shared lib.
- [x] Sonar controls — RESOLVED: garmin uses `marine_radar_control_msgs`; reuse `rqt_marine_radar`'s dynamic `RadarControlSet`/`RadarControlValue` panel, no per-control topic config.

## Integrated Review
**Status**: complete
**When**: 2026-06-07 09:39 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #41 at `45fc70b` (reviewed); local/gitcloud ahead at `5db384f` — branch not yet pushed to PR
**Sources**: 1 (Copilot review @ `45fc70b`); progress.md had no prior review entries
**Cross-source confirmations**: 0
**CI**: copilot-pull-request-reviewer success; no build/test gate configured on the PR

### Findings
- [ ] (functional bug, Copilot) `set_manual_range()` never clears `auto_range_`, so `rebuild_image()` keeps using auto-range — unchecking "Auto range" leaves manual Min/Max with no effect — `src/waterfall_widget.cpp:100`
- [ ] (minor, Copilot) Py2-style `#!/usr/bin/env python` shebang; ROS 2 only guarantees python3 — `scripts/rqt_sonar_waterfall:1`
- [ ] (minor, Copilot) `create_timer(1.0/rate)` unguarded; `rate<=0` crashes the synthetic publisher at startup — `scripts/synthetic_sidescan.py:76`
- [ ] (doc, Copilot) README status block still calls the package a "scaffold ... empty canvas"; data path, view knobs, and control panel are all implemented — `README.md:7-10`
- [ ] (process) Push `feature/issue-39` to advance PR `45fc70b`→`5db384f`; clears the two stale findings below and re-runs review on real head

### False positives
- (Copilot) README/package.xml control panel "not implemented" / `marine_radar_control_msgs` "unused" (2 comments) — implemented by `a4023eb` (control_panel.cpp/hpp + RadarControlSet/Value sub/pub + tests); stale only because the branch wasn't pushed to the PR
- (Copilot) `post_row()` "guard target before invokeMethod" — guard already present: `widget_` is `QPointer` (auto-nulls), `post_row` early-returns on `!widget_`, and the queued lambda re-checks `if (target)` before `add_row`; a null context to the functor+QueuedConnection overload is a benign no-op

## Integrated Review
**Status**: complete
**When**: 2026-06-07 10:19 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #41 at `91a20c4`
**Sources**: 2 at head (Copilot R2 @ `91a20c4`, rolker conversation comment) + prior Integrated Review R1 @ `45fc70b`
**Cross-source confirmations**: 1
**CI**: copilot-pull-request-reviewer success; no build/test gate on PR — package has GTest tests, run colcon test locally

R1 resolved (manual-range clears auto_range_ at waterfall_widget.cpp:104, python3 shebang, synthetic rate guard; R1 post_row FP confirmed guarded at plugin.cpp:507). R2 reviews newer code (control panel, row extractor, topic filter, render path).

### Findings
- [x] (cross-confirmed: Copilot R2 + rolker conversation) `add_row()` -> `rebuild_image()` recolors every buffered row x full width on every ping (O(h*w)/ping, h up to history cap 600/5000); matches rolker's observed slow-down/lock-up as the buffer fills. Fix = incremental render (shift cached QImage, draw only new row; full rebuild only on setting/width/range change; handle auto-range recompute + variable width) — `src/waterfall_widget.cpp:50,110`
- [x] (low-med, Copilot R2) control inputs not initialized from item.value: FLOAT/FLOAT_WITH_AUTO QLineEdit blank, ENUM QComboBox defaults to enum[0] — contradicts actual control state (value IS shown in adjacent value label; validator + activated-on-user-action prevent spurious empty publish, so correctness/UX not data loss) — `src/control_panel.cpp:76,99,111`
- [x] (low, Copilot R2) range_max uses declared samples_per_beam even when fewer decoded (truncated data) -> overstated slant range; use min(declared, decoded) — `src/row_extractor.cpp:60`
- [x] (low, Copilot R2) derive_change_topic() strips trailing "state" substring not "/state" segment (/sonar/estate -> /sonar/echange_state); match final /state segment — `src/topic_filter.cpp:77`

### False positives
- none this round (R1 post_row "guard target" FP not re-raised; guard present at plugin.cpp:507)

### Resolution (fixes applied)
**When**: 2026-06-07 10:32 -04:00 · **By**: Claude Code Agent (Claude Opus 4.8 (1M context))
- **Render pipeline reworked (cross-confirmed must-fix):** `add_row()` now takes an
  O(width) incremental path — scroll the cached `QImage` down one row (memmove at
  capacity, memcpy during growth) and paint only the new top row via a shared
  `paint_row()`; `rebuild_image()` (full recolor) runs only on first row, view-setting
  change, width growth, history change, or an auto-range change.
- **Auto-range stays exact and cheap (per Roland):** each row caches its own
  min/max at buffer entry (`WaterfallRow.min_intensity/max_intensity`), so the
  buffer-wide `auto_range()` reads two numbers per row — O(rows), not O(rows x
  samples). add_row recomputes the exact range each ping; any change (brighter ping
  OR an extreme scrolling off) triggers one full recolor, so the range both expands
  and contracts correctly. No sticky-gain approximation.
- **Control inputs seeded from `item.value`:** FLOAT/FLOAT_WITH_AUTO `QLineEdit`
  (skipping "auto"), ENUM `QComboBox` via `setCurrentText` (no `activated()` -> no
  spurious publish).
- **range_max clamped** to `min(declared samples_per_beam, decoded count)`.
- **derive_change_topic** matches a final `/state` segment, not any trailing
  "state" substring.
- Added 3 regression tests (truncated-range clamp, `/sonar/estate` segment,
  auto_range honors cached extremes). Widget smoke tests (5/0) and control-panel
  tests (5/0) confirm the render + input changes.
- `colcon test rqt_sonar_waterfall`: **199 tests, 0 failures** (27 skipped = linters).

## Integrated Review
**Status**: complete
**When**: 2026-06-07 15:40 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #41 at `8239735`
**Sources**: 1 at head (Copilot R3 @ `8239735`) + prior Integrated Reviews (R1 `45fc70b`, R2 `91a20c4`, resolved) + rolker conversation comment
**Cross-source confirmations**: 0
**CI**: copilot-pull-request-reviewer success (no build/test check on PR; package GTests pass locally)

R1/R2 findings all resolved in the prior entries. R3 reviews the cleaned-up control panel.

### Findings
- [ ] (suggestion, Copilot R3) ControlPanel::apply() refreshes only the value label, not the editable widget -> input column goes stale vs device state after first update. Refresh the input from item.value when it is NOT focused (don't clobber active edits) — `src/control_panel.cpp:128-136`

### False positives
- (Copilot R3) FLOAT_WITH_AUTO "could publish empty string on editingFinished" — `src/control_panel.cpp:81-99`: the QLineEdit has a QDoubleValidator; Qt suppresses editingFinished when content isn't Acceptable, and empty is Intermediate, so focus-out/Enter on empty does not emit. Blank-for-"auto" is intentional (auto lives on the button); the input-not-reflecting-state residual is subsumed by the apply() finding above.
- (rolker conversation) widget slowdown/lock-up — addressed in R2 (incremental render + O(rows) auto-range).
