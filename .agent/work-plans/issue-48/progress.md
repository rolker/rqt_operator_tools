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
