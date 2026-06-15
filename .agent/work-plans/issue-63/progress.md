---
issue: 63
---

# Issue #63 — rqt_marine_sonar: render the echogram on the GPU colormap stack (fix full-recompute hang)

## Plan Authored
**Status**: complete
**When**: 2026-06-14 13:05 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Plan**: `.agent/work-plans/issue-63/plan.md` at `df436b9`
**PR**: https://github.com/rolker/rqt_operator_tools/pull/64 (`[PLAN]` prefix)
**Phases**: single

### Open questions
- [ ] Depth axis becomes a QPainter overlay (loses QtCharts QValueAxis) — required to put the echogram on the GL stack; confirm acceptable.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-15 09:30 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-63 at `36be841`
**Mode**: pre-push
**Depth**: Standard (reason: GPU render rewrite + threading, one package)
**Must-fix**: 1 | **Suggestions**: 2

### Findings
- [x] (must-fix) resizeGL used logical height() not device-px h for glViewport → HiDPI shrink — `src/echogram_widget.cpp`
- [x] (suggestion) dead sub-1.0 zoom-out band → floor depth_zoom_ at 1.0 — `src/echogram_widget.cpp`
- [x] (suggestion) texture height vs span off-by-up-to-a-bin → snap overlay max to row grid — `src/echogram_widget.cpp`

### False positives / notes
- (Lens B) mouseMoved(QPointF) signal has no consumer — kept as widget API (was present pre-rewrite); harmless.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-15 11:55 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved
**Scope**: control rework (auto-range + normalized black/white window, gain dropped)

**Branch**: feature/issue-63 at `e514ba9`
**Must-fix**: 1 | **Suggestions**: 0

### Findings
- [x] (must-fix) all-NaN dropout ping (valid geometry) dragged the auto-range extent toward 0 → inverted-sentinel skip in dataExtent — `src/echogram_widget.cpp`

### Verified clean
- No dangling refs to the removed min/max/gain API across widget/plugin/.ui/test.
- valueWindow/dataExtent edge cases: empty buffer → {0,1}; black>=white / zero-span → not drawn (no NaN to shader); freeze-on-toggle captures the extent; old saved layouts default to auto-range on.
