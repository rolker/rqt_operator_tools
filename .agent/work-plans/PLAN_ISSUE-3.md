# Work Plan: Issue #3 — Annunciator Panel

**Issue**: [Annunciator panel — dark-until-problem status indicators](https://github.com/rolker/rqt_operator_tools/issues/3)
**Branch**: `feature/issue-3`
**Repo**: `rolker/rqt_operator_tools` (default branch: `jazzy`)

## Summary

Create a dark-until-problem annunciator panel as both an rqt plugin and standalone
ROS 2 application. The panel displays system vitals (NTP sync, GPS fix, battery
voltage, network heartbeat) as a strip of status indicators that stay dim when
everything is OK and light up amber/red when attention is needed.

Target deployment: operator station wall display (50" 4K) alongside CAMP, rviz,
and camera views, but also usable as a docked rqt panel on a laptop.

## Principles and ADRs Considered

| Principle / ADR | How it applies |
|---|---|
| **Only what's needed** | Ship the core indicator widget first; defer advanced features (drag reorder, complex threshold expressions) to follow-up issues |
| **Improve incrementally** | Phase 1 delivers a working annunciator with diagnostics support and direct topic subscriptions; richer config UI is Phase 2 |
| **A change includes its consequences** | Package includes setup.cfg, plugin.xml, resource marker, example config, and basic tests |
| **Test what breaks** | Test threshold evaluation logic, stale detection, and config parsing — the failure modes that matter at sea |
| **Workspace vs. project separation** | This is project-specific UI, correctly in a project repo (rqt_operator_tools), not the workspace |
| **ADR-0008 (ROS 2 conventions)** | Use `ament_python` build type, standard package.xml format 3, REP-144 naming, BSD license header |
| **ADR-0009 (Python packaging)** | All dependencies via package.xml exec_depend; no pip installs |

## Package Structure

```
rqt_operator_tools/
├── .agent/work-plans/PLAN_ISSUE-3.md
├── package.xml                          # format 3, ament_python
├── setup.py                             # ament_python entry points
├── setup.cfg                            # ament_python install config
├── resource/rqt_operator_tools          # ament index marker
├── plugin.xml                           # rqt plugin descriptor
├── config/
│   └── default_annunciator.yaml         # example/default config
├── rqt_operator_tools/
│   ├── __init__.py
│   ├── annunciator_plugin.py            # rqt plugin wrapper (thin)
│   ├── annunciator_widget.py            # core widget (layout, subscription mgmt)
│   ├── annunciator_standalone.py        # standalone entry point
│   ├── indicator_widget.py              # single indicator (color, label, value)
│   ├── config_dialog.py                 # settings dialog for rqt
│   └── config_model.py                  # indicator config data model + YAML I/O
└── test/
    ├── test_config_model.py             # config parsing and validation
    ├── test_threshold.py                # threshold evaluation logic
    └── test_copyright.py                # ament_copyright
```

## Design Decisions

### Visual approach: dark-until-problem

| State | Appearance |
|---|---|
| OK | Dim gray text, subtle or no color bar — blends into dark background |
| WARN | Amber/yellow — draws attention |
| ERROR | Red, prominent — demands attention |
| STALE | Distinct gray with "---" value — data stopped arriving |

Each indicator shows: status color block + label + formatted value.

### Adaptive layout

The widget measures its own dimensions and selects layout:
- **Wide (aspect > 2:1)**: horizontal strip (QHBoxLayout)
- **Tall (aspect < 1:2)**: vertical column (QVBoxLayout)
- **Squarish**: grid (QGridLayout, columns computed from indicator count)

Font size auto-calculates from window height divided by the number of visible
rows, with a configurable scale factor. No hardcoded pixel sizes.

### Dual entry points

Widget logic lives in `AnnunciatorWidget`. Two thin wrappers:

1. **rqt plugin** (`annunciator_plugin.py`): subclasses `rqt_gui_py.plugin.Plugin`,
   calls `context.add_widget()`, implements `save_settings()`/`restore_settings()`
   for perspective persistence.

2. **Standalone** (`annunciator_standalone.py`): `ros2 run rqt_operator_tools annunciator`
   with `--config <path>` and optional `--always-on-top`. Creates its own
   `QApplication` and ROS node.

### Data sources

Two types of indicators, both configurable per-indicator in YAML:

1. **Diagnostics-based**: subscribes to `/diagnostics` (`diagnostic_msgs/msg/DiagnosticArray`),
   filters by `diagnostic_name`. Status level comes from the diagnostic message
   itself (OK/WARN/ERROR/STALE mapping).

2. **Direct topic**: subscribes to an arbitrary topic. Config specifies `msg_type`,
   `value_field` (dot-separated path into the message), `format` string, and
   `thresholds` (simple Python expressions evaluated with `value` in scope).

### Configuration

**rqt mode**:
- Settings dialog with UI for adding/removing/configuring indicators
- Add by browsing available `/diagnostics` entries or selecting ROS topics
- Set thresholds, format strings, labels
- Persisted via rqt's `save_settings()` / `restore_settings()` (QSettings)
- YAML import/export buttons in the settings dialog

**Standalone mode**:
- Config file path as CLI argument: `--config bizzyboat_annunciator.yaml`
- Falls back to `config/default_annunciator.yaml` from the package share directory

**YAML format** (from the issue):
```yaml
indicators:
  - name: Battery
    topic: /battery/voltage
    msg_type: std_msgs/msg/Float64
    value_field: data
    format: "{:.1f}V"
    thresholds:
      ok: "value > 12.5"
      warn: "value > 11.5"
      error: "value <= 11.5"

  - name: NTP Sync
    source: diagnostics
    diagnostic_name: "NTP offset"
    format: "{:.0f}ms"

  - name: GPS
    source: diagnostics
    diagnostic_name: "GPS fix"
```

### Stale detection

Each indicator tracks `last_update_time`. A configurable `stale_timeout` (default
5 seconds) triggers the STALE state when no message arrives in time. A periodic
Qt timer (1 Hz) checks for staleness across all indicators.

## Implementation Phases

### Phase 1 — Core (this PR)

1. **Package scaffolding**: `package.xml`, `setup.py`, `setup.cfg`, `resource/`,
   `plugin.xml`, `__init__.py`
2. **Config model** (`config_model.py`): dataclass for indicator config, YAML
   load/save, validation
3. **Indicator widget** (`indicator_widget.py`): single indicator with color bar,
   label, value; `set_status()` method that updates appearance
4. **Annunciator widget** (`annunciator_widget.py`): container that manages a list
   of `IndicatorWidget`s, handles adaptive layout on resize, subscribes to
   `/diagnostics` and direct topics, routes updates to indicators, runs stale timer
5. **rqt plugin** (`annunciator_plugin.py`): thin wrapper with
   `save_settings()`/`restore_settings()`
6. **Standalone wrapper** (`annunciator_standalone.py`): CLI entry point
7. **Config dialog** (`config_dialog.py`): basic settings dialog — list of
   indicators, add/remove, edit fields, YAML import/export
8. **Default config** (`config/default_annunciator.yaml`): NTP, GPS, Battery,
   Network heartbeat
9. **Tests**: config parsing, threshold evaluation, stale detection logic

### Phase 2 — Enhancements (follow-up issues)

- Drag-to-reorder indicators
- Topic/diagnostics browser in config dialog (discover available sources)
- Complex threshold expressions (beyond simple comparisons)
- Sound/notification on state transitions
- Per-indicator history sparkline

## Dependencies (package.xml)

```xml
<exec_depend>python_qt_binding</exec_depend>
<exec_depend>rclpy</exec_depend>
<exec_depend>rqt_gui</exec_depend>
<exec_depend>rqt_gui_py</exec_depend>
<exec_depend>diagnostic_msgs</exec_depend>
<exec_depend>std_msgs</exec_depend>
<exec_depend>rosidl_runtime_py</exec_depend>

<test_depend>ament_copyright</test_depend>
<test_depend>python3-pytest</test_depend>
```

`rosidl_runtime_py` is needed for `utilities.get_message()` to dynamically import
message types from string names (e.g., `"std_msgs/msg/Float64"`).

## Key Technical Details

### Dynamic message import

For direct-topic indicators, the message type is specified as a string in YAML.
Use `rosidl_runtime_py.utilities.get_message()` to resolve it at runtime:

```python
from rosidl_runtime_py.utilities import get_message
msg_class = get_message("std_msgs/msg/Float64")
```

### Value field extraction

`value_field` supports dot-separated paths: `"pose.position.x"` would extract
`msg.pose.position.x`. Implemented as a simple `getattr` chain.

### Threshold evaluation

For Phase 1, thresholds are simple Python expressions evaluated with `value` in
the local scope via a restricted evaluator (not raw `eval`). Use `ast.literal_eval`
for constants and a small safe expression evaluator for comparisons. This keeps
the attack surface minimal while supporting the documented YAML format.

### rqt plugin.xml

```xml
<library path="rqt_operator_tools">
  <class name="rqt_operator_tools/AnnunciatorPlugin"
         type="rqt_operator_tools.annunciator_plugin.AnnunciatorPlugin"
         base_class_type="rqt_gui_py::Plugin">
    <description>
      Dark-until-problem annunciator panel for operator station status monitoring.
    </description>
    <qtgui>
      <group>
        <label>Robot Tools</label>
      </group>
      <label>Annunciator Panel</label>
      <icon type="theme">dialog-warning</icon>
      <statustip>Status indicators that light up only when something needs attention.</statustip>
    </qtgui>
  </class>
</library>
```

### setup.py entry points

```python
entry_points={
    'console_scripts': [
        'annunciator = rqt_operator_tools.annunciator_standalone:main',
    ],
},
```

## Open Questions

1. **Threshold expression safety**: The issue specifies threshold expressions like
   `"value > 12.5"`. Phase 1 will use a restricted AST-based evaluator (comparison
   operators only, no function calls). Is this sufficient, or should we use a
   declarative format instead (e.g., `{operator: ">", value: 12.5}`)?

2. **Diagnostics key matching**: Should `diagnostic_name` match against the
   `DiagnosticStatus.name` field exactly, or support substring/regex matching?
   Exact match is simpler and more predictable; starting there.

3. **Multiple diagnostics topics**: Some systems publish diagnostics on namespaced
   topics (e.g., `/robot1/diagnostics`). Should we support configuring the
   diagnostics topic per indicator or globally? Global config with per-indicator
   override seems reasonable.

4. **Color scheme customization**: The dark-until-problem palette (dim gray / amber /
   red) is hardcoded in Phase 1. Should colors be configurable per-indicator or
   globally? Defer to Phase 2 unless needed sooner.

## Risk / Complexity Notes

- **Dynamic message types** add runtime failure modes (typo in msg_type, package
  not installed). Config validation should catch these at load time with clear
  error messages.
- **Qt thread safety**: ROS callbacks arrive on the rclpy executor thread; UI
  updates must happen on the Qt main thread. Use Qt signals to bridge the gap.
- **Stale detection** relies on wall-clock time, which is appropriate for operator
  display but should be documented.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus 4.6 (1M context)`
