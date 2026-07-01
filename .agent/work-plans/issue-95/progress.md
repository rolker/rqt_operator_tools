---
issue: 95
---

# Issue #95 — marine_control_widgets: one-sided (DBL_MAX/inf) bounds render as garbage range

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-30 21:05 +00:00
**By**: Claude Code Agent (Claude Opus 4.8)
**Verdict**: approved

**Branch**: feature/issue-95 at `e9e1f09`
**Mode**: pre-push
**Depth**: Standard (reason: medium single-package UI change, ~209 lines)
**Must-fix**: 0 | **Suggestions**: 1
**Round**: 1 | **Ship**: recommended — clean fix at the correct (consumer) layer, honors the existing sentinel contract, edge-case tests pass, no must-fix.

### Findings
- [ ] (suggestion) INT open-bound detection keys off the double max_value before toIntBound; an INT device using INT_MAX (~2.1e9) as its "unbounded" sentinel is below the 1e300 threshold and still renders 2147483647. Out of scope for #95 (float DBL_MAX case); INT_MAX is a defensible literal bound. — `marine_control_widgets/src/control_set_widget.cpp` (INT spinbox path)
