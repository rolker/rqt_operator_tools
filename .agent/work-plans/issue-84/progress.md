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

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-23 23:51 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-84 at `3aea92f`
**Mode**: pre-push
**Depth**: Deep (reason: 2524 lines / 25 files, new package, operator safety display)
**Must-fix**: 2 | **Suggestions**: 5
**Round**: 1 | **Ship**: continue — 2 must-fix incl. one safety-display gap; both clear/mechanical, fast convergence expected

### Findings
- [x] (must-fix) Gauges render non-finite (NaN) data as live: NaN battery voltage → green OK lamp + "nan V"; NaN speed → full-deflection arc + "nan kn". Gate via `is_valid_measurement` (already used in environment_panel). — `gauges/battery_gauge.py:71` / `gauges/speed_gauge.py:67`
- [x] (must-fix) One missing message package disables ALL subscriptions: `_topic_specs` imports all msg pkgs together; the `except` aborts the whole loop. Import per-spec. (cross-pass A+B confirmed) — `boat_state_widget.py:123`
- [x] (suggestion) Stale sweep accesses child widget private attr `self._heading._heading`; add `mark_stale()`. (cross-pass A+B) — `boat_state_widget.py:270`
- [x] (suggestion) "2-hour window" is nominal only — `add_sample` runs per-message, no 10 s decimation; reconcile docstrings/plan or decimate. — `trend_buffer.py:11`
- [x] (suggestion) Corrupt/hand-edited config with non-int channel indices raises TypeError in `_on_rc_out`; validate element types on load. — `config_model.py:270`
- [x] (suggestion) Invalid channel-map JSON silently discarded in the dialog with no user feedback. — `config_dialog.py:136`
- [x] (suggestion) Static: unused `IndicatorLevel` import (F401); `TrendBuffer.range` shadows builtin (A003). Other 25 ament_flake8 D/I nits consistent with repo convention (sibling rqt_annunciator same; CI doesn't run flake8). — `test/test_config_model.py:6`

### Next step
Lifecycle: **Local Review** → **address-findings** (verdict is changes-requested) → re-run **review-code** → push / open PR → **triage-reviews**. The diff is not pushed until a pre-push review returns approved.

## Implementation
**Status**: complete
**When**: 2026-06-24 00:07 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-84 at `8704f7c`
**Addressed**: `## Local Review (Pre-Push)` (2026-06-23 23:51 +00:00, branch `feature/issue-84` at `3aea92f`) — all 7 open findings (2 must-fix, 5 suggestions).
**Commits**: `20399b8`, `8e43fc8`, `6feb530`, `b2a9f20`, `ebbfaa4`, `26b236e`, `8704f7c`

### Actions
- [x] (must-fix) Gate non-finite (NaN/inf) measurements in the battery and speed gauges via `is_valid_measurement` so NaN no longer renders as a live green-lamp "nan V" / full-deflection "nan kn" — `gauges/battery_gauge.py`, `gauges/speed_gauge.py` (`20399b8`)
- [x] (must-fix) Import each source's message type independently in `_topic_specs` (importlib per-spec, log-and-skip on failure) so one missing package — e.g. `marine_interfaces` unbuilt — disables only its own sources, not every subscription — `boat_state_widget.py` (`8e43fc8`)
- [x] (suggestion) Added `HeadingGauge.mark_stale()`; the 1 Hz stale sweep now calls it instead of poking the private `self._heading._heading` — `gauges/heading_gauge.py`, `boat_state_widget.py` (`6feb530`)
- [x] (suggestion) Reconciled the trend docstrings: the buffer is sized in samples, and the "≈2 h" figure is nominal (1 sample / 10 s) only — the panel pushes once per message, so the window tracks arrival rate — `trend_buffer.py`, `trend_plot.py` (`b2a9f20`)
- [x] (suggestion) `BoatStateConfig.from_dict` now coerces channel-map indices to int (dropping non-coercible ones) via `_coerce_channel_map`, so a corrupt/hand-edited config can't raise `TypeError` in `_on_rc_out` — `config_model.py` (`ebbfaa4`)
- [x] (suggestion) The config dialog warns the operator (`QMessageBox`) when the channel-map JSON is invalid and reuses the same `_coerce_channel_map` sanitizer instead of silently discarding the entry — `config_dialog.py` (`26b236e`)
- [x] (suggestion) Static: dropped the unused `IndicatorLevel` import (F401) and renamed `TrendBuffer.range` → `value_range` (A003 builtin shadow), updating both callers. The other ~25 ament_flake8 D/I nits are left as-is per the review (repo convention; CI doesn't run flake8) — `test/test_config_model.py`, `trend_buffer.py`, `trend_plot.py`, `test/test_trend_plot.py` (`8704f7c`)

**Sanity checks**: `python3 -m pytest test/` → **68 passed**; all changed modules `py_compile` clean; NaN-gating and channel-map coercion spot-checked directly. No findings deferred — every item was actionable on inspection.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 84 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-24 00:16 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-84 at `3552874`
**Mode**: pre-push
**Depth**: Deep (reason: new safety-relevant operator display, 2618 lines / 25 files, FCU/RC authority logic)
**Must-fix**: 3 | **Suggestions**: 4
**Round**: 2 | **Ship**: continue — round-1 findings all verified fixed, but Deep passes surfaced 3 new must-fixes incl. a "does it receive data at all" QoS concern; not converged.

Round-1 findings (2 must-fix, 5 suggestions) all verified genuinely addressed in code. pyflakes clean; pytest 68/68. New must-fixes are correctness/safety-display issues the prior round did not reach.

### Findings
- [x] (must-fix) All subscriptions use bare depth `10` (RELIABLE); mavros/sensor/odom publishers are typically BEST_EFFORT → silent no-match, dead panel. Use `qos_profile_sensor_data` for those sources (precedent: `rqt_sonar_waterfall`); validate against live topics — `boat_state_widget.py:162`
- [x] (must-fix) Steering/throttle + commanded markers never greyed when `rc_out`/`helm`/`cmd_vel` go stale; `CenterZeroGauge` has no `mark_stale()` and `_check_stale` skips it — frozen control values shown as live — `boat_state_widget.py:274` / `gauges/center_zero_gauge.py`
- [x] (must-fix) Heading not finiteness-gated in `_on_odom` (unlike round-1 battery/speed fix); NaN orientation renders "nan°" instead of "---" — `boat_state_widget.py:184`
- [x] (suggestion) `velocity_frame` ENU/NED selectable but never applied in code; selecting NED silently yields ENU math — wire or remove — `config_dialog.py:94` / `config_model.py:275`
- [x] (suggestion) `_setup_subscriptions` seeds `_last_update=now`, so not-yet-received sources read fresh for `stale_timeout` after startup/reload; seed to "never received" — `boat_state_widget.py:167`
- [x] (suggestion) `_handle_message` can write `_last_update` for a source torn down by a concurrent `load_config`; gate on `source in self._subscriptions` — `boat_state_widget.py:177`
- [x] (suggestion) `test_default_capacity_is_two_hours` is a tautology codifying a "2 h" claim the corrected docstring contradicts — retire or test real decimation — `test/test_trend_plot.py:11`

### Next step
Lifecycle: **Local Review** → **address-findings** (verdict is changes-requested) → re-run **review-code** → push / open PR → **triage-reviews**. The diff is not pushed until a pre-push review returns approved.

## Implementation
**Status**: complete
**When**: 2026-06-24 00:46 +00:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-84 at `474dd4d`
**Addressed**: `## Local Review (Pre-Push)` round 2 (2026-06-24 00:16 +00:00, branch `feature/issue-84` at `3552874`) — all 7 open findings (3 must-fix, 4 suggestions).
**Commits**: `a7c7181`, `5bdb443`, `0001f94`, `3338d8f`, `7534958`, `aa9fc00`, `474dd4d`

### Actions
- [x] (must-fix) Subscribe the BEST_EFFORT telemetry sources (odom + all mavros/* + sensor_msgs/*) with `qos_profile_sensor_data` so a RELIABLE/BEST_EFFORT mismatch no longer silently no-matches and deadens the panel; per-source QoS lives in the `_topic_specs` table, command/string sources (cmd_vel, helm, piloting_mode) keep depth-10 RELIABLE (precedent: `rqt_sonar_waterfall`) — `boat_state_widget.py` (`a7c7181`)
- [x] (must-fix) `CenterZeroGauge.mark_stale()`/`mark_commanded_stale()` and `SpeedGauge.mark_commanded_stale()` added; the 1 Hz sweep now greys the steering/throttle actuals on stale `rc_out`, their commanded ghosts on stale `helm`, and the speed ghost on stale `cmd_vel`, so a frozen control value can no longer read as live — `gauges/center_zero_gauge.py`, `gauges/speed_gauge.py`, `boat_state_widget.py` (`5bdb443`)
- [x] (must-fix) Finiteness-gate the heading in `_on_odom` (and the COG arrow) via `is_valid_measurement`, so a NaN/inf orientation blanks the compass to "---" instead of rendering "nan°" — `boat_state_widget.py` (`0001f94`)
- [x] (suggestion) Removed the dead `velocity_frame` ENU/NED selector (dataclass field, YAML round-trip, and dialog combo) — the mru_transform odom is always ENU and SOG is frame-invariant, so the toggle silently did nothing; `velocity_reference` (ground/body, which *is* applied) stays — `config_model.py`, `config_dialog.py`, `test/test_config_model.py` (`3338d8f`)
- [x] (suggestion) `_setup_subscriptions` now seeds `_last_update[name] = None` (never-received) instead of `now`, so a source that never publishes reads stale immediately rather than fresh for one `stale_timeout` after startup/reload — `boat_state_widget.py` (`7534958`)
- [x] (suggestion) `_handle_message` drops messages whose source is no longer in `_subscriptions`, so a callback queued before a `load_config` teardown can't resurrect a torn-down source in `_last_update` — `boat_state_widget.py` (`aa9fc00`)
- [x] (suggestion) Retired the `test_default_capacity_is_two_hours` tautology (which codified the contradicted "2 h" claim) in favour of `test_default_capacity_is_720_samples`, asserting the real sample-count contract — `test/test_trend_plot.py` (`474dd4d`)

**Sanity checks**: `python3 -m pytest test/` → **68 passed**; pyflakes clean on all changed modules; every changed module `py_compile` clean. No findings deferred — all 7 were actionable on inspection. Live QoS validation against running topics was not possible in this worktree (no live stack / `marine_interfaces` unbuilt); the BEST_EFFORT-vs-RELIABLE split follows the documented mavros/sensor convention and the `rqt_sonar_waterfall` precedent.

### Next step
Lifecycle: **Implementation** → **review-code** (re-review the fixes). Hand off to a fresh-context sub-agent:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 84 --skill review-code

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-24 01:07 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-84 at `c106ee4` (code tip `474dd4d`)
**Mode**: pre-push
**Depth**: Deep (reason: new safety-relevant operator display, 2721 lines / 25 files, FCU/RC authority logic)
**Must-fix**: 0 | **Suggestions**: 1
**Round**: 3 | **Ship**: recommended — round-1 (2 mf) and round-2 (3 mf) findings all verified fixed; two fresh Deep adversarial passes surfaced zero genuine must-fixes. Converged.

Round-2 findings (3 must-fix, 4 suggestions) all verified genuinely addressed in code: BEST_EFFORT `qos_profile_sensor_data` on telemetry sources, `CenterZeroGauge.mark_stale`/`mark_commanded_stale` + `SpeedGauge.mark_commanded_stale` wired into the 1 Hz sweep, heading finiteness-gated in `_on_odom`, dead `velocity_frame` removed, `_last_update` seeded `None`, torn-down-source guard in `_handle_message`. pyflakes clean; pytest 68/68. Two Deep adversarial passes (logic + systemic/lifecycle) returned two "must-fix" candidates, both dismissed on verification (see Findings).

### Findings
- [ ] (suggestion) `pwm_to_unit` returns `1.0` (full deflection) for a non-finite arg: `max(-1, min(1, NaN))` → `1.0`. Unreachable from the live path — `RCOut.channels` is `sequence<uint16>` (no NaN/inf), `pwm_center` is spinbox-bounded, `pwm_half_range<=0` is guarded — so a finiteness gate in `channel_unit_value`/`pwm_to_unit` is defensive consistency only (the public Qt-free API could be fed bad input by a test/caller). — `config_model.py:82`

**Dismissed (false positives, recorded so they don't resurface in triage):**
- Lens B flagged `_RELIABLE_DEPTH = 10` (a bare int) passed as `qos` to `create_subscription` as a TypeError/safety bug. **False positive** — rclpy's `_validate_qos_or_depth_parameter` accepts an `int` and returns `QoSProfile(depth=10)` (RELIABLE default); passing a depth int is idiomatic. — `boat_state_widget.py:53`
- The ~150 `ament_flake8` D-series (missing-docstring) / I-series (import-order) nits are repo convention — the sibling `rqt_annunciator` carries the same class/count and the package does not run `ament_flake8` in CI. The genuinely-bad statics (F401, A003) were already fixed in round 1; `pyflakes` is now clean. Not findings.

### Governance
- **Human-control transparency** — Pass (strong): the authority banner makes the active driver visible at all times; commanded overlays grey under stale RC; staleness/NaN gating blanks rather than showing frozen/garbage data as live.
- **Fail-safe display** — Pass: per-source 1 Hz staleness sweep greys every overlay on its own source's silence; non-finite telemetry blanks to `---`/`—`.
- **ADR-0008 (ROS 2 package conventions)** — Compliant: `package.xml` format 3, BSD-3-Clause, correct `exec_depend` tags (incl. `marine_interfaces`), `ament_python` + `rqt_gui` plugin export; no per-file SPDX headers, matching repo convention (documented deviation).

### Plan Adherence
Implementation matches `plan.md`; deviations (`trend_buffer.py` split out for Qt-free tests, `boat_state_standalone.py` entry point, gauge file-count reconciliation) are documented in the Implementation entries. No undisclosed scope creep.

### Summary
Round-3 re-review of the new `rqt_boat_state` safety panel. All round-1/round-2 must-fixes verified fixed; two fresh Deep adversarial passes found no genuine must-fix (both candidates dismissed on verification). One optional defensive-consistency suggestion remains, unreachable from the live ROS path. Tests 68/68, pyflakes clean. The diff is shippable.

### Recommended Actions
- [ ] (optional) Add a finiteness gate to `channel_unit_value`/`pwm_to_unit` for parity with the other gauge NaN gates — defensive only; not required before push.

### Next step
Lifecycle: **Local Review** (approved) → push / open PR → **triage-reviews**. Verdict is **approved** with 0 must-fix, so the diff is ready to push. Hand off to a fresh-context sub-agent after pushing:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 84 --skill triage-reviews

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-24 10:39 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-84 at `d44e1f6`
**Mode**: pre-push
**Depth**: Deep (reason: new safety-relevant operator display; 4 new feature commits since the round-3 approval reworked authority resolution, the trend buffer, and gauge rendering)
**Must-fix**: 0 | **Suggestions**: 2
**Round**: 4 | **Ship**: recommended — round-3 approved the base; the 4 follow-on feature commits add zero genuine must-fixes (the two adversarial passes' shared "must-fix" is unreachable from the live path). Converged.

Re-review of the 4 feature commits landed after the round-3 approval (`b351eb3` time-binned trends + battery health zones, `9cbc997` vertical throttle + knots scale, `c9b4f45` COG body-frame + heartbeat-sourced mode + UNKNOWN authority, `d44e1f6` voltage-only battery readout + zone-brightness marker). The branch advanced from `c9b4f45` to `d44e1f6` mid-review; review pinned at `d44e1f6`. Two fresh Deep adversarial passes (Lens A logic + Lens B systemic/safety) both converged on one candidate must-fix (trend out-of-order folding); verified unreachable from the live feed and downgraded. pytest **74/74**, pyflakes clean.

### Findings
- [ ] (suggestion) `TrendBuffer.add` folds a backward-timestamp sample (`bin_start < cur['t0']`) into the open bin rather than dropping it. Unreachable from the live path: `add_sample` reads `time.monotonic()` in the GUI thread, and `_handle_message` is serialized via the `_msg_received` Qt signal, so `t` is non-decreasing and no caller passes an explicit `t`. Defensive-consistency only (same class as round-3's `pwm_to_unit` finding); optional one-line guard `elif bin_start < self._cur['t0']: return`. — `trend_buffer.py:90`
- [ ] (suggestion) Vertical `CenterZeroGauge` hardcodes `top=36`/`bottom=h-32`; if a layout forced the widget below ~68 px the track/captions would clip. Mitigated by the 120 px minimum size hint (`_MIN_HINT_V`); cosmetic, not safety-relevant. — `gauges/center_zero_gauge.py:172`

**Dismissed (recorded so they don't resurface in triage):**
- Both adversarial passes flagged the trend out-of-order fold as **must-fix**. **Downgraded to suggestion** — unreachable from the live path (monotonic clock + single-GUI-thread serialized dispatch ⇒ non-decreasing `t`; production callers pass value only). Even if reached it perturbs a min/max excursion band, not a fail-unsafe live readout.
- `speed_arc_max` default keeps the value `5.0` but its unit changed m/s→knots, so a *saved* config would be reinterpreted. Not a finding: this is a brand-new, never-pushed package — there are no field configs to migrate, and 5 kn is a sensible USV default.

### Governance
- **Human-control transparency** — Pass (strengthened): the new `UNKNOWN` authority state reports the FCU regime (e.g. `GUIDED — ROS mode ?`) instead of mislabeling an empty/never-received piloting mode as `STANDBY`, removing a fail-unsafe "ROS is idle" claim while an armed boat moves under GUIDED. COG arrow recoloured teal so it no longer reads as a commanded heading.
- **Fail-safe display** — Pass: NaN/stale gating preserved across the reworked gauges; voltage-only battery readout is intentional (BMS percentage/current carry no useful data on this platform) and blanks to `-- V` + clears the trend marker when stale/invalid.
- **Message-contract correctness** — Pass: the `piloting_mode` migration from `std_msgs/String` to `marine_interfaces/Heartbeat` was verified against the live `Heartbeat.msg` (`std_msgs/Header header` + `KeyValue[] values`); the `for kv in msg.values: kv.key/kv.value` field path is correct and `marine_interfaces` is already an `exec_depend`.
- **ADR-0008 (ROS 2 package conventions)** — Compliant; `package.xml` unchanged by these commits.

### Plan Adherence
The follow-on commits are post-approval UI/behaviour refinements (binning, knots, vertical throttle, authority UNKNOWN, voltage-only battery) consistent with the plan's intent; no undisclosed scope creep. COG body-frame fix and the trend rework correct issues the earlier rounds' code carried.

### Summary
Round-4 re-review of the `rqt_boat_state` safety panel covering the 4 feature commits added after the round-3 approval. Two fresh Deep adversarial passes found one shared candidate must-fix (trend out-of-order folding), verified unreachable from the live ROS path and downgraded to a defensive suggestion. The `UNKNOWN`-vs-`STANDBY` authority change is a genuine fail-safe improvement; the Heartbeat field path is verified. Tests 74/74, pyflakes clean. The diff is shippable.

### Recommended Actions
- [ ] (optional) Guard `TrendBuffer.add` against backward-timestamp samples (`elif bin_start < self._cur['t0']: return`) for defensive parity — not required before push.
- [ ] (optional) Clamp the vertical `CenterZeroGauge` band math so a sub-min-hint height degrades gracefully rather than clipping.

### Next step
Lifecycle: **Local Review** (approved) → push / open PR → **triage-reviews**. Verdict is **approved** with 0 must-fix, so the diff is ready to push. Hand off to a fresh-context sub-agent after pushing:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 84 --skill triage-reviews

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-24 11:10 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-84 at `567e5e0`
**Mode**: pre-push
**Depth**: Deep (reason: safety-relevant operator display; the 2 new commits add a commanded-yaw-rate indicator and a steering-direction reversal that touch turn-direction semantics)
**Must-fix**: 0 | **Suggestions**: 4
**Round**: 5 | **Ship**: recommended — round-3/4 approved the base; the 2 follow-on commits (`cffad52`, `567e5e0`) add zero must-fixes. Two Deep adversarial passes converged; Lens A verified the steering/yaw-rate sign conventions end-to-end against `Helm.msg` + `helm_manager.cpp` (correct). Converged.

Re-review of the 2 commits landed after the round-4 approval (`cffad52` commanded-rotation secondary bar + battery zone-marker EMA + equal bar thickness + `cmd_rotation_max`; `567e5e0` `steering_reversed` flag flipping the displayed actual steering). pytest 74/74, pyflakes clean. The trend backward-timestamp guard recommended in round 4 was applied (`trend_buffer.py:92`).

### Findings
- [ ] (suggestion) `-rot / cmd_rotation_max` is unguarded; a hand-edited config with `cmd_rotation_max: 0` raises `ZeroDivisionError` in the `_on_cmd_vel` Qt slot. Config-reachable only (dialog enforces a 0.1 min; not from message data). Module precedent: `pwm_to_unit` guards `half_range<=0`. Cross-pass A+B confirmed. Recommended before push (trivial). — `boat_state_widget.py:271` / `config_model.py:390`
- [ ] (suggestion) A non-finite `angular.z` (finite `linear`) skips `set_secondary`, so the cmd-rotation marker freezes at full non-greyed amber while `cmd_vel` stays fresh — frozen command shown as live. Same latent gap as the speed-commanded overlay. Grey/clear on the invalid branch. Cross-pass A+B. — `boat_state_widget.py:270`
- [ ] (suggestion) No tests cover the new behavior: `steering_reversed`/`cmd_rotation_max` round-trip not asserted (`test_yaml_roundtrip` field-checks specific keys), nor secondary-staleness, steering reversal, battery EMA, or the trend drop-guard. New fields are Qt-free/testable (issue-review action #1). — `test/test_config_model.py`
- [ ] (suggestion, carried from round 4) Fixed `_BAR_THICKNESS=24` (horiz) + hardcoded `top=36`/`bottom=h-32` (vert) can clip below min-hint heights; mitigated by min size hints (throttle's 120px drives the shared row). Cosmetic. — `gauges/center_zero_gauge.py:143` / `:221`

**Dismissed (recorded so they don't resurface in triage):**
- Direction/sign correctness flagged for cold inspection — **not a finding**. Verified end-to-end against `marine_interfaces/Helm.msg` (`rudder` = full-left..full-right) and `helm_manager.cpp` (`angular.z = -rudder*max_yaw_speed`): the flipped rc_out actual, the un-flipped helm rudder marker, and the negated cmd-rotation all agree in direction. No wrong-way indication.

### Governance
- **Human-control transparency** — Pass (strengthened): the commanded-yaw-rate bar adds autonomy-intent visibility (greys on `cmd_vel` staleness); `steering_reversed` makes the actual bar match the physical turn.
- **Fail-safe display** — Pass: the battery EMA feeds only the cosmetic zone-split marker; the warn/critical lamp uses raw instantaneous voltage (verified), so smoothing can't delay a low-voltage alarm. NaN/stale gating preserved (suggestion #2 is the one residual edge).
- **Message-contract correctness** — Pass: steering/yaw-rate sign conventions verified against source (above).
- **ADR-0008** — Compliant; `package.xml` unchanged by these commits.

### Plan Adherence
Post-approval UI/behaviour refinements consistent with the plan's intent; no scope creep. `steering_reversed=True` default is a BizzyBoat-specific calibration consistent with the package's other platform defaults (`/bizzy/odom`, ArduRover channel map) and documented in a code comment.

### Summary
Round-5 re-review of the 2 commits after the round-4 approval. Two Deep adversarial passes converged with no must-fix; Lens A traced the turn-direction conventions to source and confirmed them. Three actionable suggestions (cross-pass div-by-zero with module precedent, non-finite-command display edge, test-coverage gap) plus one carried cosmetic note. Tests 74/74, pyflakes clean. The diff is shippable.

### Recommended Actions
- [ ] (recommended before push — trivial) Guard `cmd_rotation_max` against zero (clamp in `from_dict` or `if > 0` before the division).
- [ ] (suggestion) Grey/clear the cmd-rotation secondary on a non-finite `angular.z`.
- [ ] (suggestion) Add round-trip assertions for the two new config fields and a secondary-staleness test.
- [ ] (optional, carried) Clamp the gauge band math so sub-min-hint heights degrade gracefully.

### Next step
Lifecycle: **Local Review** (approved) → push / open PR → **triage-reviews**. Verdict is **approved** with 0 must-fix, so the diff is ready to push. The 4 suggestions are non-blocking (the operator may apply the trivial div-by-zero guard first). Hand off to a fresh-context sub-agent after pushing:

    .agent/scripts/dispatch_subagent.sh --mode in-process --issue 84 --skill triage-reviews
