# Plan: rqt_marine_control — grouping, range hints, multi-device tabs

## Issue

https://github.com/rolker/rqt_operator_tools/issues/92

## Context

`ControlSetWidget` renders a flat `QGridLayout` (columns: name | value | input) and
ignores `ControlItem.group`. `MarineControlPlugin` shows a single `ControlSetWidget`
for one topic at a time; `ControlSet.device_name` is never displayed; numeric bounds
(min/max/step) are enforced silently by spinbox limits but not visible to the operator.
`ControlSetWidget` has one external caller (`rqt_marine_control`) — verified no others.

## Approach

### 1. `ControlSetWidget` — group sections

Replace `QGridLayout * grid_` with a `QVBoxLayout * vbox_` inside the widget. Each
group gets a bold `QLabel` section header followed by an inner `QGridLayout` with four
columns: name | value | input | range_hint. Items with `group == ""` go in a "General"
section at the top. Groups are created in first-seen order (`std::vector<std::string>
section_order_` + `std::map<std::string, GroupSection> sections_`).

`GroupSection` holds the header widget, the inner grid, and a running row count so
`apply()` can append into the right section. The `rows_` map (`std::map<std::string,
Row>`) is unchanged; `apply()`, `clear()`, `rowCount()`, `valueText()`, `inputFor()`
keep their existing semantics. Add `sectionCount() const` for test introspection.

`clear()` deletes the header widgets and inner grids, resets `sections_` and
`section_order_`, and clears `rows_` (as before). It also deletes the new
`Row::range_hint` label so no hint widget is leaked (resolves Plan Review
suggestion).

### 2. `ControlSetWidget` — range hints

Add `QLabel * range_hint = nullptr` to `Row`. In `makeInput()`, for `TYPE_FLOAT` and
`TYPE_INT` when `max_value > min_value`, create a `QLabel` showing "[min – max unit]"
(e.g., "[0.0 – 100.0 m]") and set `row.range_hint`. `apply()` inserts it at column 3
in the group's inner grid. Controls without explicit bounds leave column 3 empty.
Also set a tooltip on the spinbox: "Range: min – max unit, step N" for full detail.

### 3. `MarineControlPlugin` — multi-device tabs

Replace the single `control_widget_` / `state_sub_` / `change_pub_` / `latest*` with a
`QTabWidget * tab_widget_` (closable tabs, `setTabsClosable(true)`) plus a dedicated,
node-free **`TabManager`** that owns the per-tab lifecycle, and `std::string
manual_topic_` (tracks which tab was opened via `topic_combo_`).

**Test seam (resolves Plan Review must-fix).** The per-tab subscription/publisher is
created behind an injectable `TabTransportFactory`, so the tab lifecycle is
unit-testable without a live ROS node:

- `TabTransport` (interface) — `publishChange(name, value)`; its destruction tears
  down the underlying subscription/publisher.
- `TabTransportFactory = std::function<shared_ptr<TabTransport>(state_topic, on_set)>`
  where `on_set(ControlSet)` is invoked on the GUI thread for each received set.
- `RclcppTabTransport` (the real impl, in the plugin .cpp) creates the
  RELIABLE+VOLATILE/depth-10 state subscription and the change publisher, and marshals
  each received set onto the GUI thread.
- `TabManager` holds `std::map<std::string, std::shared_ptr<TabEntry>>` keyed by state
  topic; `TabEntry` = `{ ControlSetWidget * widget, shared_ptr<TabTransport> transport,
  bool titled }`.

`TabManager` API: `openTab`, `closeTab`, `clear`, `hasTab`, `tabCount`,
`topicForIndex`, `widgetFor`.

