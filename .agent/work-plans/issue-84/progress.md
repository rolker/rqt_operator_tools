---
issue: 84
---

# Issue #84 — rqt_boat_state: live control/power/environment gauge panel

## Issue Review
**Status**: complete
**When**: 2026-06-23 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #84
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Actions
- [ ] Add unit tests for the Qt-free data model — authority table resolution, staleness detection, and channel-map parsing are all testable without a display; the issue doesn't mention them explicitly.
- [ ] Ensure package structure follows ROS 2 conventions (ADR-0008): package.xml format 3, proper `<exec_depend>`/`<depend>` tags, SPDX license, and license headers in all source files matching the existing rqt_operator_tools convention.
- [ ] Document the ENU/NED + body/ground-velocity convention and the ArduRover default channel map in config or code comments — these are non-obvious constraints that could be accidentally overridden; a short note in the settings dialog or a config README is enough.
- [ ] Verify that `marine_interfaces/SoundSpeed` dependency is correct (the issue cites the message type but not the package name for the `<depend>` tag).

## Plan Authored
**Status**: complete
**When**: 2026-06-23 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-84/plan.md` at `c222d52`
**Branch**: feature/issue-84 at `c222d52`
**Phases**: single

### Open questions
- [ ] Should `rqt_boat_state` import `IndicatorLevel` from `rqt_annunciator`, or define its own color palette independently?
- [ ] `mavros/setpoint_velocity/cmd_vel` type: `geometry_msgs/Twist` or `geometry_msgs/TwistStamped`? Verify before wiring speed ghost needle.
- [ ] Confirm ENU/NED default for this vessel's odom topic — plan defaults to ENU (ROS standard).

## Plan Review
**Status**: complete
**When**: 2026-06-23 22:54 +00:00
**By**: Claude Code Agent (Claude Opus)
<!-- Independent: plan authored by (Claude Sonnet); this review is a fresh-context, different-model (Claude Opus) dispatch per the #490 handoff contract. Shared workspace agent-name makes the name-only self-review heuristic coarse; the model field disambiguates. -->

**Plan**: `.agent/work-plans/issue-84/plan.md` at `c222d52`
**PR**: PR-less (--issue mode)
**Verdict**: approve-with-suggestions

Verified against live workspace: `marine_interfaces/Helm` (throttle/rudder, −1..1) and
`marine_interfaces/SoundSpeed` (`sound_speed`) exist; `IndicatorLevel` reuse from
`rqt_annunciator/config_model.py` is real; all four review-issue action items are
addressed by the plan. `gh` was unauthenticated, so the issue body was taken from the
`## Issue Review` entry and the plan's Context.

### Findings
- [ ] (must-fix) Environment panel under-specified — "control/power/environment" scope has no environment gauge/widget, subscription, or water-temp message type; `SoundSpeed` is named only as a `TrendPlot` consumer. Add the widget+subscriptions or explicitly descope to V2. — `plan.md:36`, `plan.md:75`
- [ ] (suggestion) Gauge file count mismatch: step 4 describes 4 gauges, Files table says "gauges/ (5 files)". Reconcile (name the 5th). — `plan.md:75`
- [ ] (suggestion) License-header claim diverges from repo convention — zero `.py` files in `rqt_operator_tools` carry SPDX headers (license is in `package.xml`); per ADR-0008 §5 match existing or flag a deliberate change. — `plan.md:100`
- [ ] (suggestion) `TrendPlot` buffer math inconsistent: 7200 samples at 1/10 s = 12 min, not ≈2 h (2 h needs 1 Hz). Fix rate or count. — `plan.md:33`
- [ ] (suggestion) Consider phasing: ~20 new files in one PR; land Qt-free core (`config_model` + `trend_plot` + 3 tests) ahead of Qt widgets. — `plan.md` (scope)
- [ ] (suggestion) Resolve `cmd_vel` Twist-vs-TwistStamped open question early — it gates the speed ghost-needle wiring. — `plan.md:113`

### Next step
Lifecycle: **Plan Review** → **implement** → **review-code**. Verdict is
approve-with-suggestions: address finding 1 (environment panel scope) and amend the
plan inline before/at the start of implementation; remaining items are suggestions.

## Implementation
**Status**: complete
**When**: 2026-06-23 00:00 +00:00
**By**: Claude Opus

