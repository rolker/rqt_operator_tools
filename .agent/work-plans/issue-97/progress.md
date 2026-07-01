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

## Plan Authored
**Status**: complete
**When**: 2026-07-01 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-97/plan.md` at `5987ad7`
**Branch**: feature/issue-97 at `5987ad7`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-07-01 02:18 +00:00
**By**: Claude Code Agent (Claude Opus)  <!-- independent review: Opus, fresh context; plan was authored by Claude Sonnet. Name-only self-review heuristic would false-match since all agents share the name, so annotation omitted. -->

**Plan**: `.agent/work-plans/issue-97/plan.md` at `5987ad7`
**PR**: PR-less (`--issue` mode)
**Verdict**: changes-requested

Note: `gh` unauthenticated in this dispatch — issue body/comments not re-fetched;
evaluated against the recorded Issue Review findings + operator decisions above.
Project ADR-0003/0008 text not independently readable (no `docs/decisions/` in
repo); ADR compliance assessed from the `marine_control_plugin.hpp` contract
docstring. Plan is otherwise well-grounded — all removed members/slots and all
called backend APIs verified against the current source.

### Findings
- [ ] (must-fix) Test approach infeasible: `BridgeControlClient` is a concrete, non-virtual, rclcpp-node-bound class (`bridge_control_client.hpp:50-80`) — "stub BridgeControlClient (no live ROS node)" needs a seam (interface or injected callbacks); add the affected header to Files to Change — `plan.md:45`
- [ ] (must-fix) Dead code left behind (violates operator decision 2): `onTabCloseRequested` (`marine_control_plugin.hpp:84`) and `connectedDeviceForTopic` (`marine_control_plugin.hpp:97`) are orphaned by the redesign but absent from the removal list — `plan.md:36-37`
- [ ] (suggestion) Name the stable QObject that receives the marshaled `devicesChanged` callback after a bridge rebuild (hub survives reparenting) — `plan.md:20,22`
- [ ] (suggestion) Scope is at the upper edge of a single PR (~9 files, 2 non-trivial new classes) but cohesive — no split needed — `plan.md:48-60`
