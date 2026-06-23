---
issue: 78
---

# Issue #78 — rqt GUI freeze: rqt_marine_control blocking connect on GUI thread

## Issue Review
**Status**: complete
**When**: 2026-06-23 11:30 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #78
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Field regression: rqt froze at startup during 2026-06-16/17 surveys when
`rqt_marine_control` performed (per the issue description) a blocking connect
on the Qt main thread. Issue is well-scoped, right repo, one-PR fix.

### Code Analysis (Acceptance Criterion #1 — confirm blocking call site)

Source was examined before writing this entry. The issue's suspected mechanism
("synchronous connect in `__init__` / `initPlugin`") is **partially correct
but imprecise**:

- `BridgeControlClient` is **not** constructed in `initPlugin` — it is only
  created in `onBridgeChanged()` when the user explicitly selects a bridge.
  The client's constructor uses non-blocking `create_client` / `create_subscription`;
  `connect()` → `callService()` uses `async_send_request` (fire-and-forget).

- The **actual blocking calls in `initPlugin`** are:
  - `updateTopicList()` → `node_->get_topic_names_and_types()` (sync DDS query)
  - `updateBridgeList()` → `node_->get_service_names_and_types()` (sync DDS query)
  These run on the Qt main thread. Under a degraded/missing bridge link, DDS
  discovery can stall, which would produce exactly the observed GUI freeze across
  all plugins in that rqt instance.

The acceptance criterion to "confirm the blocking call site" remains valid and
important — the diagnosis in the issue may need refinement after a targeted
field-condition repro test.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Existing connect/disconnect UI (#71) keeps user in control; fix makes startup non-blocking |
| Test what breaks | Watch | Manual repro scenario is specified; automated test for GUI-thread blocking is hard but a unit test for the async path would help |
| Improve incrementally | OK | Targeted one-PR fix, no scope creep |
| A change includes its consequences | Action needed | If `updateBridgeList()`/`updateTopicList()` become async, `restoreSettings()` (calls `selectTopic()`) must be reviewed for ordering; thread-safe GUI update pattern (existing `QMetaObject::invokeMethod` model) must be applied consistently |
| Capture decisions | Watch | The choice of fix strategy (off-thread QThread, Timer-deferred, or lazy-on-user-action) should be noted in the PR description — the rqt plugin threading model is a non-obvious constraint |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0008 — ROS 2 conventions | Yes | Fix must follow rqt plugin threading patterns: GUI thread for Qt, callbacks via executor thread, marshal back with `QMetaObject::invokeMethod` (pattern already used in `controlSetCallback` → `applyLatest`) |
| ADR-0001 — Capture decisions | Watch | Threading approach choice merits a brief rationale in PR description or code comment |

### Consequences

- Moving `get_topic_names_and_types()` / `get_service_names_and_types()` off the
  GUI thread requires thread-safe UI updates (follow the existing
  `QMetaObject::invokeMethod(…, Qt::QueuedConnection)` pattern).
- Any async refactor of `updateBridgeList()` must not break the `QSignalBlocker`
  pattern that prevents spurious client teardown on repopulate.
- Verify: `restoreSettings()` ordering after async init is correct.

### Actions
- [ ] Confirm actual blocking call site at field conditions (may be `get_service_names_and_types()` / `get_topic_names_and_types()` in `initPlugin`, not a bridge socket connect) — refine issue description if needed
- [ ] Move `updateTopicList()` / `updateBridgeList()` off the GUI thread, or defer them to a zero-interval `QTimer::singleShot` after `initPlugin` returns, so plugin load never stalls the rqt event loop
- [ ] Apply `QMetaObject::invokeMethod(…, Qt::QueuedConnection)` pattern consistently for any async GUI updates (follows existing `controlSetCallback` → `applyLatest` model)
- [ ] Review `restoreSettings()` ordering after any async init refactor
- [ ] Verify: launch rqt with `rqt_marine_control` while bridge target is unreachable — GUI comes up responsive, shows disconnected state
- [ ] Verify: other plugins in the same rqt instance stay interactive throughout

## Plan Authored
**Status**: complete
**When**: 2026-06-23 12:00 -04:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-78/plan.md` at `4b7536f`
**Branch**: feature/issue-78 at `4b7536f`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-06-23 11:21 +00:00
**By**: Claude Code Agent (Claude Opus)
<!-- Independent: fresh-context sub-agent, model Claude Opus (author was Claude Sonnet). The
     skill's name-based self-review heuristic matches only because every workspace agent shares
     the name "Claude Code Agent"; model + dispatch differ, so no self-review annotation. -->

**Plan**: `.agent/work-plans/issue-78/plan.md` at `4b7536f`
**PR**: PR-less (`/review-plan` via worktree)
**Verdict**: approve-with-suggestions

### Findings
- [ ] (suggestion) Deferring `updateTopicList` via `QTimer::singleShot(0)` moves its populate past the `onTopicChanged` connect (cpp:114 before :116 today); unlike `updateBridgeList` it has no `QSignalBlocker` (cpp:275), so the deferred `clear()`/`addItem` emits `onTopicChanged` → transient sub teardown/rebuild. Guard with a `QSignalBlocker` or document as acceptable — `plan.md:22-26`
- [ ] (suggestion) Connect is fire-and-forget (`async_send_request`); setting `status_label_` to "Connected" on click reflects *requested* not *confirmed* — a failed connect won't update it, so it doesn't surface failure. Use "Connecting…" or drive from `bridge_client_->isConnected()` — `plan.md:27-33`
- [ ] (suggestion) `status_label_` updated only in `onConnectClicked`; button state is also synced in `onDeviceChanged` (cpp:329-343). Update the label there too or it goes stale on device/bridge switch — `plan.md:27-33`
- [ ] (suggestion) `QTimer::singleShot` needs `#include <QTimer>`, not currently included and not in the change list — `plan.md:48-54`

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-23 14:09 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-78 at `9378b96`
**Mode**: pre-push
**Depth**: Standard (reason: project work-plans/issue-*/plan.md override-trigger + GUI-thread lifecycle change; line count inflated by committed planning docs, no security/cross-layer/ADR surface)
**Must-fix**: 0 | **Suggestions**: 1
**Round**: 1 | **Ship**: recommended — no must-fix; ament_cpplint clean, both adversarial lenses clear, full plan adherence

### Findings
- [ ] (suggestion, optional) `shutdownPlugin()` doesn't cancel pending `QTimer::singleShot(0)` populates — safe as written (Qt receiver-context overload auto-cancels on destroy; late populate only repopulates signal-blocked combos with node_ alive). Defensive hardening only — `rqt_marine_control/src/marine_control_plugin.cpp:152`
- [ ] (governance/watch) Carry the `QTimer::singleShot(0)` vs QThread/lazy rationale into the PR description (ADR-0001 capture-decisions) — PR body
