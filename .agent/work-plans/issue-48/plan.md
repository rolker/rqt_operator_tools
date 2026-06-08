---
issue: 48
---

# Issue #48 — rqt_sonar_waterfall GPU render path

GPU (QOpenGLWidget) waterfall: upload full-precision intensity (R32F) and
colormap in a fragment shader via the shared `marine_colormap` GLSL
(rolker/marine_colormap#5 / PR #6). Part of unh_marine_autonomy#137.

## Dependency

Needs `marine_colormap::colormap_glsl()` (`marine_colormap/shader.hpp`), provided
by marine_colormap **≥ #6** (merged into `jazzy` 2026-06-07). The main-tree
`ui_ws/src/marine_colormap` carries it; no symlink workaround is needed now that
#6 is in. (Historical: while #6 was open the worktree symlink was temporarily
repointed at the issue-5 worktree, and this PR was held until #6 landed.)

## Approach — staged, foundation first

The full QWidget→QOpenGLWidget swap is large and would destabilize the shipping
widget right before the 2026-06-08 freeze. So land it in stages; this PR is
stage 1 (the validated, self-contained foundation), the widget swap follows.

### Stage 1 (this PR) — `GpuColorMap` renderer + offscreen-GL parity test

- **`gpu_color_map.hpp/.cpp`** — a GL helper, no widget/FBO ownership (so the
  widget can use it against its default framebuffer and the test against an FBO):
  - `initialize()` — `QOpenGLFunctions_3_3_Core` init; build a
    `QOpenGLShaderProgram` from a fragment shader that prepends `#version 330 core`
    + sampler decls + `main()` onto `marine_colormap::colormap_glsl()`. (No
    `precision` qualifier — that is GLES-only; desktop GLSL 330 rejects it.)
    Returns false (+ log) on compile fail.
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

### Stage 2 (this branch, folds into PR #49 → closes #48) — widget swap

`WaterfallWidget : public QOpenGLWidget`, keeping the public API
(`add_row`/`set_*`) so `control_panel`/`sonar_waterfall_plugin` are untouched.

- **Context**: request a 3.3 format in the ctor (compatibility profile so the
  QPainter overlay for labels works alongside `GpuColorMap`'s raw GL).
- **Intensity texture (first cut = re-upload)**: on any data/view change, set a
  `dirty_` flag + `update()`; in `paintGL` (context current) assemble a
  `W x count` R32F buffer from `WaterfallBuffer` (oldest row first → screen-top =
  newest with no V-flip), each row nearest-resampled to `W = max row width`, and
  `glTexImage2D` it. The CPU bottleneck was the per-pixel *recolor*, not the
  upload (~800 KB/ping is trivial), so re-upload is already a big win and is
  simple/correct. **Ring-buffer `glTexSubImage2D` optimization is a noted
  follow-up** if profiling ever shows upload cost.
- **Colormap on GPU**: `paintGL` sets range/gain/contrast uniforms and calls
  `GpuColorMap::draw()`; palette re-baked only on `set_color_map`. Auto-range
  from the per-row cached extremes (PR #41) feeds the `min`/`max` uniforms — no
  recolor. Re-ranging/gain/contrast/palette are now free.
- **Overlays**: dark `glClear` placeholder; "No sonar data" + range-`m` labels
  via `QPainter(this)` in `paintGL` after the GL draw.
- **Tests**: rework `test_waterfall_widget` to `grabFramebuffer()` behind a
  GL-availability skip (same pattern as `test_gpu_color_map`).

### Out of scope (follow-ups)
- Ring-buffer subimage upload (optimization over re-upload).
- rviz Ogre material + CAMP GL viewport (separate issues / camp#63).
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
