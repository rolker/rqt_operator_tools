---
issue: 48
---

# Issue #48 — rqt_sonar_waterfall GPU render path

## Implementation (stage 1 — GpuColorMap + parity test)
**Status**: complete (pending review)
**When**: 2026-06-07
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Branch**: feature/issue-48

Foundation for the GPU waterfall: a reusable GL colormap renderer + the
offscreen-GL CPU/GPU parity test (the validation deferred from
marine_colormap#5). Does **not** touch the shipping CPU widget — the
QOpenGLWidget swap is stage 2 (see plan.md).

- `gpu_color_map.hpp/.cpp` — `GpuColorMap`: compiles a fragment shader assembled
  from `marine_colormap::colormap_glsl()` (desktop `#version 330 core` preamble +
  samplers/uniforms + `main()`), uploads the palette as an identity-transfer 1-D
  LUT (`bake_lut`), and draws a viewport quad sampling an `R32F` intensity
  texture. min/max/gain/contrast are uniforms (free re-ranging); the LUT
  re-bakes only on palette change. Owns no framebuffer (widget binds its default
  FBO; the test binds an offscreen FBO). LUT coord is remapped to texel centers
  so `GL_LINEAR` reconstructs the same piecewise-linear palette the CPU samples.
- `test/test_gpu_color_map.cpp` — `QOffscreenSurface` + `QOpenGLContext` 3.3 core;
  renders an intensity ramp and asserts each pixel matches the CPU path
  (`scale_intensity()` + `ColorMap::lookup()`). **Self-skips** (GTEST_SKIP) if no
  GL context can be created. 5 cases: grayscale (tol 2), thermal/bronze (tol 4,
  for LUT interpolation near stops), normalize-over-data-range, gain+contrast.
- CMake: `Qt5 Gui/OpenGL`; new sources + test (`QT_QPA_PLATFORM=offscreen` +
  `LIBGL_ALWAYS_SOFTWARE=1`). `package.xml`: `libqt5opengl5-dev`.

**Verified**: `colcon test` — 218 tests, 0 failures, 30 skipped. The 5 parity
tests **executed and passed** on this box (Mesa swrast offscreen GL), confirming
real CPU/GPU agreement of the marine_colormap#5 shader.

### Dependency / merge order
- Needs `marine_colormap/shader.hpp` (`colormap_glsl()`) from marine_colormap
  PR #6. During dev the worktree's `marine_colormap` symlink is repointed at the
  issue-5 worktree. **Do not merge before #6**; PR CI will fail until #6 lands.

### Deferred to stage 2
- `WaterfallWidget : QOpenGLWidget` (R32F scrolling ring texture, uniforms for
  range/gain/contrast, label overlay); rework `test_waterfall_widget` to
  `grabFramebuffer()`. #48 stays open until stage 2 lands.

## Implementation (stage 2 — QOpenGLWidget swap)
**Status**: complete (pending review + on-screen verification)
**When**: 2026-06-07
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Branch**: feature/issue-48 (builds on stage 1, same PR #49)

`WaterfallWidget` is now a `QOpenGLWidget` rendering on the GPU via `GpuColorMap`.
Same public API (`add_row`/`set_*`) so `control_panel`/`sonar_waterfall_plugin`
are untouched.

- Raw (full-precision) intensities → R32F texture; colormap in the fragment
  shader. Range/gain/contrast are uniforms, palette is the baked LUT, so changing
  any of them is a redraw — **no CPU recolor**, and data is not quantized to 8-bit
  (the original ask). The old CPU recolor / `rebuild_image` / `memmove`
  incremental path is gone.
- **Texture upload = re-upload whole buffer** on change (`data_dirty_` flag →
  `paintGL`, context current). Each row nearest-resampled to `W = max width`,
  stored oldest-first so screen-top = newest with no V flip. The CPU bottleneck
  was the per-pixel recolor, not the upload (~KB/ping), so this is already a big
  win; ring-buffer `glTexSubImage2D` is a noted follow-up.
- Auto-range from the per-row cached extremes (PR #41) feeds the min/max uniforms.
- 3.3 **compatibility** context so the QPainter label/placeholder overlay coexists
  with the modern shader path. Dark `glClear` placeholder + "No sonar data" /
  range-`m` labels via QPainter after the GL draw.
- `test_waterfall_widget` reworked to `grabFramebuffer()` behind a
  GL-availability skip (same pattern as `test_gpu_color_map`); the content test
  now asserts the left-dark→right-bright grayscale gradient (verifies the GL
  render, not the label overlay).

**Verified**: `colcon test` — 218 tests, 0 failures, 30 skipped. The 5 widget
tests executed (real GL render + framebuffer readback) on Mesa swrast.
On-screen verification against the Garmin sidescan bag (`bag_2026-06-05T14.07.32_sidescan_raw`, 25k pings) **PASSED** 2026-06-07 — Roland confirmed it renders correctly. PR #49 taken out of draft.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-07 19:55 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: changes-requested

**Branch**: feature/issue-48 at `da7782b` (PR #49)
**Mode**: pre-push
**Depth**: Deep (new GL rendering subsystem, ~1k lines, GL lifecycle/context)
**Static analysis**: clean (cpplint/cppcheck/uncrustify/copyright via colcon test) | **Adversarial**: Claude + Copilot
**Must-fix**: 4 | **Suggestions**: 4

### Findings
- [ ] (must-fix, cross-confirmed Claude+Copilot) GL objects (LUT/program/VBO/VAO) destroyed with no current context: GpuColorMap member dtor runs after widget dtor's doneCurrent() -> leak/wrong-context delete; add gpu_.cleanup() between makeCurrent/doneCurrent or hook aboutToBeDestroyed — `src/gpu_color_map.cpp:80`,`src/waterfall_widget.cpp:57-68`
- [ ] (must-fix, cross-confirmed Claude+Copilot) shader compile/link failure self-skips instead of FAIL — skip only when no context creatable; FAIL if context exists but initialize() fails — `test/test_gpu_color_map.cpp:195-198`
- [ ] (must-fix, Copilot) no GL_MAX_TEXTURE_SIZE guard / no glTexImage2D failure check; set_history unbounded + unbounded row width -> silent broken render with has_data_=true — `src/waterfall_widget.cpp:185-219`
- [ ] (must-fix, Claude) unguarded makeCurrent() in dtor if widget never shown / context gone; guard if(context() && context()->isValid()) — `src/waterfall_widget.cpp:62`
- [ ] (suggestion, both) NaN/Inf intensity -> shader clamp(NaN) undefined (no sentinel in main()); pre-existing, add isnan guard + test — `src/gpu_color_map.cpp:110`
- [ ] (suggestion, Claude) document add_row as GUI-thread-only (relies on plugin Qt::QueuedConnection) — `include/rqt_sonar_waterfall/waterfall_widget.hpp:67`
- [ ] (suggestion, both) test coverage: assert newest-at-top V-orientation, single/zero-width row, oversized texture — `test/`
- [ ] (suggestion, Claude) per-frame vector alloc + full glTexImage2D realloc + tex-param re-set each dirty frame (documented re-upload tradeoff; ring-buffer is the tracked follow-up) — `src/waterfall_widget.cpp:196,217`

### Verified correct (both reviewers)
- LUT texel-center remap (t*(N-1)+0.5)/N matches bake_lut entry i=sample(i/(N-1)) under GL_LINEAR; V-orientation newest-at-top (oldest at row 0, no flip); resample lround index with divide guards; #version 330 core w/o precision; CompatibilityProfile for QPainter overlay coexistence.

## Integrated Review
**Status**: complete
**When**: 2026-06-07 19:58 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #49 at `68dcb9f`
**Sources**: 2 (Copilot R1 @ `68dcb9f`, Local Review (Pre-Push) @ `da7782b` — same code) + the pre-push run's Claude+Copilot adversarial specialists
**Cross-source confirmations**: 2
**CI**: none (repo has no CI gate, #42)

### Findings
- [ ] (cross-confirmed: Copilot R1 + Local Review pre-push #1/#4 + adversarials) GL cleanup without a current context — widget doneCurrent() before gpu_ member dtor's glDeleteTextures → undefined/leak/crash; add context-guarded gpu_.cleanup() — `src/gpu_color_map.cpp:87`,`src/waterfall_widget.cpp:67`
- [ ] (cross-confirmed: Copilot R1 + Local Review pre-push #2 + adversarials) shader compile/link failure GTEST_SKIPs instead of FAIL — masks regressions — `test/test_gpu_color_map.cpp:198`
- [ ] (must-fix, Copilot R1 — NEW, not in pre-push) GL-skip gate checks majorVersion()>=3 but code needs 3.3; a 3.0–3.2 context won't skip and the #version 330 shader then fails confusingly. Require >=3.3 — `test/test_waterfall_widget.cpp:90`,`test/test_gpu_color_map.cpp:93`
- [ ] (note, Copilot R1) the context-guarded dtor fix needs #include <QOpenGLContext> — `src/gpu_color_map.cpp:32`
- [ ] (carry-over from pre-push, not raised by Copilot) GL_MAX_TEXTURE_SIZE guard `src/waterfall_widget.cpp:185-219`; NaN shader guard `src/gpu_color_map.cpp:110`; add_row GUI-thread-only doc `waterfall_widget.hpp:67`; V-orientation test assertion; ring-buffer perf (tracked follow-up)

### False positives
- none — all 6 Copilot inline comments are valid.

## Resolution (review fixes applied)
**When**: 2026-06-07 · **By**: Claude Code Agent (Claude Opus 4.8 (1M context))

All pre-push + Copilot R1 findings addressed (colcon test: 219 tests, 0 failures,
+1 V-orientation test):
- [x] GL cleanup without current context — added `GpuColorMap::cleanup()`
  (releases program/VBO/VAO/LUT); widget dtor calls it inside a
  `context() && context()->isValid()` guard with the context current; dtor
  fallback only deletes when a context is current. `+#include <QOpenGLContext>`.
- [x] makeCurrent() in dtor now guarded against absent/invalid context.
- [x] shader compile/link failure now `ASSERT_FALSE` (FAIL), not GTEST_SKIP —
  skip is reserved for "no GL 3.3 context" (the SetUp gate).
- [x] GL-skip gates now require 3.3 exactly (major>3 || (major==3 && minor>=3))
  in both `test_gpu_color_map` and `test_waterfall_widget`.
- [x] GL_MAX_TEXTURE_SIZE clamp (keep newest rows, downsample width) + post-upload
  `glGetError` check → placeholder fallback on failure (no garbage render).
- [x] NaN/Inf guard in shader main() (`value = u_min` → palette floor).
- [x] add_row documented GUI-thread-only (plugin marshals via Qt::QueuedConnection).
- [x] NewestRowAtTop test asserts V orientation (dark history + bright newest →
  top bright, bottom dark).
- Deferred (tracked follow-up): ring-buffer glTexSubImage2D upload.

## Integrated Review
**Status**: complete
**When**: 2026-06-07 20:25 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #49 at `f5dbe71` (fix commit)
**Sources**: 1 (Copilot R2 @ `7edd9aa`)
**Cross-source confirmations**: 0
**CI**: none (repo has no CI gate, #42)

Copilot R2 re-raised the **identical 6 findings as R1** because it re-reviewed the
pre-fix tree (`7edd9aa`, a progress.md-only commit) — the code fixes landed in the
next push (`f5dbe71`). Verified all 6 against the current head: every one is
addressed.

### Findings
- [x] (addressed @f5dbe71) gpu_color_map.cpp missing <QOpenGLContext> — now included `src/gpu_color_map.cpp:31`
- [x] (addressed @f5dbe71) dtor unconditional glDeleteTextures — now guarded on currentContext()+cleanup() `src/gpu_color_map.cpp:87`
- [x] (addressed @f5dbe71) widget dtor leaves gpu_ GL cleanup contextless — now gpu_.cleanup() inside context()/isValid() guard `src/waterfall_widget.cpp:64-71`
- [x] (addressed @f5dbe71) gl_available()/GlContext require >=3 not 3.3 — now minorVersion()>=3 gates `test/test_waterfall_widget.cpp:91`,`test/test_gpu_color_map.cpp:93`
- [x] (addressed @f5dbe71) expect_parity GTEST_SKIP on shader fail — now ASSERT_FALSE `test/test_gpu_color_map.cpp:204`

### False positives
- none — R2 is a stale re-review of pre-fix code; all findings are valid but already fixed.

### Note
Copilot graded `7edd9aa`, not the fix `f5dbe71`. A clean Copilot pass on the fix
needs a manual re-review trigger from the PR Reviewers dropdown (web UI) — the
API/CLI re-review path no-ops (reference_copilot_review_no_api_trigger).
