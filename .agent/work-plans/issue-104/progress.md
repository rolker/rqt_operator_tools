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
