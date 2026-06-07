---
issue: 48
---

# Issue #48 — rqt_sonar_waterfall GPU render path

GPU (QOpenGLWidget) waterfall: upload full-precision intensity (R32F) and
colormap in a fragment shader via the shared `marine_colormap` GLSL
(rolker/marine_colormap#5 / PR #6). Part of unh_marine_autonomy#137.

## Dependency

Needs `marine_colormap::colormap_glsl()` (`marine_colormap/shader.hpp`), which is
on the unmerged marine_colormap PR #6 (issue #5). During development the
worktree's `ui_ws/src/marine_colormap` symlink is repointed at the issue-5
worktree; once #6 merges into `jazzy`, the main-tree symlink carries it and the
repoint is unnecessary. **This PR should not merge before #6.**

## Approach — staged, foundation first

The full QWidget→QOpenGLWidget swap is large and would destabilize the shipping
widget right before the 2026-06-08 freeze. So land it in stages; this PR is
stage 1 (the validated, self-contained foundation), the widget swap follows.

### Stage 1 (this PR) — `GpuColorMap` renderer + offscreen-GL parity test

- **`gpu_color_map.hpp/.cpp`** — a GL helper, no widget/FBO ownership (so the
  widget can use it against its default framebuffer and the test against an FBO):
  - `initialize()` — `QOpenGLFunctions_3_3_Core` init; build a
    `QOpenGLShaderProgram` from a fragment shader that prepends `#version 330`
    + `precision highp float;` + sampler decls + `main()` onto
    `marine_colormap::colormap_glsl()`. Returns false (+ log) on compile fail.
  - `set_palette(const marine_colormap::Palette&, int n=256)` — bake the 1-D LUT
    via `marine_colormap::bake_lut(pal, TransferParams{}, n)`, upload as an Nx1
    RGBA8 texture. Re-bake only on palette change.
  - `set_range(min,max)`, `set_gain`, `set_contrast` — uniforms; no recolor.
  - `draw(GLuint intensity_tex)` — full-screen quad sampling the R32F intensity
    texture + LUT, writing colormapped RGBA to the bound framebuffer.
  - Sentinels (below-floor / no-data) handled in `main()`, mirroring the CPU
    `lookup()` precedence.
- **`test/test_gpu_color_map.cpp`** — offscreen GL (`QOffscreenSurface` +
  `QOpenGLContext` 3.3 + FBO). Render a known intensity ramp through the shader,
  `glReadPixels`, and assert each pixel matches `marine_colormap::lookup()`
  (CPU) → `to_rgba8` within 1 LSB. **Self-skips** (GTEST_SKIP) if no GL context
  can be created (headless CI without a software GL stack — see #42), so it never
  fails the build for lack of GL. This is the deferred CPU/GPU parity validation
  from marine_colormap#5.
- CMake: `find_package(Qt5 COMPONENTS Widgets Gui OpenGL)`, link the new sources;
  add the parity test with `QT_QPA_PLATFORM=offscreen` + a software-GL-friendly
  env. `package.xml`: add `libqt5opengl5-dev` build dep.

### Stage 2 (follow-up commit/PR) — widget swap

- `WaterfallWidget : public QOpenGLWidget`, keeping the public API
  (`add_row`/`set_*`) so `control_panel`/`sonar_waterfall_plugin` are untouched.
- Intensity texture as a scrolling ring (R32F, width = max samples, height =
  capacity); `add_row` does a `glTexSubImage2D` of the new row into the ring
  slot; shader offsets V by the ring head so newest stays on top.
- Auto-range from the existing per-row cached extremes (PR #41) → feeds the
  `min`/`max` uniforms instead of a recolor.
- Rework `test_waterfall_widget` to `grabFramebuffer()` (QOpenGLWidget) instead
  of `QWidget::render()`, behind the same GL-availability skip.
- Range-label overlay via QPainter in `paintGL` after the GL draw.

## Files to change (stage 1)

- add `include/rqt_sonar_waterfall/gpu_color_map.hpp`
- add `src/gpu_color_map.cpp`
- add `test/test_gpu_color_map.cpp`
- `CMakeLists.txt` (Qt5 Gui/OpenGL, new sources, new test)
- `package.xml` (`libqt5opengl5-dev`)

## Out of scope

- The widget swap (stage 2) and any change to the shipping CPU render path.
- Real-device verification (sim/bag) — a Roland-driven gate after stage 2.