**`openTab(state_topic)`** — if a tab for that topic exists, activate it and return.
Otherwise create a `TabEntry` + `ControlSetWidget`, add the tab titled with the topic
string, connect the widget's `controlChanged` to a lambda **capturing the
`shared_ptr<TabEntry>`** (so it can't dangle if the tab closes) that publishes via
`entry->transport`, then build the transport via the factory.

**`closeTab(state_topic)`** — reset `entry->transport` (destroying the
subscription/publisher), `removeTab`, delete the widget, erase the entry.

**`applySet(topic, set)`** (GUI thread) — look the entry up by topic (a closed tab is a
safe no-op); `entry->widget->apply(set)`; on the first set carrying a non-empty
`device_name`, replace the tab title with it.

**Threading model (resolves Plan Review suggestion).** `TabManager`'s map is touched
**only on the GUI thread**, so no `tabs_mutex_`/`latest_mutex_` is needed. The single
cross-thread point is `RclcppTabTransport`'s ROS callback, which captures only
self-contained copies (the delivery `std::function` + a stable QObject* marshalling
target — never a `TabManager`/`TabEntry` pointer) and the queued GUI lambda captures
only a message snapshot, so a concurrent tab close cannot dangle the delivery. Mirrors
the #78-proven marshal-to-a-stable-QObject pattern.

**Coexistence design** — bridge connect → `openTab(state_topic)` (drops the old
`selectTopic()` call); bridge disconnect → `closeTab(state_topic)`. Manual
`topic_combo_` selection → `openTab(topic)` and update `manual_topic_`; if the
manually chosen topic already has a tab (e.g. a bridge tab), `openTab` activates it.
Moving/clearing the combo closes the previous manual tab **unless** it is also a
connected bridge device. `tabCloseRequested(index)` → if the tab's topic matches a
connected bridge device, `disconnect()` it (operator decision: tab = device
presence); if it matches `manual_topic_`, clear the combo bookkeeping; then
`closeTab(topic)`.

**Settings (operator decision: ACTIVE tab only).** `saveSettings()` persists only the
**active tab's** state topic (`tab_manager_->topicForIndex(currentIndex())`) — a
minimal extension of today's single-`topic` persistence. This intentionally narrows
review-issue's "persist multi-tab state" note: bridge tabs are transient and reopen on
the next bridge connect, so full multi-tab restore is out of scope. `restoreSettings()`
reopens that one topic as a manual tab (unchanged `selectTopic` path). Documented in a
code comment in `saveSettings()`.

**`shutdownPlugin()`** — `tab_manager_->clear()` (resets every tab's transport, no
leaked subs), then reset `bridge_client_` as before.

### 4. Tests

In `test_control_set_widget.cpp` (grouping + range hints):
- `GroupedItemsRenderInSections` — apply a ControlSet with items in two named groups;
  verify `sectionCount() == 2`, both items accessible via `inputFor()`.
- `UngroupedItemsGoToDefaultSection` — apply items with empty `group`; verify
  `sectionCount() == 1` and the default ("") section.
- `GroupsPreserveFirstSeenOrder` — items in order A, B, A, B → two sections, A first
  (via `sectionOrder()`).
- `RangeHintAppearsForBoundedFloat` — bounded FLOAT (`min=0, max=10, units="m"`) yields
  a `rangeHintText()` containing "0", "10", "m"; an unbounded FLOAT yields empty.

In `test_tab_manager.cpp` (node-free tab lifecycle, resolves the must-fix). A counting
`FakeTransport`/`Registry` is injected via `TabTransportFactory`, so the lifecycle runs
with no ROS node:
- `OpeningTabsCreatesOneSubscriptionEach` — N tabs → N transports created, all alive.
- `ReopeningSameTopicReusesTabAndSubscription` — same topic → focus, no second sub.
- `ClosingTabDestroysOnlyItsSubscription` — close A → A's transport destroyed (no
  leak), B's still alive; `hasTab`/`tabCount` reflect it.
- `ClearDestroysAllSubscriptions` — `clear()` destroys every transport.
- `EditPublishesOnlyToThatTabsTransport` — toggling tab A's control publishes to A's
  transport only; B's publish log stays empty (no cross-talk).
