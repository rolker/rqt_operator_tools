# Plan: rqt_marine_sonar — render the echogram on the GPU colormap stack

## Issue

https://github.com/rolker/rqt_operator_tools/issues/63

## Context

`EchogramWidget` is a `QtCharts::QChartView` that holds the echogram as a
`QGraphicsPixmapItem` and a `QValueAxis` for depth; zoom/pan adjust the axis
range + pixmap transform. The render path is the problem: `updateEchogram()`
re-colormaps **every buffered ping (≤2048) × every depth row** on the CPU
(`scale_intensity` + palette lookup per pixel) on every update, and the plugin
posts a render per incoming message (~39/s) — saturating the GUI thread and
hanging rqt. The sibling `rqt_sonar_waterfall` avoids this by colormapping on
the GPU (`GpuColorMap`: raw floats → `R32F` texture → fragment-shader transfer),
which is already an exported, reusable library. This ports the echogram onto
that same stack.

## Approach

1. **Reuse `rqt_sonar_waterfall::GpuColorMap`** — rqt_marine_sonar already
   depends on rqt_sonar_waterfall (it includes `color_map.hpp`); add
   `gpu_color_map.hpp` + link. No new colormap code; the shader transfer matches
   the CPU `scale_intensity` exactly (parity is GpuColorMap's design contract).
2. **Make `EchogramWidget` a `QOpenGLWidget`** (+ `QOpenGLFunctions_3_3_Core`),
   mirroring `WaterfallWidget`: `initializeGL`/`resizeGL`/`paintGL`, an `R32F`
   intensity texture, and `GpuColorMap::draw()`.
3. **Texture = depth-rows × pings.** Keep the `DecodedPing` float cache and the
   `pings_` buffer. `upload_texture()` packs the binned float samples into the
   texture (one column per ping, depth down rows) using the existing
   shared-geometry logic (min/max depth, bin_size). Re-upload on new pings;
   gain/contrast/value-window/palette become GPU uniforms / LUT — a redraw, never
   a CPU recolor. This removes the per-frame O(pings×depth) CPU cost entirely.
4. **Depth axis + zoom/pan as a `QPainter` overlay** over the GL frame (the
   waterfall already overlays labels this way). Port `adjustAxis()`'s nice-tick
   logic and the wheel-zoom / drag-pan math to drive (a) the texture-sampling
   region (visible depth window → texture V range) and (b) the drawn axis ticks.
5. **Coalesce rendering** — replace the per-message `newPings` post with a small
   GUI-side timer (~20 Hz) draining the queue, so bursts trigger one redraw.
6. **Tests** — rework `test_echogram_widget.cpp` to read back the GL framebuffer
   offscreen (the pattern `test_waterfall_widget` / `test_gpu_color_map` use:
   `QT_QPA_PLATFORM=offscreen`, software GL), since the raster is now a texture,
   not a `QImage`. Keep a CPU/GPU value-parity assertion.

## Files to Change

| File | Change |
|------|--------|
| `rqt_marine_sonar/include/.../echogram_widget.hpp` | Base `QtCharts::QChartView` → `QOpenGLWidget`; drop pixmap/axis members; add `GpuColorMap`, texture id, GL-dirty flags; keep `DecodedPing`/`pings_`/controls; depth-window state |
| `rqt_marine_sonar/src/echogram_widget.cpp` | Rewrite render path (GL init/paint/resize, `upload_texture`, shader draw, QPainter depth-axis overlay, wheel/drag → sampling region); delete the CPU recompute loop |
| `rqt_marine_sonar/src/marine_echogram_plugin.cpp` | Coalesce `newPings` to a ~20 Hz timer; embedding otherwise unchanged |
| `rqt_marine_sonar/test/test_echogram_widget.cpp` | Offscreen-GL framebuffer readback; CPU/GPU parity |
| `rqt_marine_sonar/CMakeLists.txt` | Link `rqt_sonar_waterfall` lib + `Qt5::OpenGL/Gui`; offscreen env on the test |
| `rqt_marine_sonar/package.xml` | Ensure `libqt5opengl5-dev` build/exec dep |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Reuses the exported `GpuColorMap`/`ColorMap`; no duplicate colormap code |
| A change includes its consequences | Tests reworked for the GL raster; plugin throttle updated; package deps adjusted |
| Test what breaks | Offscreen-GL readback + CPU/GPU parity guard the new path; degenerate-geometry guards from the current code are preserved |
| Improve incrementally | Behavior/controls preserved; only the render substrate changes |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0001 (shared scalar colormap) | Yes | Adopts the shared `marine_colormap` GPU path, the ADR's intent; unifies the two viewers on one stack |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `EchogramWidget` base class | plugin embedding (QWidget — compatible), tests | Yes |
| CPU raster → GL texture | `echogramImage()` test accessor → framebuffer readback | Yes |
| Reuse GpuColorMap cross-package | CMake link + `gpu_color_map.hpp` install (already exported) | Yes — verify export at build |

## Open Questions

- **Depth axis becomes a QPainter overlay** (losing the `QtCharts` `QValueAxis`).
  Functionally equivalent (ticks + zoom/pan reimplemented), but the axis is drawn
  by us rather than QtCharts. Confirm that's acceptable (it's required to put the
  echogram on the GL stack; keeping QtCharts would force a readback-hybrid that
  doesn't fully remove the per-update cost).

## Estimated Scope

Single PR, one package. Largest piece is reimplementing the depth axis + zoom/pan
as an overlay; the colormap/texture work is a direct lift from `WaterfallWidget`.
