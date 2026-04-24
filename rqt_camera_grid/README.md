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

## Demo

For hands-on testing without boat hardware, the package ships a demo
launch file that fans a local webcam into six visually distinct streams
and a matching 2×3 grid config:

```bash
ros2 launch rqt_camera_grid demo_webcam_grid.launch.py
```

The launch starts `v4l2_camera` on `/dev/video0` (override with
`video_device:=/dev/videoN`) and five `demo_transform.py` instances,
producing:

| Source | Pane topic                       | Transform        |
|--------|----------------------------------|------------------|
| 1      | `/demo/webcam/image_raw`         | raw webcam       |
| 2      | `/demo/flip_h/image_raw`         | horizontal flip  |
| 3      | `/demo/grayscale/image_raw`      | grayscale → rgb8 |
| 4      | `/demo/crop_left/image_raw`      | left half only   |
| 5      | `/demo/crop_right/image_raw`     | right half only  |
| 6      | `/demo/negative/image_raw`       | color negative   |

In a separate terminal, launch rqt and load the matching config:

```bash
rqt --force-discover        # first-time: ensures Camera Grid is listed
# Plugins -> Visualization -> Camera Grid
# wrench icon -> Import YAML...
#   -> <install-space>/share/rqt_camera_grid/config/demo_webcam_grid.yaml
```

The cropped panes have different aspect ratios than the full-frame
panes, which exercises the aspect-aware layout's letterboxing.

Additional transforms available via the `transform` parameter on
`demo_transform.py`: `flip_v`, `blur`, `edge`, `none` — edit the
launch file or YAML to substitute them.

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

`rmw_qos_profile_sensor_data`. Image publishers typically use best-effort
sensor-data QoS; the default-QoS overload (reliable, keep-last-1) would
silently fail to match those publishers and deliver no frames.

### Staleness thresholds

Per-pane `warn_s` and `error_s` (seconds since last frame). Defaults are
`warn=2.0`, `error=5.0`, chosen for consistency with the green/yellow/red
staleness boundaries operators see elsewhere on the operator station.

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

- `rgb8` — wrap the `sensor_msgs::msg::Image` buffer in a temporary `QImage` view, then deep-copy so the pixels outlive the `ConstSharedPtr`. Single-copy (skips the encoding conversion path), not zero-copy.
- `bgr8` — `cv_bridge::toCvCopy(msg, "rgb8")`, then wrap as `QImage`. Note that `toCvShare` would not help here — any encoding conversion (bgr8→rgb8, mono8→rgb8) necessarily copies.
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
