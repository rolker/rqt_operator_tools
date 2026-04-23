# Work Plan: Issue #20 — rqt_camera_grid

**Issue**: [feat: rqt_camera_grid — multi-stream image grid with staleness border](https://github.com/rolker/rqt_operator_tools/issues/20)
**Branch**: `feature/issue-20`
**Repo**: `rolker/rqt_operator_tools` (default branch: `jazzy`)

## Summary

Add a new `rqt_camera_grid` package to this repo: an rqt plugin that displays N
`image_transport` streams in a configurable grid, with a per-pane staleness
border (neutral / amber / red) that matches `rqt_annunciator`'s dark-until-problem
convention. Secondary goal: declare `ffmpeg_image_transport` as `exec_depend` so
a fresh `rosdep install` on an operator station pulls the H.265 decoder needed
by the bizzyboat / izzyboat rollout
([unh_marine_perception#4](https://github.com/rolker/unh_marine_perception/issues/4),
[unh_echoboats_project11#78](https://github.com/rolker/unh_echoboats_project11/issues/78)).

## Principles and ADRs Considered

| Principle / ADR | How it applies |
|---|---|
| **Only what's needed** | Ship the core grid + staleness border + perspective save/restore. Defer click-to-expand, FPS limiter, and recording-status overlay to follow-up issues. |
| **Improve incrementally** | Single reviewable PR matching `rqt_annunciator`'s shape; nice-to-haves land later. |
| **Capture decisions** | Record the per-pane `{base, transport}` config decision and the auto-discovery-by-suffix UI rule in the package README's "Design" section — the rationale (ffmpeg-only deployments with no base topic) is non-obvious. |
| **A change includes its consequences** | Same PR updates repo-root `README.md` (new pkg + pre-existing missing `rqt_operator_log` entry), ships a default config, and adds unit tests at parity with `rqt_annunciator`. |
| **Test what breaks** | Unit-test the staleness state machine (neutral → warn → error → recovered) and config YAML roundtrip. GUI layout / letterbox left to manual acceptance. |
| **Workspace vs. project separation** | Plugin stays platform-agnostic — no bizzyboat defaults in code; default config is a 2×2 placeholder. |
| **ADR-0008 (ROS 2 conventions)** | `package.xml` format 3, `ament_python`, BSD-3-Clause, `plugin.xml` at package root, REP-144 naming. Mirrors `rqt_annunciator`. |
| **ADR-0009 (Python packaging)** | All deps via `package.xml` `exec_depend`; no pip. `ffmpeg_image_transport` and `image_transport` declared there. |

## Package Structure

```
rqt_operator_tools/
├── rqt_camera_grid/                              # NEW package
│   ├── package.xml                               # format 3, ament_python
│   ├── setup.py / setup.cfg
│   ├── resource/rqt_camera_grid                  # ament index marker
│   ├── plugin.xml                                # rqt plugin descriptor
│   ├── README.md                                 # design section + perspective schema
│   ├── config/default_camera_grid.yaml           # 2×2 placeholder
│   ├── rqt_camera_grid/
│   │   ├── __init__.py
│   │   ├── camera_grid_plugin.py                 # thin rqt wrapper
│   │   ├── camera_grid_widget.py                 # grid container, layout, perspective I/O
│   │   ├── camera_pane_widget.py                 # single pane: image + border + label
│   │   ├── staleness_tracker.py                  # pure state machine (testable w/o Qt)
│   │   ├── config_model.py                       # PaneConfig / GridConfig + YAML
│   │   └── config_dialog.py                      # grid dims + per-pane (base, transport) editor
│   └── test/
│       ├── test_config_model.py                  # yaml roundtrip, validation
│       ├── test_staleness.py                     # state machine transitions
│       └── test_copyright.py                     # ament_copyright
└── README.md                                     # UPDATED: list all three packages
```

## Design Decisions (promoted from issue comment)

### Per-pane config: `{base, transport}` struct, not a single topic string

```yaml
grid: { rows: 2, cols: 2 }
panes:
  - { base: /bizzy/sensors/cameras/oak_forward/image_raw,   transport: ffmpeg,     warn_s: 1.0, error_s: 3.0 }
  - { base: /bizzy/sensors/cameras/oak_starboard/image_raw, transport: ffmpeg,     warn_s: 1.0, error_s: 3.0 }
  - { base: /bizzy/sensors/cameras/oak_aft/segmentation,    transport: compressed, warn_s: 2.0, error_s: 6.0 }
  - { base: /some/usb_cam/image_raw,                        transport: raw,        warn_s: 1.0, error_s: 3.0 }
```

The plugin constructs `image_transport::Subscriber(base, queue, cb, transport_hint)`
per pane. The base topic itself may never be advertised (bizzyboat publishes only
the `/ffmpeg` sibling) — we rely on the fact that `image_transport::Subscriber`
builds the full topic name from `<base>/<suffix>` without checking the base.

Defaults for `warn_s` / `error_s`: if omitted, compute from first observed frame
rate (warn = 3× period, error = 10× period, per the issue); fall back to
`warn_s=1.0, error_s=3.0` until the first frame arrives.

### Staleness state machine (pure, testable)

```
                      frame arrives
  NEUTRAL <─────────────────────────── WARN / ERROR
      │  last-frame-age > warn_s           ▲
      ▼                                     │
    WARN ─── last-frame-age > error_s ──▶ ERROR
```

Implemented in `staleness_tracker.py` as a class taking `(warn_s, error_s, now_fn)`
with `mark_frame()` and `tick(now)` → level. The pane widget calls `tick()` from a
1 Hz `QTimer` and repaints the border on change.

### Config dialog UX

Dropdown-driven: enumerate all currently advertised topics whose **name** ends in
a known transport suffix (`/ffmpeg`, `/compressed`, `/compressedDepth`, `/theora`)
or whose **type** is `sensor_msgs/Image` / `sensor_msgs/CompressedImage` /
`ffmpeg_image_msgs/FFMPEGPacket`. Parse each into a `(base, transport)` pair for
the dropdown. Free-form fallback fields for deployments where the topic isn't
currently advertised.

Rationale lives in the package README — this is the load-bearing decision the
comment on #20 captured.

### Perspective save/restore

Follow `rqt_annunciator`'s pattern: YAML-serialize `GridConfig` and store via
`instance_settings.set_value('config_yaml', ...)`. Load the default from
`share/rqt_camera_grid/config/default_camera_grid.yaml` when no perspective
setting exists.

## Dependencies (package.xml)

```xml
<exec_depend>python_qt_binding</exec_depend>
<exec_depend>rclpy</exec_depend>
<exec_depend>rqt_gui</exec_depend>
<exec_depend>rqt_gui_py</exec_depend>
<exec_depend>sensor_msgs</exec_depend>
<exec_depend>image_transport</exec_depend>
<exec_depend>ffmpeg_image_transport</exec_depend>
<exec_depend>cv_bridge</exec_depend>
<exec_depend>python3-yaml</exec_depend>

<test_depend>ament_copyright</test_depend>
<test_depend>python3-pytest</test_depend>
```

`cv_bridge` converts the incoming `sensor_msgs/Image` → QImage. `image_transport`
and `ffmpeg_image_transport` are the Python-accessible subscription plumbing and
the H.265 plugin that motivates the secondary goal.

## Implementation Phases

### Phase 1 — This PR

1. **Scaffolding**: `package.xml`, `setup.py`, `setup.cfg`, `resource/`, `plugin.xml`, `README.md`.
2. **`config_model.py`**: `PaneConfig` and `GridConfig` dataclasses + YAML I/O; defaults.
3. **`staleness_tracker.py`**: pure state machine (no Qt import).
4. **`camera_pane_widget.py`**: QLabel-based image renderer with colored `QFrame` border; letterbox via `QImage.scaled(..., KeepAspectRatio)`; topic + rate label.
5. **`camera_grid_widget.py`**: `QGridLayout` container; creates panes from `GridConfig`; holds `image_transport` subscriptions; 1 Hz staleness tick; `get_config` / `load_config`.
6. **`camera_grid_plugin.py`**: rqt wrapper mirroring `annunciator_plugin.py` (perspective save/restore + `trigger_configuration`).
7. **`config_dialog.py`**: grid-dims spinboxes + pane table with dropdown-or-free-form `(base, transport)` fields.
8. **`config/default_camera_grid.yaml`**: 2×2 empty placeholder panes (so a fresh install shows borders + labels).
9. **Tests**: `test_config_model.py` (YAML roundtrip, missing-field validation), `test_staleness.py` (transitions + boundary ticks), `test_copyright.py`.
10. **Repo root `README.md`**: add `rqt_camera_grid` entry AND `rqt_operator_log` entry (pre-existing omission flagged in review).

### Phase 2 — Follow-up issues (file separately at PR time)

- Click-to-expand a pane to fill the grid.
- Per-pane FPS limiter (QElapsedTimer-based drop).
- Recording-status overlay coordinated with `rqt_operator_log`.
- Auto-threshold from observed frame rate (if manual defaults prove awkward).

## Files to Change

| File | Change |
|------|--------|
| `rqt_camera_grid/**` | New package (all files listed above) |
| `README.md` | Add `rqt_camera_grid` entry; add missing `rqt_operator_log` entry |
| `.agent/work-plans/PLAN_ISSUE-20.md` | This plan (first commit) |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| Add a new package | Repo root `README.md` | Yes |
| Add `ffmpeg_image_transport` rosdep | PR description verifies `rosdep install` resolves it on a fresh salmon shell | Yes (call out in PR) |
| Define perspective schema | Document schema in package `README.md` | Yes |
| Staleness threshold defaults | Documented in package `README.md` | Yes |

## Open Questions

1. **Image conversion for `FFMPEGPacket`**: `ffmpeg_image_transport` exposes a
   Python subscription that decodes into `sensor_msgs/Image`, or does it deliver
   `FFMPEGPacket` to the subscriber and require host-side decode in our widget?
   Needs a five-minute check against `ffmpeg_image_transport`'s Python API on
   jazzy before writing the subscription code — may affect `cv_bridge` usage.
2. **Default config content**: Ship an empty 2×2 placeholder, or ship a commented
   bizzyboat-style example (clearly tagged as "example, not applicable on every
   platform")? The "platform-agnostic" principle argues for empty; usability
   argues for a commented example.
3. **Rate label source**: Measure rate from incoming-frame timestamps in the
   pane, or subscribe to `/rosout` / reuse an existing rate-tracker utility?
   The self-measured approach is simpler and matches what operators actually
   care about (is *this display* keeping up); default to self-measured unless
   there's a reason not to.

## Estimated Scope

Single PR. Package size comparable to `rqt_annunciator` (~800-1200 LoC including
tests). Nice-to-haves deferred to separate issues keeps this reviewable.

---
**Authored-By**: `Claude Code Agent`
**Model**: `Claude Opus 4.7 (1M context)`
