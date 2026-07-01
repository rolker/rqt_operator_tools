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

## Implementation
**Status**: complete
**When**: 2026-07-01 03:10 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-97 at `51f949a`
**Commits**:
- `0a2ef32` feat(rqt_marine_control): replace toolbar with Connections hub UI (#97)
- `51f949a` test(rqt_marine_control): node-free hub + responsive layout tests (#97)

### What was built
- **`ConnectionsHubWidget`** (`connections_hub_widget.{hpp,cpp}`) — scrollable
  three-section checklist (Local / Bridge selector / Remote). Depends on the
  bridge only through the injected **`BridgeControlHooks`** functor seam (struct
  of `std::function`s), so `BridgeControlClient` is untouched and the hub runs
  without a ROS node. Implements local/bridged de-dup, a **desired set**
  reconciled on every devices-changed (a device desired before discovery
  auto-connects + opens), `onTabClosed`/`onTabCloseRequested` (immediate remote
  disconnect on close, no dialog), and `saveSettings`/`restoreSettings`. The
  devices-changed callback is marshalled onto the hub instance itself (the stable
  QObject that outlives bridge rebuilds and reparents).
- **`ResponsiveHubLayout`** (`responsive_hub_layout.{hpp,cpp}`) — owns the hub +
  `QTabWidget` + `QSplitter`; debounced `resizeEvent`; compile-time hysteresis
  constants (`kHubPanelWidthHi=860`, `kHubPanelWidthLo=700`); reparent that
  preserves the hub instance and re-selects the device tab by topic key.
- **`MarineControlPlugin`** rewired: deleted the toolbar members/slots incl.
  `onTabCloseRequested` and `connectedDeviceForTopic` (plan-review must-fix #2);
  builds `BridgeControlHooks` from the real client; delegates save/restore;
  `shutdownPlugin`/`makeTransportFactory`/`RclcppTabTransport` unchanged.
- `CMakeLists.txt`: registered the new sources/headers and two offscreen gtest
  targets. Tests: `test_connections_hub.cpp` (check↔tab sync, connect/disconnect,
  de-dup, desired-set reconcile — all against a fake `BridgeControlHooks` + fake
  transport) and `test_responsive_hub_layout.cpp` (reparent state preservation,
  hysteresis no-thrash, selection-by-topic preservation).

### Deviations from the plan
- **Hooks are stable, not re-handed on bridge change.** Plan step 1 said a bridge
  change would "hand the hub a freshly-populated `BridgeControlHooks`." Instead the
  hooks read the plugin's *current* `bridge_client_` dynamically, so they stay
  valid across a rebuild without a swap; the plugin re-registers the hub's
  marshalling callback on the fresh client and calls `hub->onDevicesChanged()`.
  This still fully satisfies must-fix #1 (hub sees only the injected seam; client
  unchanged; node-free tests) and is simpler / less error-prone.
- **`onTabClosed` signal wiring.** `QTabWidget::tabCloseRequested(int)` connects to
  a hub slot `onTabCloseRequested(int)` that resolves the index→topic via
  `TabManager` and calls `onTabClosed(state_topic)` (the plan named the target
  `onTabClosed`, which takes a topic; the int-index adapter bridges the signal).
- **Also removed `widget_` and `tab_widget_`** plugin members (superseded by
  `ResponsiveHubLayout`), consistent with operator decision 2 (no dead code),
  though they were not on the explicit removal list. `arg_topic_` removed as
  listed; a command-line topic now opens a tab directly.
- **Stable-QObject naming** (plan-review suggestion): documented in the hub header
  and constructor — the callback marshals onto `ConnectionsHubWidget` itself.

### Test / lint results
- Build clean: `./ui_ws/build.sh rqt_marine_control` — 0 errors, 0 new warnings.
- Tests green: `./ui_ws/test.sh rqt_marine_control` — 89 tests, 0 failures
  (13 gtests across the two new suites + existing suites; the rest are lint).
- Lint clean: uncrustify / cpplint / copyright / header-guard all pass on the
  changed files (`.h` third-party include flagged by cpplint's extension
  heuristic silenced with `// NOLINT(build/include_order)`, matching existing
  repo usage).

### Environment note (not a code change)
The shared core layer had no build, so `marine_control_interfaces` and
`udp_bridge_interfaces` were built once in `core_ws` to satisfy the ui-layer
build; no core sources were modified.
