---
issue: 97
---

# Issue #97 — Connections hub UI for rqt_marine_control

## Issue Review
**Status**: complete
**When**: 2026-07-01 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #97
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Actions
- [ ] Confirm responsive layout controller is a distinct class (not inlined in `MarineControlPlugin`) to keep the reparent-state-preservation test tractable.
- [ ] Ensure removed `MarineControlPlugin` members (`topic_combo_`, `bridge_combo_`, `device_combo_`, `connect_button_`, `status_label_`, associated slots, `manual_topic_`) are explicitly deleted — not left as dead code.
- [ ] Decide whether the hysteresis thresholds (860/700 px) are compile-time constants or runtime-settable (issue says "settable constant" — clarify in plan).
