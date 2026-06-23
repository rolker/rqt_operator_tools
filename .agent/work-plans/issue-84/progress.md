---
issue: 84
---

# Issue #84 — rqt_boat_state: live control/power/environment gauge panel

## Issue Review
**Status**: complete
**When**: 2026-06-23 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #84
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Actions
- [ ] Add unit tests for the Qt-free data model — authority table resolution, staleness detection, and channel-map parsing are all testable without a display; the issue doesn't mention them explicitly.
- [ ] Ensure package structure follows ROS 2 conventions (ADR-0008): package.xml format 3, proper `<exec_depend>`/`<depend>` tags, SPDX license, and license headers in all source files matching the existing rqt_operator_tools convention.
- [ ] Document the ENU/NED + body/ground-velocity convention and the ArduRover default channel map in config or code comments — these are non-obvious constraints that could be accidentally overridden; a short note in the settings dialog or a config README is enough.
- [ ] Verify that `marine_interfaces/SoundSpeed` dependency is correct (the issue cites the message type but not the package name for the `<depend>` tag).
