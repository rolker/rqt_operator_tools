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
2. **Pure, testable helpers on `IndicatorConfig`** (`config_model.py`) — keep all the new
   logic out of the Qt widget so it is unit-testable directly:
   - `has_thresholds` property: `True` when `threshold_warn`/`threshold_error` is set.
   - `select_keyvalue(values) -> Optional[str]`: return the value string for `value_key`
     (exact key match); if `value_key` empty, fall back to `values[0]` (current display
     default); `None` if not found / empty.
   - `combine_levels(diag_level, threshold_level) -> IndicatorLevel` (staticmethod):
     deliberate severity-max over `OK < WARN < ERROR`; if `diag_level is STALE`, return
     `STALE` (a stale diagnostic is not trustworthy for thresholds — this **deliberately
     masks** a threshold-computed ERROR; documented + tested). Producer-STALE (byte 3 →
     `IndicatorLevel.STALE`) is the *only* STALE `combine_levels` ever sees: the widget's
     own `_check_stale` timer emits ERROR/WARN with text "no data", never
     `IndicatorLevel.STALE`, and runs on a separate path.
   - `evaluate_diagnostic(native_level, values, message='') -> (IndicatorLevel, text)`:
     the single entry point. With thresholds: `select_keyvalue` → if `None` (key absent)
     return **ERROR** + flag text; else wrap `float(value_str)` — on `ValueError`/`TypeError`
     (non-numeric) return **ERROR** + flag text (e.g. `Voltage?`); else
     `combine_levels(native_level, evaluate_level(val))` + formatted text. Does **not** lean
     on `evaluate_level` to coerce a string (a numeric string still returns ERROR there, and
     `float` is what `format` needs). Without thresholds: reproduces today's behavior
     exactly (native level, first-KeyValue formatted, `message`/level-name fallback).
3. **Wire into `_handle_diagnostics`** (`annunciator_widget.py`) — map the producer level
   via `_diagnostic_level_to_indicator`, then delegate to `config.evaluate_diagnostic(...)`
   and `widget.set_status(...)`. The widget no longer parses KeyValues itself.
4. **Round-trip the new keys through the config dialog** (`config_dialog.py`) — required so
   editing a thresholded diagnostics row in the GUI doesn't silently drop `value_key`/
   thresholds once `to_dict` emits them (today `get_config` has no `value_key` widget and the
   threshold edits live in the hidden topic group). Add a `value_key` field + warn/error
   threshold fields to the **Diagnostics Settings** group; load them in `load_config` and
   read them in `get_config`. Keeps the GUI a faithful editor of the new schema.
5. **Tests** — see Files to Change; cover serialization, selection, the full combine matrix
   (incl. STALE×threshold), the degraded-data → ERROR branch, dialog round-trip, and
   no-thresholds backward-compat.

## Files to Change

| File | Change |
|------|--------|
| `rqt_annunciator/rqt_annunciator/config_model.py` | `value_key` field; diagnostics threshold (de)serialization; `has_thresholds`, `select_keyvalue`, `combine_levels`, `evaluate_diagnostic` |
| `rqt_annunciator/rqt_annunciator/annunciator_widget.py` | `_handle_diagnostics` delegates to `config.evaluate_diagnostic(native_level, values, message)`; no KeyValue parsing in the widget |
| `rqt_annunciator/rqt_annunciator/config_dialog.py` | add `value_key` + warn/error threshold fields to the Diagnostics Settings group; round-trip in `load_config`/`get_config` (prevents GUI-edit data loss — Finding 1) |
| `rqt_annunciator/test/test_config_model.py` | `value_key` round-trip; diagnostics `thresholds` to_dict/from_dict; `select_keyvalue` (found / `value_key` empty→`[0]` / missing); `combine_levels` full matrix incl. STALE×threshold-ERROR |
| `rqt_annunciator/test/test_threshold.py` | diagnostics-path eval on a synthetic `DiagnosticStatus`: OK level + sub-threshold value → WARN/ERROR; ERROR level + healthy value → stays ERROR; WARN level + ERROR value → ERROR; STALE level + ERROR-computing value → STALE; missing key → ERROR; non-numeric value → ERROR; no-thresholds → byte-for-byte today's behavior |
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
| `to_dict` now emits diagnostics thresholds | `config_dialog.py` must round-trip `value_key`/thresholds or GUI edits silently drop them | **Yes — now in scope** (Finding 1; was wrongly deferred) |
| exact `value_key` matching | live `mavros: Battery` KeyValue casing (`diagnostic_test_publisher.py` uses lowercase `voltage`) | Verify on the boat before the echoboats consumer config ships (issue already flags this) |
| (gap) rqt_operator_tools has no `.agents/README.md` | onboarding guide | No — pre-existing gap, separate task |

## Resolved Decisions

- **Q1 — degraded data (resolved 2026-05-26):** when `value_key`/thresholds are set but the
  KeyValue is missing or non-numeric, the row goes **ERROR** (a lost safety value is treated
  as a fault, not silently green), with the cause flagged in the display text (e.g.
  `Voltage?`). Consistent with the existing topic-path `evaluate_level` TypeError → ERROR
  behavior.
- **Q2 — `value_key` match (resolved 2026-05-26):** **exact** key match — safest for short,
  well-defined diagnostic keys like `Voltage`.

## Estimated Scope

Single PR in `rqt_operator_tools`. Consumer config change is a separate small PR under
echoboats#162 once this merges and the live `value_key` string is confirmed on the boat.
