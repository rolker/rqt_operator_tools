# rqt_camera_grid

rqt plugin that displays N `image_transport` camera streams in a configurable
grid, with a per-pane staleness border that matches the `rqt_annunciator`
dark-until-problem convention.

## Use

Load via rqt:

```bash
rqt --standalone rqt_camera_grid
```

or add the plugin from rqt's **Plugins → Robot Tools → Camera Grid** menu.

Click rqt's wrench icon on the plugin to open the config dialog; use
**Import YAML...** to load a config file, or edit panes in-place. OK applies
the config and rqt persists it in the current perspective.

## Configuration

### Schema

```yaml
grid: { rows: 2, cols: 2 }
panes:
  - { base: /my/camera/image_raw, transport: ffmpeg,     warn_s: 2.0, error_s: 5.0 }
  - { base: /my/other/image_raw,  transport: compressed, warn_s: 2.0, error_s: 5.0 }
  # ... one pane per cell, row-major
```

`panes` are assigned to grid cells row-major. If `len(panes) < rows*cols`,
trailing cells render as empty dark placeholders. If `len(panes) >
rows*cols`, excess panes are truncated at load time with a warning.

### Lifecycle

Three layers feed the widget's in-memory config; whichever was set most
recently wins:

1. **Shipped default** (`share/rqt_camera_grid/config/default_camera_grid.yaml`)
   — loaded at plugin start.
2. **rqt perspective** — loaded via `restoreSettings` on plugin start
   (overrides the shipped default); written via `saveSettings` on shutdown.
3. **Config dialog** — opens from rqt's wrench icon; OK replaces the
   widget's config atomically. Import/Export YAML read/write files on disk.

## Design

### Per-pane `{base, transport}` config model

Each pane is `{base, transport, warn_s, error_s}`. The plugin constructs an
`image_transport::Subscriber` from `(base, transport)`; `image_transport`
builds the full topic name as `<base>/<suffix>` (or just `<base>` for the
`raw` transport). The base topic itself does not need to be advertised —
some deployments publish only the `/ffmpeg` or `/compressed` sibling, with
no `image_raw` base. Free-form text entry in the config dialog supports
those cases.

### Subscription QoS

`rmw_qos_profile_sensor_data`. Matches the `rclcpp::SensorDataQoS()` used
by every image subscriber in this workspace — the default-QoS overload
would silently fail to match best-effort publishers.

### Staleness thresholds

Per-pane `warn_s` and `error_s` (seconds since last frame). Defaults are
`warn=2.0`, `error=5.0`, matching `camp/src/camp/helm_manager/helm_manager.h`
so operators see the same green/yellow/red boundaries across tools.

### Grid layout

Custom layout (not `QGridLayout`). Slack from aspect-ratio mismatch between
the widget and the camera images is pushed to the outer edges of the grid;
inner cell boundaries are shared (zero gap between panes). Edge cells
carry the outer padding; their image renders are anchored toward the grid
interior so staleness borders stay symmetric. Per-pane images whose aspect
differs from the grid-wide median letterbox within their cell.

Target cell aspect = median of per-pane observed image aspects, defaulting
to 16:9 until any frames arrive. Relayout triggers on widget resize and on
the first frame arrival per pane.

### Supported encodings

- `rgb8` — zero-copy `QImage` view of the `sensor_msgs::msg::Image` buffer.
- `bgr8` — `cv_bridge::toCvShare(msg, "rgb8")`, then wrap as `QImage`.
- `mono8` — `cv_bridge` automatic grayscale → RGB conversion.
- Other encodings (`16UC1`, `32FC1`, depth formats, unknown) — one-time
  `RCLCPP_WARN` per pane, render a dark placeholder. Staleness border
  stays operational; a misconfigured pane does not crash the grid.

### Stability

`rqt_image_view` is the structural reference (ament_cmake layout, plugin
wiring, Qt thread marshaling) but not a behavioral one — it has
field-observed crashes. This package follows six explicit stability rules:
snapshot-based topic discovery, reconfigure serialized on the Qt main
thread, signal-based callback marshaling with `ConstSharedPtr` payloads,
defensive paint slot, explicit shutdown order, and a lifecycle regression
test (`test_pane_lifecycle.cpp`) that hammers construct/destruct cycles
under `QT_QPA_PLATFORM=offscreen`.
