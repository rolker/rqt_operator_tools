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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-01 03:18 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-97 at `5bc0cfc`
**Mode**: pre-push
**Depth**: Deep (reason: 200+ lines and 10+ files; Qt reparent/callback-marshalling lifecycle surface)
**Must-fix**: 1 | **Suggestions**: 5
**Round**: 1 | **Ship**: continue — one genuine correctness concern (remote-keying) warrants a fix or an explicit scope call before push

Specialists: static analysis (ament_uncrustify/ament_cpplint/cppcheck all clean),
governance (dead-code removal grep-verified complete; consequences addressed),
plan drift (all 9 files, deviations documented + sound), 2 Claude Adversarial
passes (Lens A logic + Lens B systemic, Deep). Copilot off (default). Lens B
independently verified the stable-QObject devices-changed marshalling survives
bridge rebuild + reparent — the core safety property holds.

### Findings
- [x] (must-fix) Hub keys remote devices by `state_topic` alone but `BridgeControlClient` distinguishes `(remote, state_topic)`; multi-remote same-topic → reconcile auto-connects the wrong remote, mis-checks its box, and `onTabClosed` disconnects the wrong remote. Re-key by `(remote, state_topic)` or record multi-remote-same-topic as out-of-scope — `connections_hub_widget.cpp:197,265,308`
- [x] (suggestion) Selected bridge vanishing from discovery resets the combo under QSignalBlocker, leaving a live client while UI shows "no bridge" (state/UI desync); restoreSettings conversely resurrects a phantom bridge — `connections_hub_widget.cpp:151`
- [x] (suggestion) Close teardown-ordering windows: reset `tab_manager_` in shutdownPlugin and guard the `hooks_` lambdas against plugin-freed-before-widget — `marine_control_plugin.cpp:175`
- [x] (suggestion) `bridge_client_.reset()` on GUI thread while executor spins may race in-flight bridge_info callbacks (pre-existing pattern; header calls out the contract) — `marine_control_plugin.cpp:175,279` (deferred: operator decision — pre-existing pattern, header documents the contract; follow-up to be filed)
- [x] (suggestion) Command-line argv topic opens an orphan tab (no checkbox, no bridge connect if remote/unknown) — `marine_control_plugin.cpp:162` (deferred: operator decision — edge input path, not part of the hub contract)
- [x] (suggestion) Add a duplicate-topic-across-remotes test so the must-fix path is covered — `test/test_connections_hub.cpp`

### Operator decision (2026-07-01, checkpoint on Round 1 review)
Roland chose **"Fix properly + safety items"**. For the `address-findings` pass:

- **MUST-FIX — re-key by `(remote, state_topic)`** (do NOT scope multi-remote out).
  The operator station talks to multiple boats, so same-named control topics
  across remotes are realistic. Match `BridgeControlClient`'s own
  `(remote, state_topic)` keying throughout the hub: the desired set, the
  checkbox map, reconcile, and `onTabClosed`/`onTabCloseRequested` must all key on
  the pair, not `state_topic` alone. (Note: the tab is still keyed by state_topic
  in `TabManager`; if two remotes truly share a state-topic string the tab layer
  needs a disambiguated key too — handle or explicitly note.)
- **ADD the duplicate-topic-across-remotes test** exercising the re-keyed path
  (two devices, same state_topic, different remote → correct remote connected,
  correct box checked, correct remote disconnected on close).
- **FOLD IN the safety suggestions:**
  - Close teardown ordering: `control_set_widget`… reset `tab_manager_` in
    `shutdownPlugin`, and guard the `hooks_` lambdas so a plugin freed before the
    widget can't dangle (the lifetime concern from the implementation deviation).
  - Bridge-vanishing / `restoreSettings` desync: don't leave a live client while
    the combo shows "no bridge"; don't resurrect a phantom bridge on restore.
- **DEFER (do NOT fix now; leave a one-line tracked note in the Implementation
  entry + I will file a follow-up):**
  - Pre-existing GUI-thread `bridge_client_.reset()` vs. executor bridge_info
    race (pre-existing pattern; the header already documents the contract).
  - Command-line argv topic opening an orphan tab (edge input path, not part of
    the hub contract).

After applying, write a `## Implementation` entry documenting each finding's
resolution (fixed / deferred-with-reason) so the re-review can verify.