- `DeliveredSetRendersAndTitlesTabWithDeviceName` — `on_set` renders the widget and
  retitles the tab from the topic string to `device_name`.

## Files to Change

| File | Change |
|------|--------|
| `marine_control_widgets/include/marine_control_widgets/control_set_widget.hpp` | Add `GroupSection`, `range_hint` in `Row`, `vbox_`/`sections_`/`section_order_`; add `sectionCount()`, `sectionOrder()`, `rangeHintText()` |
| `marine_control_widgets/src/control_set_widget.cpp` | Per-group sections, range hint labels + spinbox tooltips, introspection accessors |
| `marine_control_widgets/test/test_control_set_widget.cpp` | 4 new grouping/range-hint tests |
| `rqt_marine_control/include/rqt_marine_control/tab_manager.hpp` (new) | `TabTransport` iface, `TabTransportFactory`, `TabManager` (node-free seam) |
| `rqt_marine_control/src/tab_manager.cpp` (new) | `TabManager` open/close/clear/applySet lifecycle |
| `rqt_marine_control/test/test_tab_manager.cpp` (new) | 6 node-free tab-lifecycle tests |
| `rqt_marine_control/include/rqt_marine_control/marine_control_plugin.hpp` | Add `tab_widget_`, `tab_manager_`, `manual_topic_`; remove single-widget/sub/pub/latch fields |
| `rqt_marine_control/src/marine_control_plugin.cpp` | `RclcppTabTransport`, `makeTransportFactory()`, `connectedDeviceForTopic()`, multi-tab `onTopicChanged()`/`onConnectClicked()`/`onTabCloseRequested()`, active-tab `saveSettings()`, `shutdownPlugin()` |
| `rqt_marine_control/CMakeLists.txt` | Add `tab_manager` source + `test_tab_manager` gtest |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | Groups, bounds, and simultaneous device views directly increase operator situational awareness |
| Capture decisions, not just implementations | Per-tab subscription lifecycle and coexistence design resolved in this plan |
| A change includes its consequences | Tab teardown, settings persistence, no-leaked-subs all explicitly covered |
| Only what's needed | No new abstractions; `QTabWidget` is standard Qt; `TabEntry` is the minimum lifecycle unit |
| Test what breaks | Grouping + range hint tests added; tab teardown enforced by explicit `reset()` calls |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| 0008 — Follow ROS 2 Conventions | Yes | Per-tab subs/pubs use same QoS as existing (RELIABLE + VOLATILE, depth 10); `reset()` destroys on close |
| 0013 — progress.md vocabulary | Yes | Recorded as `## Plan Authored` entry |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `ControlSetWidget` internal layout | `test_control_set_widget.cpp` grouping tests | Yes |
| `ControlSetWidget` public API (new `sectionCount()`) | Only caller is `rqt_marine_control` + tests — no breakage | Yes |
| `onConnectClicked()` to call `openTab()` | Remove the now-unused `selectTopic()` call from that path | Yes |
| Tab teardown | `shutdownPlugin()` must iterate all tabs | Yes |
| Settings | Only the ACTIVE tab's topic persists (operator decision; bridge tabs are transient) | Yes |

## Open Questions (resolved by operator)

- [x] On tab close by the user: **disconnect the bridge device** (tab = device
  presence). `onTabCloseRequested` calls `bridge_client_->disconnect(device)` for a
  connected bridge tab, then tears down the tab. Confirmed by the operator.
- [x] Manual tab title before first state message: **topic string initially**, replaced
  by `device_name` on the first `ControlSet`. Confirmed by the operator.

## Estimated Scope

Single PR. Grouping + range hints are pure widget changes; tab lifecycle is a
subscription refactor. The review-issue entry noted they are cohesive and a single
PR is acceptable if changes remain reviewable in sequence.
