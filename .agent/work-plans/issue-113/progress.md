---
issue: 113
---

# Issue #113 — rqt plugin: a boat's current ping seen from the front

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-19 10:05 -04:00
**By**: Claude Code Agent (Claude Sonnet 5.5)
**Verdict**: approved

**Branch**: feature/issue-113
**Mode**: pre-push
**Depth**: Standard (reason: new package, about 1500 lines)
**Must-fix**: 4 | **Suggestions**: 12
**Round**: 1 | **Ship**: recommended — all four must-fixes (codex) and the sensible suggestions were fixed in the follow-up commit; 90 tests pass. Gemini timed out (420 s) and gave no findings.

### Findings
- [x] (must-fix) queued messages replayed late and stamped at handling time — mailbox with ROS-thread receipt time — `ping_profile_widget.py`
- [x] (must-fix) all-invalid ping erased the last good one and refreshed staleness — now a BadPing — `profile_model.py`
- [x] (must-fix) long title pushed the status off screen — title elides — `ping_profile_widget.py`
- [x] (must-fix) error-log throttle bypassed by alternating faults — one global limit — `ping_profile_widget.py`
- [x] (suggestion) auto-scale reset on topic change, 1 m floor, beams >= 90 degrees dropped, history dims when stale, paint save/restore exception-safe, paint error shown, bool settings rejected, README items — various
- [ ] (suggestion) inverted fixed range resets both ends rather than the offending one — `profile_model.py` (documented, left)
- [ ] (suggestion) sound-speed fallback for sonars reporting 0 — `profile_model.py` (left; reason shown on screen)
