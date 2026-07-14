---
issue: 104
---

# Issue #104 — rqt_marine_control: scroll tall device tabs instead of growing the plugin

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-14 19:04 +0000
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-104 at `6a6b102`
**Mode**: pre-push
**Depth**: Standard (reason: 5 files / 88 changed lines — 4–9 files & 50–199 lines band)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 1 | **Ship**: recommended — no must-fix; clean static analysis and two clean adversarial passes

### Findings
- [ ] (suggestion) Comment says "re-select by widget" but reselect now goes through `tabIndexFor(topic)` — `rqt_marine_control/src/responsive_hub_layout.cpp:121`
- [ ] (suggestion) `widgetFor()` is now unused by production code and tests — drop it or document why it's retained — `rqt_marine_control/include/rqt_marine_control/tab_manager.hpp:108`

## Integrated Review
**Status**: complete
**When**: 2026-07-14 15:46 -04:00
**By**: Claude Code Agent (Claude Fable 5)

**PR**: #105 at `6e229e5`
**Sources**: 3 (Copilot R1, Local Review (Pre-Push) R1 approved/Ship: recommended, CI build-and-test)
**Cross-source confirmations**: 0 (both sources clean)
**CI**: all-pass

### Findings
- [x] (suggestion, local) stale 're-select by widget' comment — fixed (`6e229e5`) — `rqt_marine_control/src/responsive_hub_layout.cpp`
- [x] (suggestion, local) orphaned widgetFor() accessor — removed, 93 tests green (`6e229e5`) — `rqt_marine_control/src/tab_manager.cpp`

### False positives
(none)
