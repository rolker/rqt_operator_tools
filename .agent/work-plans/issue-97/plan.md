# Plan: Connections hub UI for rqt_marine_control

## Issue

https://github.com/rolker/rqt_operator_tools/issues/97

## Context

`MarineControlPlugin` currently shows a `topic_combo_ + bridge_combo_ + device_combo_ + Connect button` toolbar — modal, one-device-at-a-time. The replacement is a **Connections hub**: a checklist of all available local and remote control devices where toggling a checkbox opens/closes that device's tab and (for remote devices) configures the `udp_bridge`. The hub is responsive: narrow → Tab 0; wide → side panel. Backend libraries (`BridgeControlClient`, `topic_filter`, `TabManager`, `ControlSetWidget`) are unchanged.

Operator decisions (from issue-review checkpoint):
- `ResponsiveHubLayout` is a **distinct class**, not inlined in `MarineControlPlugin`
- Old toolbar members and their slots are **explicitly removed** (no dead code)
- Hysteresis thresholds are **compile-time constants** in the layout controller header

## Approach

1. **Add `ConnectionsHubWidget`** (header + source) — Three-section scrollable widget:
   - *Local* section: one checkbox per `control_set_topics()` entry; excludes topics already connected via bridge (de-dup). Check → `TabManager::openTab`. Uncheck → `TabManager::closeTab`.
   - *Bridge* section: `QComboBox` from `bridge_nodes_from_services()`; changing it rebuilds the `BridgeControlClient` (passed back to `MarineControlPlugin`).
   - *Remote* section: one checkbox per `BridgeControlClient::availableDevices()`, grouped by remote. Check → `connect(device)` + `openTab`. Uncheck → `disconnect(device)` + `closeTab`.
   - Maintains a **desired set** (set of state topics) and reconciles on each `devicesChanged` callback — connect+open when a desired-but-not-yet-seen device appears.
   - Provides `onTabClosed(state_topic)` slot: unchecks the box and (if remote) disconnects.
   - Implements `saveSettings`/`restoreSettings`: checked set keyed by (bridge node + remote + state topic).

2. **Add `ResponsiveHubLayout`** (header + source) — `QWidget` subclass owning `ConnectionsHubWidget*`, `QTabWidget*`, and `QSplitter*`:
   - Overrides `resizeEvent`; debounces with a 200 ms one-shot `QTimer`.
   - Hysteresis constants `kHubPanelWidthHi = 860` and `kHubPanelWidthLo = 700` (pixels) in the header.
   - Wide (width > `kHubPanelWidthHi`): hub in `QSplitter` left pane; splitter shown; tab bar at top.
   - Narrow (width < `kHubPanelWidthLo`): hub reparented to `QTabWidget` as Tab 0 (non-closable); splitter hidden.
   - Preserves selected tab by state-topic key (via `TabManager::topicForIndex`) across reparent — not by index.
   - Persists splitter divider position via `saveSettings`/`restoreSettings`.
   - Exposes `tabWidget()` so `MarineControlPlugin` can pass it to `TabManager`.

3. **Rewire `MarineControlPlugin`** — Remove all superseded members and slots:
   - Delete: `topic_combo_`, `bridge_combo_`, `device_combo_`, `connect_button_`, `status_label_`, `manual_topic_`, `arg_topic_`.
   - Delete slots: `updateTopicList`, `selectTopic`, `onTopicChanged`, `updateBridgeList`, `onBridgeChanged`, `refreshDevices`, `onDeviceChanged`, `onConnectClicked`.
   - `initPlugin`: create `ResponsiveHubLayout`, pass its `tabWidget()` to `TabManager`; connect `QTabWidget::tabCloseRequested` → `hub->onTabClosed`.
   - `saveSettings`/`restoreSettings`: delegate to hub and layout.
   - Keep `shutdownPlugin`, `makeTransportFactory`, `RclcppTabTransport` unchanged.

4. **Update `CMakeLists.txt`** — Add `connections_hub_widget.{hpp,cpp}` and `responsive_hub_layout.{hpp,cpp}` to `${PROJECT_NAME}_HEADERS`/`${PROJECT_NAME}_SOURCES`. Add two new gtest targets (`test_connections_hub`, `test_responsive_hub_layout`) with `QT_QPA_PLATFORM=offscreen`.

5. **Add tests**:
   - `test_connections_hub.cpp`: (a) check↔tab sync — checking opens a tab, unchecking closes it; (b) local/bridged de-dup — a connected remote device's topic disappears from the local section; (c) desired-set reconcile — check a device before it appears in `availableDevices()`, then fire `devicesChanged` → tab opens automatically. All use `FakeTransport` + a stub `BridgeControlClient` (no live ROS node).
   - `test_responsive_hub_layout.cpp`: (a) state preservation across reparent — hub checkboxes survive wide↔narrow switch; (b) hysteresis — resizing to a width between `kHubPanelWidthLo` and `kHubPanelWidthHi` from either direction does not re-reparent.

## Files to Change

| File | Change |
|------|--------|
| `include/rqt_marine_control/connections_hub_widget.hpp` | **New** — hub widget API |
| `src/connections_hub_widget.cpp` | **New** — hub widget implementation |
| `include/rqt_marine_control/responsive_hub_layout.hpp` | **New** — responsive layout controller (constants, API) |
| `src/responsive_hub_layout.cpp` | **New** — responsive layout implementation |
| `include/rqt_marine_control/marine_control_plugin.hpp` | Remove old toolbar members/slots; add hub + layout pointers |
| `src/marine_control_plugin.cpp` | Rewire initPlugin/saveSettings/restoreSettings; remove superseded slots |
| `CMakeLists.txt` | Add new sources/headers; add two gtest targets |
| `test/test_connections_hub.cpp` | **New** — hub sync/dedup/reconcile tests |
| `test/test_responsive_hub_layout.cpp` | **New** — reparent state-preservation and hysteresis tests |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Only what's needed | No multi-bridge map, no manual pin override, no runtime-settable thresholds — all deferred as out-of-scope |
| Improve incrementally | Single PR; backend libraries untouched |
| Test what breaks | Tests cover the three non-obvious behaviors: dedup, desired-set reconcile, reparent-without-rebuild |
| A change includes its consequences | Dead members explicitly removed; saveSettings/restoreSettings updated to match new persisted keys |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0003 (bridgeable device control, D7-dyn) | Yes | Hub re-implements the D7-dyn connect/disconnect flow via the same `BridgeControlClient` API — no protocol changes |
| ADR-0008 (ROS 2 conventions) | Yes | Marshal-to-stable-QObject pattern for `devicesChanged` callback kept unchanged |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `MarineControlPlugin` toolbar removed | `saveSettings` key `"topic"` replaced by hub's checked-set keys | Yes — step 3 |
| New `QTabWidget` owner (`ResponsiveHubLayout`) | `TabManager` constructed with `layout.tabWidget()` not a raw `QTabWidget` | Yes — step 3 |
| `onTabCloseRequested` path changed | Must still call `disconnect(device)` for bridge devices | Yes — routed via `hub->onTabClosed` |

## Open Questions

- [ ] No open questions — plan is review-plan-ready.

## Estimated Scope

Single PR.
