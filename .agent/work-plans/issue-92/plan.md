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
`section_order_`, and clears `rows_` (as before).

### 2. `ControlSetWidget` — range hints

Add `QLabel * range_hint = nullptr` to `Row`. In `makeInput()`, for `TYPE_FLOAT` and
`TYPE_INT` when `max_value > min_value`, create a `QLabel` showing "[min – max unit]"
(e.g., "[0.0 – 100.0 m]") and set `row.range_hint`. `apply()` inserts it at column 3
in the group's inner grid. Controls without explicit bounds leave column 3 empty.
Also set a tooltip on the spinbox: "Range: min – max unit, step N" for full detail.

### 3. `MarineControlPlugin` — multi-device tabs

Replace the single `control_widget_` / `state_sub_` / `change_pub_` / `latest*` with:
- `QTabWidget * tab_widget_` (closable tabs, set via `setTabsClosable(true)`)
- `std::map<std::string, std::shared_ptr<TabEntry>> tabs_` keyed by state topic
- `std::mutex tabs_mutex_` (guards `tabs_` from executor callbacks)
- `std::string manual_topic_` (tracks which tab was opened via `topic_combo_`)

`TabEntry` holds: `ControlSetWidget * widget`, `state_sub`, `change_pub`,
`std::mutex latest_mutex`, `ControlSet latest`, `bool have_latest`.

**`openTab(state_topic)`** — if a tab for that topic already exists, activate it and
return. Otherwise: create a `TabEntry`, subscribe (RELIABLE + VOLATILE, depth 10);
connect the entry's `controlChanged` to a per-tab lambda that publishes via
`entry->change_pub`; add tab to `tab_widget_` with `setTabData(index, topic)` set
to the topic string; wrap the subscription callback to invoke `applyLatest(topic)` on
the GUI thread via `QMetaObject::invokeMethod(this, [=](){ ... }, Qt::QueuedConnection)`.

**`closeTab(state_topic)`** — reset `entry->state_sub` and `entry->change_pub`
(destroying the DDS subscription/publisher), remove the tab widget from `tab_widget_`,
erase from `tabs_`.

**`applyLatest(topic)`** (GUI thread) — find `TabEntry`, copy latest set under lock,
call `entry->widget->apply(set)`. On first apply, update the tab label to
`set.device_name` if non-empty; otherwise show the topic string.

**Coexistence design** — bridge connect → `openTab(state_topic)` (drops the old
`selectTopic()` call); bridge disconnect → `closeTab(state_topic)`. Manual
`topic_combo_` selection → `openTab(topic)` and update `manual_topic_`; if the
manually chosen topic already has a tab (e.g. a bridge tab), activate it. Clearing
the combo closes the manual tab. `tabCloseRequested(index)` → if the tab's topic
matches a connected bridge device, disconnect it; if it matches `manual_topic_`,
clear `topic_combo_`; then `closeTab(topic)`.

**Settings** — `saveSettings()` persists only `topic_combo_->currentText()` (bridge
tabs are transient; they reopen on the next bridge connect). `restoreSettings()` is
unchanged.

**`shutdownPlugin()`** — iterate `tabs_`, reset all `state_sub`/`change_pub`, clear
`tabs_`, then reset `bridge_client_` as before.

### 4. Tests

In `test_control_set_widget.cpp`:
- `GroupedItemsRenderInSections` — apply a ControlSet with items in two named groups;
  verify `sectionCount() == 2`, both items accessible via `inputFor()`.
- `UngroupedItemsGoToDefaultSection` — apply items with empty `group`; verify
  `sectionCount() == 1`.
- `GroupsPreserveFirstSeenOrder` — items in order A, B, A, B → two sections, A first.
- `RangeHintAppearsForBoundedFloat` — FLOAT item with `min_value=0, max_value=10,
  units="m"`; verify the range hint tooltip on the spinbox contains "0" and "10".

Tab teardown is not unit-testable without a full rqt node; the teardown correctness is
enforced by `state_sub_.reset()` (which calls `rclcpp`'s unsubscribe path) and the
`QTabWidget`'s `removeTab` + widget deletion. Note this in a follow-up test issue if
further coverage is needed.

## Files to Change

| File | Change |
|------|--------|
| `marine_control_widgets/include/marine_control_widgets/control_set_widget.hpp` | Add `GroupSection`, `range_hint` in `Row`, `vbox_`/`sections_`/`section_order_`; add `sectionCount()` |
| `marine_control_widgets/src/control_set_widget.cpp` | Per-group sections, range hint labels + tooltips, `sectionCount()` |
| `marine_control_widgets/test/test_control_set_widget.cpp` | 4 new grouping/range-hint tests |
| `rqt_marine_control/include/rqt_marine_control/marine_control_plugin.hpp` | Add `TabEntry`, `tab_widget_`, `tabs_`, `tabs_mutex_`, `manual_topic_`; remove single-widget fields |
| `rqt_marine_control/src/marine_control_plugin.cpp` | Multi-tab lifecycle: `openTab()`, `closeTab()`, `applyLatest(topic)`, updated `onTopicChanged()`, `onConnectClicked()`, `shutdownPlugin()` |

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
| Settings | Only `topic_combo_` topic persists (bridge tabs are transient) | Yes |

## Open Questions

- [ ] On tab close by the user (close button): disconnect the bridge device, or only tear down the local subscription (leaving the bridge wired)? Plan proposes: disconnect the bridge (tab = device presence), consistent with bridge disconnect also closing the tab.
- [ ] Manual tab title before first state message: topic string or "(manual)"? Plan proposes: topic string initially, replaced by `device_name` on first state message.

## Estimated Scope

Single PR. Grouping + range hints are pure widget changes; tab lifecycle is a
subscription refactor. The review-issue entry noted they are cohesive and a single
PR is acceptable if changes remain reviewable in sequence.
