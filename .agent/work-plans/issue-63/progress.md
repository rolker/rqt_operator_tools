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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-20 20:00 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved
**Scope**: integrate current jazzy (incl. #59 ring-texture GpuColorMap, #77) + re-verify

**Branch**: feature/issue-63 (merge `4cb55c9`)

### Findings
- None. Clean merge of origin/jazzy (17 commits, no conflicts). The echogram builds
  against the merged ring-texture `GpuColorMap` (additive API) and all gtests pass
  (rqt_marine_sonar: 61 checks, 0 failures; 8 uncrustify lint skips per the known
  local 0.78.1 drift). The GPU port (QOpenGLWidget + GpuColorMap) + control rework
  (auto-range/normalized window) carried in unchanged.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-20 21:30 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: changes-requested
**Scope**: re-review post jazzy-merge GPU echogram port, focused on #59 ring-texture GpuColorMap integration

**Branch**: feature/issue-63 at `a748cb3`
**Mode**: pre-push
**Depth**: Deep (reason: GPU render path + cross-package API integration)
**Must-fix**: 1 | **Suggestions**: 1
**Round**: 4 | **Ship**: continue — a correctness regression from the jazzy merge (depth axis collapses) that gtests do not catch; warrants a fix + re-review.

### Findings
- [ ] (must-fix) Echogram never calls `GpuColorMap::set_ring()`, but the #59-merged `draw()` shader now *always* applies the ring-buffer V mapping. With the default ring uniforms `(oldest=0, filled=1, capacity=1)` the shader computes `ridx=0 → trow=0 → vy=0.5` for every fragment, so the whole echogram samples a single depth row (texture-V 0.5) smeared vertically — the entire depth axis collapses. The widget uploads a plain `cols × depth_rows` texture and was written against the pre-#59 plain `texture(u_intensity, v_uv)` mapping (confirmed at `4cd9710^`). Fix: call `gpu_.set_ring(0, depth_rows, depth_rows)` (identity, non-wrapping) before `gpu_.draw()` in `paintGL()`, OR cache `depth_rows` from `uploadTexture()` and feed it. `src/echogram_widget.cpp:411-414` (`paintGL`); semantic-drift source is `rqt_sonar_waterfall/src/gpu_color_map.cpp:135-139`.
- [ ] (suggestion) No render test asserts depth varies down the column — the `countAmber > 0` checks pass even with the collapse above, which is why the round-3 review missed it. Add a test that asserts the rendered image differs between two depth bands (e.g. a ping with a bright shallow half and dark deep half must show a brightness gradient top-to-bottom). `test/test_echogram_widget.cpp`.

### Verified clean
- GL lifecycle: `intensity_tex_` created once (guarded), deleted in dtor with context current; `gpu_.cleanup()` called in dtor with context current — no leak/UAF.
- Threading: `dataCallback` (executor) enqueues under mutex; `newPings` (GUI timer) swaps under lock then ingests unlocked; `shutdownPlugin` stops timer + resets sub + clears queue under lock; `QPointer` guards teardown. No lost/duplicated pings, no GUI-thread races.
- Coalescing timer (50 ms / ~20 Hz) + swap-and-drain is correct; one redraw per burst.
- value-window / auto-range math: empty buffer → {0,1}; all-NaN ping skipped via inverted sentinel; zero-span / black≥white → `window_set` false → not drawn (no NaN/degenerate range to shader). Carried correctly from rounds 1–2.
- Texture upload re-specs (not leaks) the same texture name each dirty frame; `glGetError` clear-before / check-after with once-only warn.
