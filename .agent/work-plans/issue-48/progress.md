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
On-screen verification against the Garmin sidescan bag is a Roland-driven gate.
