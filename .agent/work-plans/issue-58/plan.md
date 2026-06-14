# Plan: rqt_sonar_waterfall — water-column removal + slant→ground range + range lines + uniform ping scaling

## Issue

https://github.com/rolker/rqt_operator_tools/issues/58

Scope is display correction only. Target-marking / `TargetAnnotation` is split to #59
(message-gated) and is out of scope here.

## Context

The waterfall renders raw **slant-range** pings with the water column included, no
across-track reference, and each ping resampled to the buffer's max **sample count**
independently — so equal screen width represents unequal ground extent and the scale
jumps when the sonar changes range. All three defects live in the render hop:
`SingleBeamExtractor` → `combine_rows` → `WaterfallWidget::upload_texture()` (index-based
resample) + `paintGL()` (edge labels). Intensities are kept raw in the buffer by design;
geometry/intensity corrections belong at render time.

## Approach

Add geometry + TVG as pure, unit-testable transforms applied at upload, plus a depth
subscription that stamps altitude onto each row. Every correction is an independent,
persisted toggle. The three geometry/scaling toggles default **ON**; TVG defaults **OFF**.

1. **Depth input** — Plugin gains a **Depth** combo subscribing to `sensor_msgs/Range`
   (the `garmin_sidescan` driver's `~/nadir_depth`, configurable). Latest value cached and
   stamped onto each row (`WaterfallRow.altitude`) at `post_row`. Absent/stale depth →
   ground mode falls back to raw slant with an on-canvas indicator.
2. **(a) Ground projection** — pure `resample_row_to_ground(samples, range_max, altitude,
   half_width_m, columns)`: per output column at ground range `g`, slant `s = √(g²+h²)`;
   `s > range_max` → no-data (black); else nearest sample. Water column removed for free
   (`g≥0 ⇒ s≥h`). Toggle off → existing slant resample.
3. **(c) Uniform scale** — `upload_texture` sets texture full width to the **max range
   across visible rows** (ground or slant per mode), each row resampled onto that common
   metres-axis; narrower pings occupy fewer columns. Manual range-max override.
4. **(b) Range lines** — `paintGL` QPainter overlay: across-track gridlines at round range
   intervals + labels, using the widget's half-width→pixel mapping (labels read "slant" or
   "ground" per mode).
5. **TVG (precomputed, default off)** — `WaterfallRow.intensities_tvg` + the slope `p` it
   was computed at. Factor `(max(R,R_ref)/R_ref)^p` (`R` = per-sample slant from the row's
   own `range_max`), **computed once per ping at ingest** in `add_row`, independent of view
   geometry. Toggle = `upload_texture` selects raw vs tvg vector (no recompute); slope
   change = one buffer-wide recompute (rare); lazy — nothing spent until first enabled.
6. **Controls + persistence** — second toolbar row: Depth combo, Ground range (ON),
   Uniform scale (ON), Range lines (ON), TVG (OFF) + slope spin. All saved/restored.
7. **Tests** — `test_ground_resample.cpp` (altitude=0 ≡ slant, `s>range_max` black,
   symmetry, water-column excision) and TVG cache tests (p=0 identity, monotonic with range,
   toggle selects without recompute).

## Files to Change

| File | Change |
|------|--------|
| `rqt_sonar_waterfall/include/.../waterfall_model.hpp` | `WaterfallRow`: add `altitude`, `intensities_tvg`, `tvg_slope`, `nadir_index`, per-side `range_max_port/_stbd`; declare `resample_row_to_ground`, `apply_tvg` |
| `rqt_sonar_waterfall/src/waterfall_model.cpp` | implement the two pure functions; `combine_rows` records `nadir_index` + per-side range |
| `rqt_sonar_waterfall/include/.../waterfall_widget.hpp` | state + setters: `set_ground_range`, `set_uniform_scale`, `set_range_lines`, `set_tvg`, `set_tvg_slope`, `set_altitude_*`; ground half-width accessor |
| `rqt_sonar_waterfall/src/waterfall_widget.cpp` | `add_row` computes TVG (lazy); `upload_texture` selects source + applies ground/uniform remap; `paintGL` range-line overlay; slope-change recompute |
| `rqt_sonar_waterfall/include/.../sonar_waterfall_plugin.hpp` | depth sub + combo members; altitude cache; new toggle/slope widgets |
| `rqt_sonar_waterfall/src/sonar_waterfall_plugin.cpp` | depth combo + `sensor_msgs/Range` subscription; stamp altitude; second controls row; save/restore |
| `rqt_sonar_waterfall/test/test_ground_resample.cpp` (new) | geometry + TVG unit tests |
| `rqt_sonar_waterfall/CMakeLists.txt` | register new test; `sensor_msgs` dep if not present |
| `rqt_sonar_waterfall/package.xml` | add `sensor_msgs` if not already a dep |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Every correction is an explicit, visible toggle; ground mode shows an indicator when depth is missing rather than silently faking geometry |
| Only what's needed | Reuses the existing raw-buffer + render-time-transform design; no new message contract (uses existing `sensor_msgs/Range`) |
| Test what breaks | Geometry + TVG are pure functions with dedicated unit tests; edge cases (no depth, `s>range_max`, p=0) covered |
| A change includes its consequences | `saveSettings`/`restoreSettings` updated for all new controls; `combine_rows` extended so the remap has nadir/per-side range |
| Capture decisions | TVG default-off and precompute-vs-recompute rationale recorded here and in the PR |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| (none repo-local) | No | `rqt_operator_tools` has no `docs/decisions/`; no workspace ADR governs single-plugin display geometry. No new contract introduced (#59 holds the msg-gated half) |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `WaterfallRow` fields | `combine_rows`, `add_row` caching, any row construction in tests | Yes |
| New view toggles | `saveSettings`/`restoreSettings` persistence | Yes |
| Add `sensor_msgs` dep | `package.xml` + `CMakeLists.txt` | Yes |
| `upload_texture` resample path | `test_waterfall_widget.cpp` expectations | Yes — review/extend |

## Open Questions

- **Combined-row geometry**: `combine_rows` currently keeps only `max(range_max)` and drops
  the nadir boundary (= port sample count). Plan adds `nadir_index` + per-side range to
  `WaterfallRow` so the per-side ground projection is exact. Confirm port/stbd may carry
  *different* ranges (asymmetric) — if always symmetric in practice, the per-side fields are
  belt-and-suspenders but harmless.
- **Range-line interval policy**: auto-pick a "nice" interval (1/2/5·10ⁿ) from the visible
  half-width — assumed; flag if a fixed interval control is wanted.

## Estimated Scope

Single PR. Cohesive, all within `rqt_sonar_waterfall`; the pure-function split keeps the
diff testable. If review prefers, TVG (precompute path) could split to a follow-on, but the
geometry work is the bulk and naturally lands together.
