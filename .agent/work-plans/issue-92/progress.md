---
issue: 92
---

# Issue #92 — rqt_marine_control: grouping, range hints, multi-device tabs

## Issue Review
**Status**: complete
**When**: 2026-06-30 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #92
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Scope Assessment

Three closely related UX improvements to two packages in the same project repo
(`rqt_marine_control` + `marine_control_widgets`). All changes are contained within
`rqt_operator_tools`. The third feature (multi-device tabs) is the most complex and
introduces a subscription lifecycle change; the issue appropriately defers the
design question to plan-task. A single PR is feasible but will be substantial.

**Right repo?** Yes — changes are entirely within the `rqt_operator_tools` project repo.

**Dependencies?** No blocking dependencies. `unh_marine_navigation#84` is motivation
context; issue #78 (deferred DDS queries) is existing behavior to preserve.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | All three improvements directly increase operator visibility (device name, bounds, multi-device) |
| Enforcement over documentation | OK | No new compliance rules proposed |
| Capture decisions, not just implementations | Watch | The per-tab subscription map architecture is a non-trivial design; plan should document the lifecycle design clearly |
| A change includes its consequences | Watch | Tab teardown / settings save-restore for multi-tab state should be explicitly covered, not just "where practical" |
| Only what's needed | OK | Three focused UX improvements targeting a concrete operator pain (visible after unh_marine_navigation#84) |
| Improve incrementally | Watch | Three features in one PR is slightly large; splitting tab lifecycle is possible but they are cohesive; acceptable if plan keeps each change reviewable |
| Test what breaks | Action needed | Subscription lifecycle teardown (no leaked subs, no cross-talk) is exactly the class of bug hard to catch in the field — tests must be required, not optional |
| Workspace vs. project separation | OK | No workspace-level changes |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| 0001 — Adopt ADRs | Watch | The per-tab subscription map is a notable design change; if the approach is non-obvious, capture it in plan rationale or a lightweight ADR addendum |
| 0002 — Worktree isolation | Yes | Feature work uses the worktree — already in place |
| 0008 — Follow ROS 2 Conventions | Yes | New per-tab publishers/subscribers must be created and destroyed cleanly; ensure node lifecycle follows ROS 2 conventions |
| 0013 — progress.md vocabulary | Yes | This entry |

### Consequences

- If `ControlSetWidget`'s constructor or public API changes to support grouping,
  any callers outside `rqt_marine_control` (check for other plugins using it) need updating.
- Settings save/restore must be extended to persist multi-tab state (active tab,
  per-tab topic). This is called out in the acceptance criteria but should be
  explicitly planned.
