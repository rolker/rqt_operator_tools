---
issue: 46
---

# Issue #46 — rqt_sonar_waterfall: manual intensity-range default (32767) under-ranges 16-bit (UINT16) data

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-07 17:47 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved

**Branch**: feature/issue-46 at `4a2a242`
**Mode**: pre-push
**Depth**: Standard (reason: C++ logic change with executor/GUI-thread interaction)
**Must-fix**: 1 | **Suggestions**: 2

### Findings
- [x] (must-fix, cross-confirmed: Claude + Copilot) Stale-source seed on rapid topic switch — an in-flight callback from a replaced subscription could win the seed and set the manual default from the wrong source — `src/sonar_waterfall_plugin.cpp`. Fixed with a monotonic per-side subscription id stamped into each callback; mismatched (stale) callbacks are dropped before seeding.
- [x] (suggestion, Claude + Copilot) `dtype` narrowed uint32→uint8; an out-of-range value could alias a valid case. Widened `default_full_scale`/`maybe_seed_manual_range` params to `uint32_t` to match `SonarImageData::dtype`.
- [x] (suggestion, Claude) Re-seed didn't fire when a side switched to (none). Centralized the seed reset in `subscribe()` (runs for both create and clear), so it now always re-seeds for the new configuration.

### False positives
- None.

### Notes
- Static analysis (ament_cpplint) clean on all changed files. 205 gtests pass (5 new DefaultFullScale cases).
- The subscription-id staleness path is Qt/executor-threaded (like `post_row`) and is not unit-tested; the dtype→full-scale mapping is covered by the new gtests.

## Integrated Review
**Status**: complete
**When**: 2026-06-07 19:34 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #47 at `9a686ad`
**Sources**: 2 (Copilot PR review @ `9a686ad`; Local Review (Pre-Push) @ `4a2a242`)
**Cross-source confirmations**: 1 (subscription-staleness mechanism)
**CI**: copilot check pass; per-repo build-and-test gate not visible (ruleset follow-up)

### Findings
- [x] (cross-confirmed, must-fix) Residual TOCTOU: `sub.reset()` precedes the id bump, so an in-flight old-sub callback passes the staleness guard — bump id + clear range_seeded_ *before* reset — `src/sonar_waterfall_plugin.cpp:449`
- [x] (should-fix, Copilot) `restoreSettings()` defaults range_max to 32767.0, undoing the 65535.0 pre-message fallback for configs missing the key — set it to 65535.0 — `src/sonar_waterfall_plugin.cpp:356`

### False positives
- None.

## Integrated Review
**Status**: complete
**When**: 2026-06-07 20:20 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))

**PR**: #47 at `840ec71` (round 2, after rebase onto jazzy + the #43 CI gate)
**Sources**: 2 (Copilot review @ `840ec71`; prior Integrated Review @ `9a686ad`)
**Cross-source confirmations**: 0
**CI**: build-and-test pass (3m7s); copilot check pass

### Findings
- [x] (valid, Copilot) subscribe() clears range_seeded_ but doesn't reset range_max_spin_ to the pre-message fallback, so a source switch (e.g. UINT8 255 -> not-yet-publishing UINT16) clips in manual mode until first message — reset the spin to the fallback on (re)subscribe; factor 65535.0 into one shared constant — `src/sonar_waterfall_plugin.cpp:459`

### False positives
- (Copilot) "restoreSettings() still uses 32767.0" — already fixed in `36d9c2a` (round-1 finding #2); restoreSettings now defaults to 65535.0 (line 358), so the revert-to-32767 failure mode cannot occur.
