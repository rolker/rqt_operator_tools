---
issue: 56
---

# Issue #56 — rqt side: generic marine_control widgets + rqt_marine_control plugin

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-13 17:55 -04:00
**By**: Claude Code Agent (Claude Fable 5)
**Verdict**: approved (with fixes applied)
**Branch**: feature/issue-56 at `67c72d0`
**Mode**: pre-push
**Depth**: Standard (two new C++/Qt packages; threading + Qt lifecycle; sole gate — Copilot quota exhausted until end of June 2026)
**Must-fix**: 0 | **Suggestions**: 7 (all addressed)

Fresh-context adversarial review confirmed the threading model (executor→GUI
marshaling via QMetaObject::invokeMethod to a slot, mutexed latest_, QPointer
teardown), QoS (QoS(10) = RELIABLE+VOLATILE per D5), MOC/pluginlib wiring, and
memory ownership (clear() deletes layout children; no double-free) all correct.
No crashes/leaks/load-failures. Findings were correctness/robustness:

### Findings
- [x] (suggestion) INT bounds float64→int cast could overflow (UB) — clamp to int range
- [x] (suggestion) FLOAT spinbox fixed 2 decimals silently rounded/round-tripped — derive decimals from step or the value's precision
- [x] (suggestion) parseBool not case-insensitive — an echoed "TRUE"/"On" flipped the checkbox; now case-insensitive (+ "yes"), canonicalized on refresh
- [x] (suggestion) ENUM value outside the choices silently diverged from the label — now shown in the combo
- [x] (suggestion) no-op editingFinished republished the same value (audit noise) — suppress emit when unchanged (per-row last-value)
- [x] (suggestion) refresh button used QPushButton::pressed — switched to clicked
- [x] (suggestion) bare Qt5 export — export Qt5 + Qt5Widgets component
- [ ] (noted, not implemented) stale rows not removed on a shrinking ControlSet (control sets assumed stable; documented); device_name not used in the title

### Tests
73 gtests across both packages (per-type widgets, read-only/units, refresh-no-reEmit,
checkbox edit, case-insensitive bool, enum-outside-choices, distinct-toggle, clear;
topic_filter selection + change-topic derivation) + ament lint — 0 failures.
