---
issue: 44
---

# Issue #44 — rqt_sonar_waterfall: migrate onto shared marine_colormap

## Implementation
**Status**: complete (pending review)
**When**: 2026-06-07
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**Branch**: feature/issue-44

Sourced the palette data and transfer math from the shared `marine_colormap`
library, keeping the rqt-side API the waterfall widget/plugin already use so the
diff is contained to `color_map.{hpp,cpp}`:
- `ColorMap` now holds a `const marine_colormap::Palette *` (mapped from
  `ColorMapType` -> "grayscale"/"bronze"/"thermal") instead of a local stop
  vector; `lookup()` delegates to `Palette::sample()` + `to_rgba8()` -> `Rgb`.
- `scale_intensity()` delegates to `marine_colormap::normalize()` +
  `apply_response()` (the lib's formula is identical to the old local one).
- Deleted the duplicated palette stop tables and the 8-bit `lerp_channel`.
- Kept rqt-domain pieces unchanged: `Rgb`, the `ColorMapType` enum + selector
  helpers (`color_map_name/index/from_index`, `kColorMapCount`), and
  `auto_range()` (operates on `WaterfallRow`, the rqt buffer type).
- Added the `marine_colormap` dependency (package.xml + CMake; wired into the
  library and the `test_color_map`/`test_waterfall_widget` targets).

**Colours preserved:** rqt's thermal *was* the canonical de-dup ramp, and
grayscale/bronze match, so no visible shift. All golden tests pass unchanged
(endpoints/exact stops match; the one interpolated assertion, grayscale t=0.5,
is 128 under both 8-bit and float lerp). `colcon test`: 200 tests, 0 failures.

Closes #44. Part of rolker/unh_marine_autonomy#137.
