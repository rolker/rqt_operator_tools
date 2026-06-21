---
issue: 79
---

# Issue #79 — Unify sonar tools to a single shared auto-fit History knob

## Plan Authored
**Status**: complete
**When**: 2026-06-21 00:00 +00:00
**By**: Claude Sonnet

**Plan**: `.agent/work-plans/issue-79/plan.md` at `2308e54`
**Branch**: feature/issue-79 at `2308e54`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-21 03:20 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-79/plan.md` at `2308e54`
**PR**: PR-less
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) Preserve `cols = std::min(cols, max_dim)` GL-texture clamp when rewriting `uploadTexture()` — `echogram_widget.cpp:323`; plan step 4b — `plan.md:60`
- [ ] (suggestion) Update restoreSettings UI-sync `pingSpacingDoubleSpinBox->setValue` → `historySpinBox->setValue(history())` — `marine_echogram_plugin.cpp:181`; plan step 4e — `plan.md:67`
- [ ] (note) Header export to downstream `rqt_marine_sonar` happens via `install(DIRECTORY include/)` + `ament_export_include_directories`, not the `HEADERS` list — `plan.md:35`
- [ ] (note) Echogram default retention changes 2048→500; confirm against #79's agreed decision (review-issue comments unverifiable — gh offline) — `plan.md:57`

Note: all plan file/line claims verified accurate against source. Cross-package
header sharing is safe — `rqt_marine_sonar` already depends on
`rqt_sonar_waterfall` (package.xml:32, CMakeLists.txt:25) and the latter
installs/exports its headers. No must-fix items.

## Implementation
**Status**: complete
**When**: 2026-06-21 03:38 +00:00
**By**: Claude Opus

**Branch**: feature/issue-79 at `390679a`
**Commit**: `390679a` — feat(sonar): unify echogram + waterfall on a shared History knob (#79)

### What changed (per plan, single commit)
- **New** `rqt_sonar_waterfall/include/rqt_sonar_waterfall/history_spinbox.hpp` —
  header-only `configure_history_spinbox(QSpinBox*, int default_value)`: range
  1–5000, `setSingleStep(50)`, shared tooltip. Added to the `CMakeLists.txt`
  `HEADERS` list; exported to downstream via the existing
  `install(DIRECTORY include/)` + `ament_export_include_directories`.
- **Waterfall** `sonar_waterfall_plugin.cpp` — `history_spin_` now built via
  `configure_history_spinbox(history_spin_, 200)` (default 200 preserved).
  **`density_spin_` left UNCHANGED** (range-line gridline density — separate
  view aid, per the operator-confirmed plan correction).
- **Echogram** `echogram_widget.{hpp,cpp}` — removed `ping_spacing_` /
  `setPingSpacing()` / `pingSpacing()`; renamed `maximum_ping_count_` →
  `history_` (default 2048 → 500); added `setHistory(int)` (clamps ≥1, trims the
  buffer immediately) / `history()`. `uploadTexture()` now computes
  `cols = std::min(std::max(1, history_), max_dim)` — **`max_dim` clamp
  preserved**.
- **Echogram plugin** `.ui` Spacing label+`QDoubleSpinBox` → `History`
  label+plain `QSpinBox` (range set from code). `marine_echogram_plugin.cpp`
  wires `historySpinBox.valueChanged(int)` → `setHistory`, configures it via
  `configure_history_spinbox(ui_.historySpinBox, 500)`; save/restore use a
  `"history"` key (default 500), `restoreSettings` syncs
  `historySpinBox->setValue(history())`; stale `"ping_spacing"` keys are simply
  not read. Removed `on_pingSpacingDoubleSpinBox_valueChanged` handler + decl.
- **Tests** `test_echogram_widget.cpp` — dropped `BadPingSpacingNoCrash` and the
  `setPingSpacing` line in `SettersRoundTrip`; added `HistoryDefaultsTo500`,
  `HistoryRoundTrip`, `HistoryClampLow`. Waterfall tests unchanged.

### Build / test (run in-container)
Toolchain present (ROS 2 Jazzy, colcon, Qt5). Had to build the missing sibling
dep `marine_radar_control_msgs` (in `sensors_ws/src`, not pre-installed) before
the UI packages would configure — not a code issue.
- `colcon build --packages-up-to rqt_sonar_waterfall rqt_marine_sonar` → 3
  packages finished, 0 failed.
- `colcon test` (both packages, `QT_QPA_PLATFORM=offscreen`) → **100% passed,
  0 failed**; all gtest cases + linters (uncrustify/cpplint/copyright/etc.)
  green. Confirmed the three new History gtest cases ran and completed; render
  tests (offscreen GL) passed (self-skip otherwise).

### Deviations
- None functionally. Added `history_spinbox.hpp` to the `HEADERS` list (matches
  the plan's Files-to-Change table) in addition to the `install(DIRECTORY)`
  export the review-plan note clarified — both, for consistency with the other
  headers.
- Also fixed one stale doc comment ("ping-spacing controls" → "history
  controls") in `marine_echogram_plugin.hpp`.
