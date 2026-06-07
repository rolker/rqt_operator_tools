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