- The acceptance criterion "Existing behaviors preserved: no-op-edit suppression,
  signal-blocking on refresh, deferred DDS queries (#78), settings save/restore"
  should map to explicit test cases in the plan.

### Recommendations

- Require (not "where practical") tests for the tab subscription lifecycle: verify
  no leaked subs after tab close, no cross-talk between tabs with simultaneous
  updates.
- Clarify in planning whether a manually-selected topic and a bridge-connected device
  can coexist as tabs, or whether they are mutually exclusive modes.
- Consider splitting implementation into two PRs: (1) grouping + range hints (lower
  risk, pure widget changes) and (2) multi-device tabs (subscription lifecycle change).
  This is a Recommendation, not a blocking concern — if the team prefers one PR, the
  issue is still plan-task-ready.

### Actions
- [ ] Require tests for tab subscription lifecycle (no leaked subs, no cross-talk) — not optional.
- [ ] Plan must document the per-tab subscription map lifecycle design (create/destroy semantics, settings persistence for multi-tab state).
- [ ] Verify no other callers of `ControlSetWidget` public API exist that would break if the constructor/API changes.

## Plan Authored
**Status**: complete
**When**: 2026-06-30 17:30 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-92/plan.md` at `4851315`
**Branch**: feature/issue-92 at `4851315`
**Phases**: single

### Open questions
- [ ] Tab close by user: disconnect bridge device or only tear down local subscription? Plan proposes disconnect (tab = device presence).
- [ ] Manual tab title before first state: topic string or "(manual)"? Plan proposes topic string, replaced by device_name on first message.

## Plan Review
**Status**: complete
**When**: 2026-06-30 17:37 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-92/plan.md` at `4851315`
**PR**: PR-less (--issue mode)
**Verdict**: changes-requested

### Findings
- [ ] (must-fix) review-issue's "not optional" tab-lifecycle tests (no leaked subs / no cross-talk) are downgraded to a follow-up issue without reconciliation — add a node-free test seam (testable topic-keyed tab map, or injectable subscription factory counting create/destroy) or document a waiver in the plan — `plan.md:93`
- [ ] (suggestion) Settings persist only `topic_combo_` text and `manual_topic_` is a single string, so only one manual tab is restorable — confirm this matches issue intent vs review-issue's "persist multi-tab state" note — `plan.md:74-77`
- [ ] (suggestion) `tabs_mutex_` likely unnecessary if callbacks touch only `entry->latest` and `tabs_` stays GUI-thread-only; the real hazard is `closeTab()` resetting `state_sub` mid-callback — clarify callback captures `shared_ptr<TabEntry>` so it can't dangle — `plan.md:46,57`
- [ ] (suggestion) Ensure `clear()` also deletes the new `Row::range_hint` label (current `clear()` deletes name/value/input only) to avoid a widget leak — `plan.md:31-32`

### Notes
- Verified: `ControlSetWidget` has exactly one external caller (`rqt_marine_control`); all referenced message fields (`group`, `device_name`, `min_value`/`max_value`/`step`/`units`) exist; existing test infra (`QApplication` fixture, `inputFor()` + `dynamic_cast`) supports the 4 planned grouping/range-hint tests.
- Coexistence question from review-issue (manual topic vs bridge device tabs) is resolved by the plan's Coexistence design.
- Independent review: fresh-context sub-agent on a different model (Opus) than the Sonnet plan author — not in-context self-review despite the shared agent name.

## Implementation
**Status**: complete
**When**: 2026-06-30 18:10 +00:00
**By**: Claude Opus

**Branch**: feature/issue-92
**Commits**:
- `5e7d1a4` feat(marine_control_widgets): group section headers + numeric range hints
- `62dec60` feat(rqt_marine_control): multi-device tabs with per-tab subscription lifecycle
- `79de708` docs(issue-92): sync plan with operator decisions and test seam
- (this entry) progress: implementation complete for #92

### Plan Review must-fix — resolved
Added a **node-free test seam**: per-tab ROS I/O is created behind an injectable
`TabTransportFactory` (returning a `TabTransport` whose destruction tears down the
sub/pub). The lifecycle lives in a dedicated `TabManager` (`tab_manager.hpp/.cpp`) that
needs no ROS node — only Qt + the message types. `test_tab_manager.cpp` injects a
counting `FakeTransport`/`Registry` and proves: opening N tabs creates N transports
(`OpeningTabsCreatesOneSubscriptionEach`); reopening a topic reuses the tab/sub
(`ReopeningSameTopicReusesTabAndSubscription`); closing a tab destroys exactly its
transport with the others alive (`ClosingTabDestroysOnlyItsSubscription`); `clear()`
destroys all (`ClearDestroysAllSubscriptions`); an edit in tab A publishes only to A's
transport (`EditPublishesOnlyToThatTabsTransport` — no cross-talk); and a delivered set
renders + retitles the tab (`DeliveredSetRendersAndTitlesTabWithDeviceName`). The real
`RclcppTabTransport` (in the plugin .cpp) is the only node-dependent piece and is not
unit-tested, but it is a thin wrapper over `create_subscription`/`create_publisher`.

### Plan Review suggestions — resolved
- **range_hint leak**: `ControlSetWidget::clear()` now deletes `Row::range_hint` along
  with name/value/input.
- **Drop `tabs_mutex_`**: there is no `tabs_mutex_`/`latest_mutex_`. `TabManager`'s map
  is touched only on the GUI thread; the only cross-thread point is the transport's ROS
  callback, which captures self-contained copies (a `std::function` + a stable
  QObject* target) and marshals a message snapshot to the GUI thread, so a concurrent
  `closeTab()` can't dangle. The publish lambda captures `shared_ptr<TabEntry>`.
  Threading model documented in `tab_manager.hpp` and the plugin's `RclcppTabTransport`.
- **Settings scope**: confirmed as the operator's ACTIVE-tab-only decision (below).

### How each feature was built
- **Group section headers** (`ControlSetWidget`): `QVBoxLayout` of per-group sections
  (`GroupSection` = header `QLabel` + inner `QGridLayout` + row count), keyed by
  `ControlItem.group` ("" → "General"), inserted in first-seen order before a trailing
  stretch. The header is hidden while only one section exists, so an ungrouped set keeps
  the old flat-grid look. Existing no-op suppression, signal-blocking-on-refresh, and
  read-only handling are untouched (the per-row `makeInput`/`set_value` logic is
  unchanged).
- **Device name + range hints**: `ControlSet.device_name` becomes the tab title on the
  first message. Bounded FLOAT/INT controls get a compact `[min – max units]`
  `range_hint` label (column 3) plus a `Range: … , step …` spinbox tooltip; unbounded
  controls (`max_value <= min_value`) get neither.
- **Multi-device tabs**: `QTabWidget` (closable) + `TabManager`. `openTab` creates a
  tab/widget/transport (or focuses an existing one); `closeTab` resets the transport
  then removes+deletes the widget; `clear()` (from `shutdownPlugin`) tears all down.
  Bridge connect → `openTab`; bridge disconnect → `closeTab`; **tab close →
  disconnect** the matching connected bridge device then close (operator decision). The
  manual `topic_combo_` owns one tab tracked by `manual_topic_`. Preserves deferred DDS
  queries (#78), signal-blocking on refresh, no-op suppression, and zero-tab graceful
  behavior (empty tab bar, no crash).

### Active-tab-only persistence (operator decision)
`saveSettings()` persists only the active tab's state topic
(`tab_manager_->topicForIndex(currentIndex())`) — a minimal extension of the prior
single-`topic` persistence. Bridge tabs are transient and reopen on connect, so full
multi-tab restore is intentionally out of scope. Documented in a `saveSettings()` code
comment and in plan.md so it doesn't read as an unaddressed gap.

### Build & test
This environment had no lower layers built, so `marine_control_interfaces` +
`udp_bridge_interfaces` were built in `core_ws` first. Then from the worktree root:
- Build: `./ui_ws/build.sh marine_control_widgets marine_control_bridge_client rqt_marine_control` — all packages finished, no errors/warnings.
- Test: `./ui_ws/test.sh rqt_marine_control marine_control_widgets` — **90 tests, 0 errors, 0 failures, 11 skipped** (lint/copyright skips). New suites: `TabManagerTest` 6/6 pass; `ControlSetWidgetTest` incl. the 4 new grouping/range-hint cases pass.

Not pushed; no PR opened (host re-reviews and publishes).

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-30 18:17 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-92 at `769742e`
**Mode**: pre-push
**Depth**: Deep (reason: ~1100 LOC C++, subscription lifecycle + cross-thread executor↔GUI marshalling)
**Must-fix**: 0 | **Suggestions**: 4
**Round**: 1 | **Ship**: recommended — no must-fix findings; both adversarial passes confirm no correctness/lifecycle bugs and the "no mutex" threading claim holds; static analysis clean.

### Findings
- [ ] (suggestion) `saveSettings` persists the active tab's topic, which may be a bridge-only tab and restores as a node-less empty manual tab — persist `manual_topic_` or skip bridge-only — `rqt_marine_control/src/marine_control_plugin.cpp:223`
- [ ] (suggestion) Disconnecting (Connect button) a topic that is also the manual tab leaves `manual_topic_` stale + combo pointing at a closed tab until selection changes — `rqt_marine_control/src/marine_control_plugin.cpp:450`
- [ ] (suggestion) `apply()` is append-only: a control dropped from a later heartbeat leaves a stale row / orphaned section header (pre-existing, more visible with sections) — `marine_control_widgets/src/control_set_widget.cpp:372`
- [ ] (suggestion) Document the queued-delivery dangle-safety invariant (Qt flushes queued events on plugin-QObject destruction + applySet topic-lookup no-op; not a QPointer guard) at the subscription callback — `rqt_marine_control/src/marine_control_plugin.cpp:62`

### Specialists
- Static analysis: ament_cpplint / ament_uncrustify / ament_cppcheck on all changed C++ — no problems found.
- Governance: principles Pass; ADR-0003 / 0008 / 0013 compliant; consequences map satisfied; all three review-issue action items resolved (lifecycle tests delivered via node-free TabTransportFactory seam, plan documents lifecycle, no other ControlSetWidget callers — re-verified).
- Plan drift: implementation matches plan.md "Files to Change" exactly; Plan Review must-fix + 3 suggestions all resolved. No scope creep.
- Claude Adversarial (2 disjoint lenses, Deep): Lens A (logic) and Lens B (systemic/safety) both found no must-fix; Lens B independently verified the no-mutex threading claim and no-leaked-subscription guarantee hold.

## Implementation
**Status**: complete
**When**: 2026-06-30 18:30 +00:00
**By**: Claude Opus

**Branch**: feature/issue-92
**Commits**:
- `a00f563` fix(marine_control_widgets): reconcile dropped controls in apply()
- `2e8ca8c` fix(rqt_marine_control): persist manual tab, clear stale manual state on disconnect
- (this entry) progress: pre-push review fixes for #92

Operator chose to fix **all four** pre-push review suggestions. Each is resolved below.

### Finding 1 — `saveSettings` persisted a bridge-only tab (`marine_control_plugin.cpp:223`)
`saveSettings()` no longer reads the active tab; it persists the manual tab's topic
directly, since `restoreSettings` always reopens the saved topic as a *manual* tab and a
bridge-only tab cannot be restored (nothing reopens the bridge connection). The active-tab
nuance is moot — there is exactly one manual tab, and it coincides with the active tab when
the operator's manual selection is focused. New line:
```cpp
instance_settings.setValue("topic", QString::fromStdString(manual_topic_));
```
with an updated comment explaining the bridge-only-tab hazard and why manual_topic_ is the
only restorable choice.

### Finding 2 — stale `manual_topic_` on Connect-button disconnect (`marine_control_plugin.cpp:450`)
`onConnectClicked()`'s disconnect branch now clears the manual bookkeeping immediately,
mirroring `onTabCloseRequested` (signal-blocked combo reset so it doesn't reopen a tab):
```cpp
if (manual_topic_ == device.state_topic) {
  manual_topic_.clear();
  const QSignalBlocker blocker(topic_combo_);
  const int empty_index = topic_combo_->findText("");
  topic_combo_->setCurrentIndex(empty_index >= 0 ? empty_index : -1);
}
tab_manager_->closeTab(device.state_topic);
```
The manual-tab/combo state is now consistent at disconnect, not deferred to the next
selection change.

### Finding 3 — `apply()` was append-only (`control_set_widget.cpp:372`)
`apply()` now reconciles after upserting the incoming items. `Row` gained a `group` field
(its raw section key). The reconciliation:
```cpp
std::set<std::string> incoming;
for (const auto & item : set.items) { incoming.insert(item.name); }
for (auto it = rows_.begin(); it != rows_.end(); ) {
  if (incoming.count(it->first) == 0) {
    delete it->second.name; delete it->second.value;
    delete it->second.input; delete it->second.range_hint;   // matches clear()
    it = rows_.erase(it);
  } else { ++it; }
}
// drop sections left with zero rows so no orphaned header remains
std::set<std::string> live_groups;
for (const auto & [name, row] : rows_) { live_groups.insert(row.group); }
for (auto it = sections_.begin(); it != sections_.end(); ) {
  if (live_groups.count(it->first) == 0) {
    delete it->second.header; delete it->second.grid;
    section_order_.erase(std::remove(section_order_.begin(), section_order_.end(),
      it->first), section_order_.end());
    it = sections_.erase(it);
  } else { ++it; }
}
```
The single-section header-hide rule runs after removal, so it stays correct once a section
is dropped. Surviving rows keep their order: deleting a widget removes it from its grid and
the emptied grid row collapses to zero height, so no re-packing is needed and updated/
unchanged rows keep their no-op-edit suppression, signal-blocked refresh, focus, and
read-only handling (their widgets are never recreated).

New test `ApplyReconcilesDroppedControlsAndEmptySections` (mirrors the existing grouping
tests): applies `{gain@Display, range@Transmit}` (2 rows / 2 sections), then `{gain@Display}`,
and asserts `rowCount()==1`, `inputFor("range")==nullptr`, `inputFor("gain")!=nullptr`,
`sectionCount()==1`, and `sectionOrder()=={"Display"}` (the emptied "Transmit" header is gone).

### Finding 4 — document the dangle-safety invariant (`marine_control_plugin.cpp:62`, doc-only)
Added a comment at the subscription callback's `QMetaObject::invokeMethod` explaining why no
QPointer guard on the plugin QObject (`gui_target`) is needed: Qt flushes/discards queued
events targeting a QObject when it is destroyed (so a delivery queued just before plugin
teardown is dropped, never invoked on freed memory), and `TabManager::applySet`'s topic
lookup is a no-op when the tab has already closed (so a late message for a closed tab is
harmlessly discarded). No behavior change.

### Build & test
Lower layers were unbuilt, so `marine_control_interfaces` + `udp_bridge_interfaces` were
built in `core_ws` first. Then from the worktree root:
- Build: `./ui_ws/build.sh marine_control_widgets rqt_marine_control` — both packages
  finished, no errors/warnings.
- Test: `./ui_ws/test.sh rqt_marine_control marine_control_widgets` — **91 tests, 0 errors,
  0 failures, 11 skipped** (lint/copyright skips; was 90 before — the new reconciliation
  test adds 1). `ControlSetWidgetTest` now 14/14 incl. the new case; `TabManagerTest` 6/6.

Not pushed; no PR opened (host re-reviews and publishes).
