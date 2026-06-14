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
