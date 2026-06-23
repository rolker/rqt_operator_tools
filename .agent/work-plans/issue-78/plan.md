# Plan: rqt_marine_control — non-blocking initPlugin

## Issue

https://github.com/rolker/rqt_operator_tools/issues/78

## Context

`MarineControlPlugin::initPlugin` calls `updateTopicList()` and `updateBridgeList()`
synchronously on the Qt GUI thread during plugin load. Both functions call into the
ROS 2 DDS graph (`get_topic_names_and_types()` / `get_service_names_and_types()`),
which can stall under degraded-link or mid-discovery conditions and freeze the entire
rqt process — every plugin in that instance — for the duration of the stall.

The issue review confirmed: `BridgeControlClient` is correctly lazy (created only in
`onBridgeChanged` on explicit user action, not in `initPlugin`). The blocking is
solely from the two synchronous DDS graph queries in `initPlugin`. Fix is localized
to those two call sites.

## Approach

1. **Defer initial list populations in `initPlugin`** — replace the direct
   `updateTopicList()` and `updateBridgeList()` calls with
   `QTimer::singleShot(0, this, &MarineControlPlugin::updateTopicList)` and
   `QTimer::singleShot(0, this, &MarineControlPlugin::updateBridgeList)`.
   `initPlugin` then returns immediately; the lists populate once the Qt event
   loop starts (after rqt finishes loading all plugins).

2. **Add connection-status label** — add a `QLabel * status_label_ = nullptr`
   to the bridge toolbar row showing "Disconnected" at startup. Set it to
   "Connected" on `onConnectClicked` (connect path) and back to "Disconnected"
   on disconnect. Satisfies the acceptance criterion: "surface failure as a
   status indicator, never a frozen GUI."

3. **Verify `restoreSettings` ordering** — `restoreSettings` fires after
   `initPlugin` and before the deferred timer. `selectTopic()` already handles
   this: if the topic is not yet in the combo it is added (lines 188-193 in
   `marine_control_plugin.cpp`). When `updateTopicList()` later fires it
   preserves `topic_combo_->currentText()` (line 168), so the restored topic
   survives the populate. No change needed; confirm in code review.

4. **Manual verification** — no automated test exists for GUI-thread blocking in
   an rqt plugin. PR description will document the repro procedure: launch rqt
   with `rqt_marine_control` loaded while the bridge target is unreachable;
   verify the GUI comes up responsive and the status label shows "Disconnected";
   verify other plugins in the same rqt instance remain interactive.

## Files to Change

| File | Change |
|------|--------|
| `rqt_marine_control/src/marine_control_plugin.cpp` | Replace two direct calls in `initPlugin` with `QTimer::singleShot(0, ...)` deferred calls; update `onConnectClicked` and `onBridgeChanged` to set `status_label_` text |
| `rqt_marine_control/include/rqt_marine_control/marine_control_plugin.hpp` | Add `QLabel * status_label_ = nullptr;` member declaration |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Status label makes disconnected/connecting state visible; operator never faces silent freeze |
| Test what breaks | GUI-thread blocking is not unit-testable for rqt plugins; manual repro procedure documented in PR description; existing `test_topic_filter.cpp` tests cover the pure data path and require no changes |
| Improve incrementally | Two-line primary fix; one new label; no new threads, no redesign |
| A change includes its consequences | `restoreSettings` ordering confirmed safe; `QSignalBlocker` pattern in `updateBridgeList` is unaffected; `QTimer::singleShot` fires on the GUI thread so no thread-safety work needed |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0008 — ROS 2 conventions | Yes | `QTimer::singleShot(0)` defers to the GUI event loop rather than adding a new thread — consistent with the existing `QMetaObject::invokeMethod(…, Qt::QueuedConnection)` pattern used in `controlSetCallback` → `applyLatest` |
| ADR-0001 — Capture decisions | Watch | PR description documents why `QTimer::singleShot(0)` was chosen over QThread or fully-lazy (simpler, no new thread, no API change) |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Defer `updateTopicList` in `initPlugin` | Verify `restoreSettings` topic restore still works with empty combo | Yes — `selectTopic` adds absent topics; `updateTopicList` preserves current text on populate |
| Defer `updateBridgeList` in `initPlugin` | No bridge state is saved/restored; `QSignalBlocker` pattern already prevents spurious client teardown on repopulate | Yes — no additional changes needed |
| Add `status_label_` | Bridge toolbar row layout | Yes — inserted in `bridge_bar` in `initPlugin` |

## Open Questions

- [ ] No open questions — plan is review-plan-ready.

## Estimated Scope

Single PR.
