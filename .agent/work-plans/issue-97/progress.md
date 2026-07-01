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
- [x] Confirm responsive layout controller is a distinct class (not inlined in `MarineControlPlugin`) to keep the reparent-state-preservation test tractable.
- [x] Ensure removed `MarineControlPlugin` members (`topic_combo_`, `bridge_combo_`, `device_combo_`, `connect_button_`, `status_label_`, associated slots, `manual_topic_`) are explicitly deleted — not left as dead code.
- [x] Decide whether the hysteresis thresholds (860/700 px) are compile-time constants or runtime-settable (issue says "settable constant" — clarify in plan).

### Operator decision (2026-07-01, checkpoint before plan-task)
All three actions resolved by the operator (Roland) via run-issue checkpoint:
1. **Distinct class — ADOPTED.** The responsive layout controller is its own
   class (e.g. `ResponsiveHubLayout`), not inlined in `MarineControlPlugin`, so
   the reparent/state-preservation behavior is unit-testable in isolation.
2. **Delete dead members — ADOPTED.** The plan must explicitly remove the combo/
   Connect toolbar members and their slots (`topic_combo_`, `bridge_combo_`,
   `device_combo_`, `connect_button_`, `status_label_`, `manual_topic_`, and the
   `selectTopic`/`onTopicChanged`/`updateBridgeList`/`onBridgeChanged`/
   `refreshDevices`/`onDeviceChanged`/`onConnectClicked` slots superseded by the
   hub) — no dead code left behind.
3. **Breakpoints = COMPILE-TIME CONSTANTS.** The tab↔side-panel hysteresis
   thresholds (≈860/700 px) are header constants in the layout controller, not a
   runtime-settable value. No rqt setting, no ROS param. Can be promoted to
   settable later if it ever proves necessary (out of scope for v1).
