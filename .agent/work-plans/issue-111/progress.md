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
