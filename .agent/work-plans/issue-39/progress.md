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