## Implementation
**Status**: complete
**When**: 2026-07-01 03:46 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-97 at `7849e23`
**Addressed**: `## Local Review (Pre-Push)` (2026-07-01 03:18 +00:00, `5bc0cfc`) — Round 1, per the operator decision "Fix properly + safety items"
**Commits**:
- `9debf2c` fix: key hub remote devices by (remote, state_topic) (#97)
- `9a5d90e` fix: keep selected bridge in combo to avoid state/UI desync (#97)
- `6d7884b` fix: guard hub hooks and reset TabManager on shutdown (#97)
- `7849e23` test: duplicate-topic-across-remotes hub test (#97)

### Actions
- [x] (must-fix) Re-key remote devices by `(remote, state_topic)` — `9debf2c`. Added a
  `deviceKey(remote, state_topic)` helper mirroring `BridgeControlClient::deviceKey`
  (same `\n` separator). The desired set, reconcile membership test, remote checkbox
  checked-state, and `onRemoteToggled`/`onTabClosed`/`onTabCloseRequested` now all key
  on the pair. `onTabClosed` resolves the owning remote from a new authoritative
  `open_remote_tabs_` map (state_topic → the remote device occupying that tab) instead
  of first-match-by-topic — so the *correct* remote is disconnected. Persistence now
  stores index-aligned `hub_desired_remote_nodes`/`hub_desired_remote_topics` lists so
  the identity round-trips (`setDesiredRemotes` takes `(remote, topic)` pairs).
  **Tab-layer note (per the operator's "handle or explicitly note"):** TabManager still
  keys tabs by state_topic, so two remotes sharing a state-topic name collapse onto one
  tab (documented in the header). Disambiguating TabManager's key/subscription split is
  out of scope — two remotes bridged to one local state topic is itself a bridge-level
  conflict. — `connections_hub_widget.{hpp,cpp}`
- [x] (test) Duplicate-topic-across-remotes test — `7849e23`. New
  `DuplicateStateTopicAcrossRemotesKeysByRemote`: two remotes, same `/thruster/state`;
  checking the *second* (bravo) connects only bravo, its box (not alpha's) reflects the
  state after reconcile, and closing the shared tab disconnects bravo — the exact path
  first-match keying got wrong. Also migrated the existing desired-device test to the
  new pair API. — `test/test_connections_hub.cpp`
- [x] (suggestion) Bridge-combo state/UI desync — `9a5d90e`. `rebuildBridgeCombo` now
  keeps the currently-selected bridge in the list even when it drops out of discovery,
  so the combo never silently falls back to "no bridge" while a live client persists.
  Documented that `restoreSettings` selecting a not-yet-discovered bridge is the same
  deliberate persist-the-selection policy (reconciled by the deferred refresh), not a
  stray phantom. — `connections_hub_widget.cpp`
- [x] (suggestion) Close teardown-ordering windows — `6d7884b`. Added a shared liveness
  flag (`alive_`) the hub's hooks lambdas copy and check; cleared in `shutdownPlugin`,
  so a devices-changed marshalled after teardown (or if the hub outlives the plugin)
  no-ops instead of dereferencing a freed plugin. `shutdownPlugin` now resets
  `tab_manager_` in a controlled order (after disconnecting the `tabCloseRequested`→hub
  signal and dropping the layout's borrowed TabManager pointer). — `marine_control_plugin.{hpp,cpp}`
- [x] (deferred) `bridge_client_.reset()` vs. executor bridge_info race — **not fixed**
  (deferred: operator decision). Pre-existing pattern; the client header already documents
  the "destroy while node not spinning" contract. Operator will file a follow-up.
- [x] (deferred) Command-line argv topic opens an orphan tab — **not fixed** (deferred:
  operator decision). Edge input path, outside the hub contract.

### Test / lint results
- Build clean: `./ui_ws/build.sh marine_control_bridge_client marine_control_widgets rqt_marine_control` — 0 errors, 0 new warnings.
- Tests green: `./ui_ws/test.sh rqt_marine_control` — 90 tests, 0 failures, 14 skipped
  (includes the new duplicate-topic test; uncrustify/cpplint/cppcheck/copyright all pass).

### Environment note (not a code change)
As in the prior implementation, the shared core layer had no build, so
`marine_control_interfaces` and `udp_bridge_interfaces` were built once in `core_ws`
to satisfy the ui-layer build; no core sources were modified.
