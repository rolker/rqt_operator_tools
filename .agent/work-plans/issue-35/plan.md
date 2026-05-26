# Plan: rqt_annunciator — value thresholds + KeyValue selection on diagnostics-source indicators

## Issue

https://github.com/rolker/rqt_operator_tools/issues/35

## Context

`rqt_annunciator` colors a `source: diagnostics` row purely by `DiagnosticStatus.level`
(`annunciator_widget.py:316`); `status.values[0]` is read for display text only
(`:319-325`). Value thresholds (`evaluate_level`, `config_model.py:188`) are wired only
into the **topic** path (`:346`). So a diagnostic whose `level` never escalates (e.g.
BizzyBoat's `mavros: Battery`, whose level stays `OK` regardless of voltage) cannot drive
row color — the gap behind [unh_echoboats_project11#162](https://github.com/rolker/unh_echoboats_project11/issues/162),
where the battery row stayed green to ~22 V.

Goal: let a diagnostics row optionally evaluate `thresholds` against a chosen `KeyValue`
(`value_key`), combining the result with the producer's native `level`.

## Approach

1. **`value_key` field + diagnostics threshold serialization** (`config_model.py`) — add
   `IndicatorConfig.value_key: str = ''`; in `from_dict` read `value_key` and stop gating
   `thresholds` to topic-only; in `to_dict` emit `value_key`/`thresholds` for diagnostics
   rows too. No change to existing topic serialization.
2. **Pure, testable helpers on `IndicatorConfig`** (`config_model.py`) — keep the new logic
   out of the Qt widget:
   - `select_keyvalue(values) -> Optional[str]`: return the value string for `value_key`
     (exact key match); if `value_key` empty, fall back to `values[0]` (current display
     default); `None` if not found.
   - `combine_levels(diag_level, threshold_level) -> IndicatorLevel`: severity-max over
     `OK < WARN < ERROR`; if `diag_level is STALE`, return `STALE` (no fresh value to
     trust — the threshold path never yields STALE).
3. **Wire into `_handle_diagnostics`** (`annunciator_widget.py`) — when the matched config
   has thresholds configured: select the KeyValue via `select_keyvalue`, `float()` it,
   `evaluate_level`, then `combine_levels(native_level, threshold_level)`; format display
   text from the selected value. When no thresholds: **unchanged** (native level,
   `values[0]` text). Handle missing/non-numeric KeyValue per Open Question Q1.
4. **Tests** — see Files to Change; cover serialization, selection, combine matrix, the
   degraded-data branch, and backward-compat.

## Files to Change

| File | Change |
|------|--------|
| `rqt_annunciator/rqt_annunciator/config_model.py` | `value_key` field; diagnostics threshold (de)serialization; `select_keyvalue` + `combine_levels` helpers |
| `rqt_annunciator/rqt_annunciator/annunciator_widget.py` | `_handle_diagnostics`: thresholded branch via the helpers; current behavior preserved when no thresholds |
| `rqt_annunciator/test/test_config_model.py` | `value_key` round-trip; diagnostics `thresholds` to_dict/from_dict; `select_keyvalue` (found / `value_key` empty→`[0]` / missing); `combine_levels` matrix incl. STALE |
| `rqt_annunciator/test/test_threshold.py` | diagnostics-path eval end-to-end on a synthetic `DiagnosticStatus` (OK level + sub-threshold value → WARN/ERROR; ERROR level + healthy value → stays ERROR) |
| `rqt_annunciator/config/*` (if a sample config documents indicator keys) | document `value_key` + diagnostics `thresholds` if such a sample exists; else skip |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| A change includes its consequences | Ships with unit tests for every new branch; backward-compat path explicitly tested |
| Test what breaks | Targets the real failure mode (a safety row silently green); tests the severity-combine + degraded-data edges, not Qt glue |
| Only what's needed | Reuses existing `evaluate_threshold`/`evaluate_level`; adds one field + two small pure helpers; no new deps |
| Workspace vs. project separation | Engine stays generic (any numeric KeyValue); boat-specific thresholds live in the echoboats config consumer, not here |
| Human control and transparency | Degraded-data case (Q1) must be visible, never a silent green |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0008 follow ROS 2 conventions | Yes | Uses `diagnostic_msgs` KeyValue semantics; no new message types or non-standard params |
| 0013 progress.md vocabulary | Yes | Plan/implementation tracked via `progress.md` entries in this issue dir |
| others (0001–0012) | No | No infra/field-mode/packaging changes |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `IndicatorConfig` shape (`value_key`) | `to_dict`/`from_dict` + their tests | Yes |
| diagnostics coloring path | backward-compat (no-threshold) behavior + test | Yes |
| engine config schema | echoboats#162 Battery row consumes `value_key`+`thresholds` | No — separate PR there, after this lands |
| config-dialog UI for `value_key` | `config_dialog.py` | No — out of scope per issue; follow-up if non-trivial |
| (gap) rqt_operator_tools has no `.agents/README.md` | onboarding guide | No — pre-existing gap, separate task |

## Open Questions

- **Q1 — degraded data:** when `value_key`/thresholds are set but the KeyValue is missing
  or non-numeric, what should the row show? Proposed default: **keep the native diagnostic
  level** (don't fabricate OK or ERROR) and flag it in the display text (e.g. `Voltage?`),
  so the lost safety-check is visible without false alarms. Confirm vs. escalate-to-ERROR.
- **Q2 — `value_key` match:** exact key match (proposed) vs. substring. Exact is safer for
  short, well-defined keys like `Voltage`; confirm.

## Estimated Scope

Single PR in `rqt_operator_tools`. Consumer config change is a separate small PR under
echoboats#162 once this merges and the live `value_key` string is confirmed on the boat.
