# Operator Logbook rqt Plugin — Implementation Plan

**Issue**: [rqt_operator_tools#1](https://github.com/rolker/rqt_operator_tools/issues/1)
**Origin**: [rolker/unh_marine_autonomy#114](https://github.com/rolker/unh_marine_autonomy/issues/114)

## Context

During deployments the team maintains a running log in shared Word/Google Docs, disconnected from ROS bag data. This plugin co-locates operator observations with sensor data by publishing log entries as ROS topics and recording them to a local mcap bag. No existing ROS checklist or logbook plugin was found in the ecosystem.

## Repo & Package Structure

New repo `rqt_operator_tools` (GitHub: `rolker/rqt_operator_tools`, branch `jazzy`), added to `config/repos/ui.repos` in `unh_marine_autonomy`. Python rqt plugin using `ament_python` build type (bag I/O, PDF gen, image processing are all Python-heavy).

```
rqt_operator_tools/
└── rqt_operator_log/
    ├── package.xml
    ├── setup.py / setup.cfg
    ├── plugin.xml                    # base_class_type="rqt_gui_py::Plugin"
    ├── resource/rqt_operator_log     # ament index marker
    ├── config/
    │   └── default_events.yaml       # default system event config
    ├── rqt_operator_log/
    │   ├── __init__.py
    │   ├── operator_log_plugin.py    # rqt Plugin class
    │   ├── log_widget.py             # main UI (QTextBrowser timeline + text entry)
    │   ├── log_entry.py              # LogEntry dataclass
    │   ├── bag_manager.py            # rosbag2_py write/read
    │   ├── event_monitor.py          # system event subscriptions + state change detection
    │   ├── image_capture.py          # four capture methods
    │   ├── pdf_export.py             # reportlab PDF generation
    │   └── config_dialog.py          # system event config editor UI
    └── scripts/
        └── rqt_operator_log          # standalone launcher
```

## Phased Implementation

### Phase 1 — MVP: Text Logging with Bag Recording

**Goal**: Operator types entries, sees chronological timeline, entries published to ROS and recorded to mcap bag. Startup recovery from existing bag.

**Files**: `log_entry.py`, `bag_manager.py`, `log_widget.py`, `operator_log_plugin.py`, `plugin.xml`, `package.xml`, `setup.py`, `setup.cfg`

1. **`log_entry.py`** — `LogEntry` dataclass:
   - `timestamp_ns`, `entry_type` (operator_text | operator_image | system_event), `author`, `text`, `image_data` (bytes | None), `source_topic`

2. **`bag_manager.py`** — wraps `rosbag2_py`:
   - `open_new_bag(directory)` — creates `operator_log_YYYYMMDD_HHMMSS/` with mcap storage
   - `register_topic(name, msg_type_str)` — calls `writer.create_topic()`
   - `write(topic, serialized, timestamp_ns)` — thread-safe with `threading.Lock`
   - `read_log_entries(bag_path)` — `SequentialReader` to recover history (mcap supports concurrent read/write)
   - `close()` — clean shutdown
   - Bag directory: `~/operator_logs/` default, configurable via ROS param
   - One bag per session; on restart, read existing for recovery then start new bag

3. **`log_widget.py`** — main UI:
   - `QTextBrowser` (rich text, read-only) for timeline — entries as HTML blocks with timestamp/author
   - `QTextEdit` at bottom for composing, Submit button
   - Toolbar: author name, PDF export, settings
   - Author: prompted on first launch, stored in rqt instance settings, also via ROS param `~author`

4. **`operator_log_plugin.py`** — rqt Plugin class:
   - Creates node, `BagManager`, `LogWidget`
   - Publishes to `log/text` (`std_msgs/String`)
   - Thread safety: ROS callbacks append to `QMutex`-protected queue, `QTimer` (100ms) drains to widget (rqt_console pattern)
   - On startup: reads existing bag for recovery
   - `save_settings()` / `restore_settings()` for author, bag dir

5. **Packaging**: `plugin.xml` with `rqt_gui_py::Plugin`, `package.xml` with `ament_python`, standalone launcher script

**Reference files**:
- `/opt/ros/jazzy/lib/python3.12/site-packages/rqt_console/console.py` — Python rqt plugin pattern
- `/opt/ros/jazzy/share/rqt_console/plugin.xml` — plugin.xml format
- `/home/roland/project11/layers/main/sensors_ws/src/imagenex_deltat/nodes/deltat_to_bag.py` — rosbag2_py write example

### Phase 2 — System Event Monitoring

**Goal**: Subscribe to configurable topics, display events interleaved in timeline.

**Files**: `event_monitor.py`, `config_dialog.py`, `config/default_events.yaml`

1. **`event_monitor.py`**:
   - Loads YAML config, creates dynamic subscriptions via `get_message()` for type resolution
   - Two modes per topic:
     - **passthrough**: log every message (template-formatted)
     - **state_change**: track a dot-path field, log only on value change
   - Pushes `LogEntry` objects to shared queue
   - `update_config()` tears down/recreates subscriptions

2. **YAML config format**:
   ```yaml
   system_events:
     - topic: /helm_manager/status
       msg_type: std_msgs/msg/String
       mode: state_change
       field: data
       label: "Helm Mode"
     - topic: /mission_manager/mission/command
       msg_type: std_msgs/msg/String
       mode: passthrough
       label: "Mission Command"
   ```

3. **`config_dialog.py`**:
   - `QDialog` with table of event subscriptions (topic, type, mode, field, label)
   - Add/remove/edit rows, topic browser from `node.get_topic_names_and_types()`
   - Save to file, triggered via rqt's wrench icon (`trigger_configuration()`)

### Phase 3 — Image Capture

**Goal**: Four image capture methods, published to `log/image` topic.

**Files**: `image_capture.py`, updates to `log_widget.py`

1. **Four methods** (all return JPEG bytes):
   - **Clipboard paste**: `QApplication.clipboard().image()` → JPEG via `QBuffer`
   - **Screen region capture**: fullscreen transparent widget with rubber-band selection
   - **File attachment**: `QFileDialog` with image filters
   - **ROS topic snapshot**: one-shot subscription to `sensor_msgs/CompressedImage` or `Image` topic

2. **Image-text linking**: same timestamp on both topics. Text entry gets `[IMAGE:timestamp_ns] caption` prefix. On recovery, match by timestamp.

3. **Display**: images inline in `QTextBrowser` via `document().addResource()` + `<img>` tags, thumbnails in timeline, click for full size.

4. **Publishing**: `sensor_msgs/CompressedImage` with `format='jpeg'`, matching header timestamp.

### Phase 4 — PDF Export

**Goal**: Generate archival PDF of the log.

**Files**: `pdf_export.py`, updates to `log_widget.py`

1. Uses `reportlab.platypus` (`SimpleDocTemplate`, `Paragraph`, `Image`, `Spacer`)
2. Layout: A4, header with title/date, footer with page numbers
3. Entries: timestamp in gray, author bold, text body. System events italic/muted.
4. Images: scaled to page width, aspect ratio preserved
5. Filter toggle: include/exclude system events
6. Runs in `QThread` with progress dialog
7. Triggered from toolbar, `QFileDialog` for save path

### Phase 5 — Polish

- Keyboard shortcuts (Enter to submit)
- Search/filter in timeline (Ctrl+F)
- Timestamp format preference (absolute vs relative)
- Auto-scroll behavior

## Thread Safety

- ROS callbacks → `QMutex`-protected queue → `QTimer` (100ms) drains to GUI thread
- `BagManager.write()` uses `threading.Lock` (called from GUI and ROS threads)
- Image capture initiated from GUI thread; ROS topic snapshot uses one-shot subscription with `threading.Event`

## Dependencies

All available — no additional installs needed:
- `rclpy`, `rosbag2_py`, `rosidl_runtime_py`, `std_msgs`, `sensor_msgs`
- `python_qt_binding` (PyQt5)
- `rqt_gui`, `rqt_gui_py`
- `reportlab` 4.1.0 (system, confirmed installed)

## Verification

1. **Build**: `colcon build --packages-select rqt_operator_log` in ui_ws
2. **Launch standalone**: `ros2 run rqt_operator_log rqt_operator_log`
3. **Launch in rqt**: `rqt` → Plugins → Logging → Operator Log
4. **Test text entry**: type entry, verify it appears in timeline and on `log/text` topic
5. **Test bag recording**: check mcap file is created in `~/operator_logs/`, inspect with `ros2 bag info`
6. **Test recovery**: kill and restart plugin, verify previous entries load
7. **Test system events**: configure a system event, publish to that topic, verify it appears in timeline
8. **Test image capture**: test each of the four methods, verify on `log/image` topic
9. **Test PDF export**: export log, verify PDF contains entries and images
10. **Test in operator perspective**: add to rqt perspective alongside existing plugins

## ui.repos Update

Add to `config/repos/ui.repos` in `unh_marine_autonomy`:
```yaml
  rqt_operator_tools:
    type: git
    url: https://github.com/rolker/rqt_operator_tools.git
    version: jazzy
```
