---
issue: 46
---

# Issue #46 — rqt_sonar_waterfall: manual intensity-range default (32767) under-ranges 16-bit (UINT16) data

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-07 17:47 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-46 at `4a2a242`
**Mode**: pre-push
**Depth**: Standard (reason: C++ logic change with executor/GUI-thread interaction)
**Must-fix**: 1 | **Suggestions**: 2

### Findings
- [x] (must-fix, cross-confirmed: Claude + Copilot) Stale-source seed on rapid topic switch — an in-flight callback from a replaced subscription could win the seed and set the manual default from the wrong source — `src/sonar_waterfall_plugin.cpp`. Fixed with a monotonic per-side subscription id stamped into each callback; mismatched (stale) callbacks are dropped before seeding.
- [x] (suggestion, Claude + Copilot) `dtype` narrowed uint32→uint8; an out-of-range value could alias a valid case. Widened `default_full_scale`/`maybe_seed_manual_range` params to `uint32_t` to match `SonarImageData::dtype`.
- [x] (suggestion, Claude) Re-seed didn't fire when a side switched to (none). Centralized the seed reset in `subscribe()` (runs for both create and clear), so it now always re-seeds for the new configuration.

### False positives
- None.

### Notes
- Static analysis (ament_cpplint) clean on all changed files. 205 gtests pass (5 new DefaultFullScale cases).
- The subscription-id staleness path is Qt/executor-threaded (like `post_row`) and is not unit-tested; the dtype→full-scale mapping is covered by the new gtests.
