---
issue: 54
---

# Issue #54 — Modernize rqt_marine_sonar echogram onto the shared decode/colormap stack

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-12 09:55 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved (with hardening applied)

**Branch**: feature/issue-54 at `dadcd05`
**Mode**: pre-push
**Depth**: Standard (reason: render-path rewrite — any-dtype decode + value-window/
gain/contrast/palette pipeline across widget + plugin, ~440 lines changed)
**Must-fix**: 1 (addressed) | **Suggestions**: 7 (6 addressed, 1 deferred)

Independent adversarial reviewers (fresh-context Claude subagent + governance pass)
read the diff cold. The headline find: the new decoded-ping render loop kept the
old inverted-form bounds checks, which NaN geometry sails through.

### Findings
- [x] (must-fix) NaN ping geometry reaches the sample-index cast — `depth < min || depth >= max` are both false for NaN, so a malformed ping (sample_rate=0, NaN sound_speed) hit `samples[static_cast<size_t>(NaN)]` (out-of-bounds read) — `src/echogram_widget.cpp` render loop (fixed `dadcd05`: ingestPing() rejects non-finite/degenerate geometry with qWarning; loop guards flipped to positive form; NaN-mixed regression test)
- [x] (suggestion) addPing geometry validation makes the must-fix unreachable — folded into the same ingestPing() gate
- [x] (suggestion) QImage allocation unguarded — worst case kMaxDepthSamples rows ≈ 0.5 GiB; isNull() check added after allocation
- [x] (suggestion) per-ping image rebuild O(N·buffer) on bursts — addPings() batch entry point added; newPings() drains the queue through it (regression test)
- [x] (suggestion) maybeSeedValueWindow half-set window — setValue(lo) fired the handler with the old max still in place; QSignalBlocker around the seed, complete window forwarded explicitly
- [x] (suggestion) seed-vs-restoreSettings race — a ping arriving between initPlugin() and restoreSettings() could seed before the saved window loads; settings_restored_ flag gates seeding
- [x] (governance) `libqt5opengl5-dev` missing as build_export_depend in rqt_sonar_waterfall — downstream links the exported Qt5::OpenGL; fixed `029573b` (CI had passed only by container side-effect). Stale ci.yml marine_colormap comment refreshed in the same commit
- [x] (governance) silent drop of unsupported-dtype pings — qWarning added in ingestPing()
- [ ] (suggestion, deferred) stamp-0 buffer collision — inherited map<stamp> design, buffer redesign territory; carried over from the #51 review, noted again here

### Static analysis / tests
ament lint — pass. gtest: 283 tests, 0 failures (new: NanGeometryPingDoesNotPoisonRender,
AddPingsBatchMixedValidity; existing #54 dtype-decode + render regressions all green).
Local uncrustify 0.78.1 drift not applicable (CI version is authoritative).
