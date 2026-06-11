---
issue: 51
---

# Issue #51 — Migrate rqt_marine_sonar (water-column echogram) into rqt_operator_tools — faithful jazzy port

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-11 10:50 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved (with hardening applied)

**Branch**: feature/issue-51 at `684f4c3`
**Mode**: pre-push
**Depth**: Standard (reason: new C++ package, ~330 lines of canvas logic + executor→GUI threading)
**Must-fix**: 3 (all addressed) | **Suggestions**: 3 (2 addressed, 1 noted)

Two independent adversarial reviewers (fresh-context Claude subagent + Copilot CLI)
read the diff cold and converged on the same findings. The Claude reviewer verified
against the ROS 1 original that the port is faithful — every defect is pre-existing
and inherited, not introduced by the port. Hardening commit `684f4c3` addresses the
real ones with behavior-preserving guards.

### Findings
- [x] (must-fix) Ping::sampleAt out-of-bounds read — index unbounded by image.data.size(); inclusive upper bound over-reads at maximumDepth() — `src/ping.cpp:58` (fixed: half-open interval + data.size() bound + tests)
- [x] (must-fix) updateEchogram unbounded/degenerate QImage dims — depth_sample_count/bin_size_ unvalidated — `src/echogram_widget.cpp:271` (fixed: finite/positive guards + kMaxDepthSamples clamp)
- [x] (must-fix) div-by-zero / int(NaN) UB on degenerate input — bin_size_==0 pre-first-ping, area.height()==0 hidden, max_db_==min_db_ — `src/echogram_widget.cpp` zoom/pan/pixmap/updateEchogram (fixed: early-return + dB-window guard)
- [x] (suggestion) teardown race — queued newPings() after subscription reset — `src/marine_echogram_plugin.cpp:93` (mitigated: drain queue under lock in shutdownPlugin; full QPointer-based lifecycle hardening deferred to the modernization follow-up, matching the sibling waterfall's pattern)
- [x] (suggestion) QoS deviation to SensorDataQoS — both reviewers judged defensible; documented inline
- [ ] (suggestion) stamp-0 buffer collision — pings with unset/zero header stamp collapse to one buffer slot; inherited from the ROS 1 map<ros::Time> design. Deferred to the modernization follow-up (buffer redesign territory), noted in the PR.

### Static analysis
ament lint (cpplint, cpplint include-order, uncrustify, xmllint, copyright, etc.) — all pass.
Unit tests: 7 gtest cases (Ping geometry + sampleAt boundary/truncation), 0 failures.

## Integrated Review (PR #52 — Copilot round 1)
**Status**: complete
**When**: 2026-06-11 11:10 -04:00
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Reviewed at**: `a1d9e1c` | **Fixes at**: `f20832d`
**Copilot comments**: 4 | **Valid**: 4 | **False positives**: 0

All four Copilot inline comments triaged against local code — all valid, all fixed.
Three caught gaps the pre-push adversarial pass missed/deferred (endianness, ping_spacing
divide, member-poisoning-before-validation).

### Findings
- [x] (valid) sampleAt reinterpret_cast strict-aliasing/alignment UB + ignores is_bigendian — `src/ping.cpp` (fixed: byte-assemble per is_bigendian + memcpy; +big-endian gtest)
- [x] (valid) ping.cpp missing <cstring>/<cstdint> — `src/ping.cpp` (fixed: added)
- [x] (valid) adjustPixmap divides by ping_spacing_; corrupted persisted value bypasses spin-box min — `src/echogram_widget.cpp` (fixed: finite/>0 guard)
- [x] (valid) updateEchogram commits geometry to members before validating; malformed ping poisons wheelEvent/adjustAxis state — `src/echogram_widget.cpp` (fixed: validate in locals, commit only if finite/ordered/positive)

### Result
Builds clean; 46 ament tests, 0 failures (8 gtest cases). Awaiting a fresh Copilot
round at the new head before merge.
