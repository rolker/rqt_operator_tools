---
issue: 58
---

# Issue #58 — rqt_sonar_waterfall: water-column removal + slant→ground range + range lines + uniform ping scaling

## Plan Authored
**Status**: complete
**When**: 2026-06-14 11:18 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-58/plan.md` at `48b1c3c`
**PR**: https://github.com/rolker/rqt_operator_tools/pull/60 (`[PLAN]` prefix)
**Phases**: single

### Open questions
- [ ] Combined-row geometry: store `nadir_index` + per-side range in `WaterfallRow` (plan does); confirm port/stbd may carry asymmetric ranges.
- [ ] Range-line interval: auto-pick nice 1/2/5·10ⁿ from visible half-width (assumed) vs a fixed-interval control.

## Plan Review
**Status**: complete
**When**: 2026-06-14 11:32 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context)) (in-context — author self-review)

**Plan**: `.agent/work-plans/issue-58/plan.md` at `48b1c3c`
**PR**: https://github.com/rolker/rqt_operator_tools/pull/60
**Verdict**: approve-with-suggestions

### Findings
- [ ] (must-fix) Depth combo needs a `range_topics()` filter (`sensor_msgs/msg/Range`) in `topic_filter.{hpp,cpp}` + test — not in plan's Files table — `plan.md` Files to Change
- [ ] (must-fix) Depth subscription must use `SensorDataQoS`; driver publishes `~/nadir_depth` BEST_EFFORT (node.py:347,359) — reliable sub gets no altitude — `plan.md` Approach §1
- [ ] (suggestion) State single-executor-thread assumption (or guard) for the altitude cache written by depth cb / read at post_row — `plan.md` Approach §1

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-14 12:10 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-58 at `7d76565`
**Mode**: pre-push
**Depth**: Standard (reason: ~680 net lines, single UI package, threading + GL)
**Must-fix**: 0 | **Suggestions**: 4 (3 addressed, 1 noted)

### Findings
- [x] (suggestion) project_row per-row heap alloc → in-place project_row_into() into the texture buffer — `src/waterfall_model.cpp`
- [x] (suggestion) ground-mode far column blacked out by float rounding → tolerance + frac clamp — `src/waterfall_model.cpp:sample_on_side`
- [x] (suggestion) dead altitude logic in combine_rows (superseded by post_row stamp) → removed + documented — `src/waterfall_model.cpp`
- [ ] (suggestion, wontfix) apply_tvg bin-center vs project_row endpoint slant differ by half a bin — intentional (amplitude vs index mapping, both monotonic); documented in code
