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

## Integrated Review
**Status**: complete
**When**: 2026-06-07 16:35 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #45 at `d7e2176`
**Sources**: 1 at head (Copilot R1 @ `d7e2176`) + prior timeline (Implementation entry; no prior review)
**Cross-source confirmations**: 0
**CI**: copilot-pull-request-reviewer success (package GTests 200/0 locally)

### Findings
- [ ] (low, Copilot R1) color_map.hpp full-includes marine_colormap/palette.hpp but ColorMap only stores a Palette* -> forward-declare in header, keep full include in .cpp (build hygiene) — `color_map.hpp:37`
- [ ] (low/defensive, Copilot R1) lookup() raw-derefs palette_ (can't be null: set from built-in names via shared_palette) — add a null-fallback, consistent with the rviz#5 guard — `color_map.cpp:109`

### False positives
- (Copilot R1) scale_intensity degenerate range "now relies on normalize()" — `color_map.cpp`: normalize() returns 0 when !(hi>lo), so scale_intensity returns 0 for max<=min; the DegenerateRangeIsZero test (scale_intensity(5,10,10)==0) passes. Contract preserved + covered.
