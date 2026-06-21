# Plan: Unify sonar tools to a single shared auto-fit History knob (drop ping-spacing/density)

## Issue

https://github.com/rolker/rqt_operator_tools/issues/79

## Context

Both sonar rqt plugins expose redundant "how much you see" knobs implemented twice:

- `rqt_marine_sonar` (echogram): `maximum_ping_count_` hardcoded at 2048 in
  `echogram_widget.hpp:149` + `ping_spacing_` (px-per-ping, default 1.0) in
  `echogram_widget.hpp:162`, surfaced as `pingSpacingDoubleSpinBox` in
  `marine_echogram_plugin.ui`. The visible column count is
  `ceil(canvas_width / ping_spacing_)`, so spacing is a view transform, not a
  data-retention knob.

- `rqt_sonar_waterfall`: `history_spin_` (range 1–5000, default 200, no explicit
  step → stuck at 1) wires to `set_history()` (the real performance knob).
  `density_spin_` (range 0.25–4.0, step 0.25) wires to `set_range_line_density()`,
  which controls spacing between along-track range gridlines — a pure view
  transform.

The agreed design (issue #79) collapses these to one knob per tool:
**History** = data-retention (buffer size), **auto-fit** = along-track scale
computed as `canvas_px / history`.

## Approach

1. **Add shared `configure_history_spinbox()` to `rqt_sonar_waterfall`** — new
   header `include/rqt_sonar_waterfall/history_spinbox.hpp` (header-only) with a
   free function that configures a `QSpinBox*` with shared parameters:
   range 1–5000, `setSingleStep(50)`, tooltip. Both plugins call this after
   creating their spinbox. Add the header to `CMakeLists.txt`'s `HEADERS` list
   so it is exported with the library.

2. **`rqt_sonar_waterfall` — history spinbox step fix** — call
   `configure_history_spinbox(history_spin_, 200)` in `build_controls_bar()`
   (default 200, preserving the existing default for backward-compat with saved
   layouts). Remove the old manual step-less spinbox setup code.

3. **`rqt_sonar_waterfall` — KEEP `density_spin_` unchanged.**
   **CORRECTION (2026-06-21, operator-confirmed):** `density_spin_` drives
   `set_range_line_density()` — the spacing of the along-track range/distance
   **gridline overlay** (tooltip "Range-line density (higher = more lines)"),
   NOT a buffer/along-track-spacing duplicate. The waterfall already auto-fits
   along-track via its history/ring texture; `density_spin_` is a separate,
   useful measurement aid and **stays unchanged**. (The earlier "drop density"
   decision conflated it with the echogram's `ping_spacing` view knob.) The
   waterfall's only change is step 2 — the shared History control + step fix.

4. **`rqt_marine_sonar` — echogram: add History, remove pingSpacing**
   a. `echogram_widget.hpp`: replace `ping_spacing_` with nothing. Rename
      `maximum_ping_count_` to `history_` (or keep the name but change default
      from 2048 → 500). Add public `void setHistory(int count)` /
      `int history() const`.
   b. `echogram_widget.cpp / uploadTexture()`: remove
      `const float spacing = std::max(ping_spacing_, 1.0f)` and
      `int cols = ceil(canvas_width / spacing)`. Replace with
      `int cols = std::max(1, history_)`. This makes the texture width equal to
      the buffer capacity, and GL stretching auto-fits it to the canvas.
   c. Remove `setPingSpacing()` / `pingSpacing()` from hpp + cpp.
   d. `marine_echogram_plugin.ui`: replace `pingSpacingLabel` +
      `pingSpacingDoubleSpinBox` with `historyLabel` (`History`) +
      `historySpinBox` (plain `QSpinBox`, no range set in .ui — set from code).
   e. `marine_echogram_plugin.cpp`:
      - In `initPlugin()`: call `configure_history_spinbox(ui_.historySpinBox, 500)`.
      - Connect `historySpinBox` `valueChanged(int)` →
        `echogram_->setHistory(value)`.
      - Remove `on_pingSpacingDoubleSpinBox_valueChanged` handler and connection.
      - `saveSettings`: save `"history"` key, drop `"ping_spacing"`.
      - `restoreSettings`: restore `"history"` (default 500), drop
        `setPingSpacing` call. Old layouts missing `"history"` fall back to 500.
        Old layouts with a stale `"ping_spacing"` key are ignored gracefully
        (simply not read).
   f. `marine_echogram_plugin.hpp`: remove `on_pingSpacingDoubleSpinBox_valueChanged`
      declaration.

5. **Tests — `test_echogram_widget.cpp`**:
   - Remove `BadPingSpacingNoCrash` test (line 323) and `setPingSpacing` assertion
     from `SettersRoundTrip` (line 341, 347).
   - Add `HistoryRoundTrip` test: `setHistory(500)`, `EXPECT_EQ(history(), 500)`.
   - Add `HistoryDefaultsTo500` test: freshly constructed widget has `history() == 500`.
   - The existing `BadPingSpacingNoCrash` crash-test intent is replaced: add
     `HistoryClampLow` test: `setHistory(0)` → `history() >= 1` (add a clamp in
     `setHistory()`).

6. **Tests — `test_waterfall_widget.cpp`**: No changes needed — existing
   `set_history()` tests (lines 212–215) stay. No density tests exist to remove.

## Files to Change

| File | Change |
|------|--------|
| `rqt_sonar_waterfall/include/rqt_sonar_waterfall/history_spinbox.hpp` | **New** — shared `configure_history_spinbox()` factory |
| `rqt_sonar_waterfall/CMakeLists.txt` | Add `history_spinbox.hpp` to `HEADERS` list |
| `rqt_sonar_waterfall/src/sonar_waterfall_plugin.cpp` | Switch `history_spin_` to `configure_history_spinbox()` (range/step/tooltip). **`density_spin_` UNCHANGED** |
| `rqt_marine_sonar/include/rqt_marine_sonar/echogram_widget.hpp` | Remove `ping_spacing_`; rename/default `maximum_ping_count_`→500; add `setHistory()`/`history()` |
| `rqt_marine_sonar/src/echogram_widget.cpp` | `uploadTexture()`: replace spacing calc with `cols = history_`; remove `setPingSpacing()`/`pingSpacing()` |
| `rqt_marine_sonar/src/marine_echogram_plugin.ui` | Replace spacing spinbox+label with history spinbox+label |
| `rqt_marine_sonar/include/rqt_marine_sonar/marine_echogram_plugin.hpp` | Remove `on_pingSpacingDoubleSpinBox_valueChanged` declaration |
| `rqt_marine_sonar/src/marine_echogram_plugin.cpp` | Wire history spinbox; save/restore `"history"` key; drop ping_spacing wiring |
| `rqt_marine_sonar/test/test_echogram_widget.cpp` | Replace ping-spacing tests with history tests |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | Exactly the knobs and code called out in #79 — no palette/colormap/TVG changes, no GPU path changes, no new features |
| A change includes its consequences | Settings key rename handled gracefully in `restoreSettings` (missing old key → silent default); old `ping_spacing`/`range_line_density` keys ignored on read |
| Test what breaks | Tests updated to cover `setHistory()` API that replaces `setPingSpacing()`; clamp test ensures no crash on history=0 |
| Improve incrementally | Single PR; `density_spin_` removed without touching the underlying `set_range_line_density()` implementation in the widget, reducing scope |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 — ROS 2 conventions | No | No ROS topic/node/message changes |
| ADR-0013 — progress.md vocabulary | Yes | `## Plan Authored` entry written after commit |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `echogram_widget.hpp` API (remove `setPingSpacing`) | `marine_echogram_plugin.cpp` + tests | Yes |
| `echogram_widget.hpp` API (add `setHistory`) | `marine_echogram_plugin.cpp` + tests | Yes |
| `ping_spacing` settings key dropped | `restoreSettings` must not crash on old layouts | Yes — old key simply not read |
| `history_spinbox.hpp` added to `rqt_sonar_waterfall` | exported via `install(DIRECTORY include/)` + `ament_export_include_directories` (review-plan note — not a `HEADERS` list) | Yes |

## Open Questions

- None — design settled; range-line-density correction operator-confirmed 2026-06-21.

## Review-plan follow-ups (folded in)

- **Preserve the GL-texture clamp** when rewriting `uploadTexture()` (step 4b):
  `cols = std::min(std::max(1, history_), max_dim)` — don't drop the `max_dim` cap.
- **Sync the restore-settings UI**: `historySpinBox->setValue(history())`
  (replacing the old `pingSpacingDoubleSpinBox->setValue`) — `marine_echogram_plugin.cpp:181`.
- `history_spinbox.hpp` is exported via the existing `install(DIRECTORY include/)` +
  `ament_export_include_directories`, not a `HEADERS` list.

## Estimated Scope

Single PR.