**Branch**: feature/issue-84 (implementation tip `2a5bd73`)
**Build**: `./ui_ws/build.sh rqt_boat_state` → finished, 1 package, no errors.
**Test**: `./ui_ws/test.sh rqt_boat_state` → **68 tests, 0 errors, 0 failures, 0 skipped**.

### What landed
New `rqt_boat_state` ament_python rqt plugin package, built in six incremental
commits (skeleton → Qt-free core+tests → gauges → UI integration):

- **Package skeleton** — `package.xml` (format 3, `BSD-3-Clause`, exec_depends per
  plan, `ament_copyright`+`python3-pytest` test_depends), `setup.py`/`setup.cfg`/
  `plugin.xml` ("Robot Tools" group)/`resource/` marker, all mirroring
  `rqt_annunciator`. Zero SPDX/per-file headers (matches repo convention, ADR-0008 §5).
- **Qt-free data model** (`config_model.py`) — locally duplicated 4-value
  `IndicatorLevel`; `AuthoritySource` enum + `resolve_authority(fcu_mode,
  piloting_mode)` table (DISARMED/RC/HOLD/AUTONOMY/JOYSTICK/STANDBY with label +
  color token); `BoatStateConfig` dataclass (odom default `/bizzy/odom`, ENU
  default, ArduRover channel map `{throttle:[0,1], steering:[2,3]}`, gauge ranges,
  per-source staleness) with full YAML round-trip; PWM µs→−1..1 clamp,
  ENU-yaw→compass `(90−yaw)mod360`, quaternion→yaw, knots, and
  staleness/RC-freshness/NaN helpers.
- **`trend_buffer.py`** — Qt-free ring buffer, default **720 slots = 2 h at
  1/10 s**, per-slot min/max, NaN-rejecting.
- **`trend_plot.py`** + **4 gauges** (`heading`, `speed`, `center_zero` shared by
  steering+throttle, `battery`) — all QPainter, no new deps.
- **`authority_banner.py`**, **`environment_panel.py`** (SoundSpeed→"—" on NaN +
  Temperature, both trended/stale-gated), **`boat_state_widget.py`** (all 10
  subscriptions, queued-signal hand-off, 1 Hz stale sweep), **`boat_state_plugin.py`**
  (YAML save/restore + `trigger_configuration`), **`config_dialog.py`** (all fields
  incl. environment topics, ENU/NED + ArduRover channel-map inline help).
- **Tests** (`test/`, 3 files, 68 tests) — authority table, channel-map clamp,
  conversions, config round-trip, ring-buffer wraparound/min-max/2 h sizing,
  staleness gating, RC-in freshness, SoundSpeed-NaN. All pure-Python (no Qt/display).

### Verification
- Build + tests pass (above). No uncrustify/lint failures surfaced.
- Every Qt widget smoke-imported and `.grab()`-painted headless (offscreen).
- Message field paths confirmed against real defs: `mavros_msgs/State.mode/.armed`,
  `RCOut.channels`/`RCIn.channels`, `sensor_msgs/BatteryState.voltage/.percentage/
  .current`, `Temperature.temperature`, and (from source) `marine_interfaces/
  Helm.throttle/.rudder` + `SoundSpeed.sound_speed`. `geometry_msgs/TwistStamped`
  confirmed for cmd_vel.

### Deviations from plan (all noted in plan.md Files table)
- **+`trend_buffer.py`** — factored the ring buffer into a Qt-free module so
  `test_trend_plot.py` runs without importing Qt (honors the "tests Qt-free"
  hard requirement); `trend_plot.py` is the thin QPainter shell over it.
- **+`boat_state_standalone.py`** — console entry point, mirroring
  `rqt_annunciator`'s `annunciator_standalone` (referenced from `setup.py`
  `console_scripts`).
- **Gauge file count** corrected to 5 in the plan (step 4's review note about the
  4-vs-5 mismatch): the 5th is the package `__init__.py` holding the shared palette.
- `marine_interfaces` is not built in this worktree (its owning `core_ws` is
  unbuilt here), so the live subscription path could not be exercised end-to-end;
  field names were verified directly against the `.msg` sources instead. The
  widget degrades gracefully (logs and creates no subs) if the message import
  fails. Qt-free tests are unaffected and all pass.

### Next step
Lifecycle: **implement** → **review-code**.
