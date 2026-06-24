"""Qt-free data model for the boat-state panel.

Everything that the unit tests exercise lives here so they run without a
display: the indicator palette enum, the command-authority resolution table,
the RC channel-map math, the staleness helpers, and the ``BoatStateConfig``
dataclass with YAML round-tripping for ``instance_settings``.

``IndicatorLevel`` is duplicated from ``rqt_annunciator`` on purpose — it is a
trivial four-value enum and copying it avoids a runtime coupling between the
two plugins.  A shared status-palette module is a V2 consolidation candidate.
"""

import math
from dataclasses import dataclass, field
from enum import Enum

import yaml


class IndicatorLevel(Enum):
    """Status level for a readout (duplicated from rqt_annunciator)."""
    OK = 0
    WARN = 1
    ERROR = 2
    STALE = 3


class AuthoritySource(Enum):
    """Who is currently commanding the vessel."""
    AUTONOMY = 'autonomy'
    JOYSTICK = 'joystick'
    STANDBY = 'standby'
    RC = 'rc'
    HOLD = 'hold'
    DISARMED = 'disarmed'
    UNKNOWN = 'unknown'   # FCU regime known, ROS arbitration layer not (yet) seen


# Mathematical / unit conversions -------------------------------------------

#: Metres-per-second to knots.
MPS_TO_KNOTS = 1.943844


def mps_to_knots(mps: float) -> float:
    """Convert a speed in m/s to knots."""
    return mps * MPS_TO_KNOTS


def quaternion_to_yaw(x: float, y: float, z: float, w: float) -> float:
    """Yaw (rotation about Z), in radians, from a quaternion.

    Follows REP-103 (right-handed, Z up): the returned angle is the heading
    in the frame the quaternion is expressed in (ENU for ``mru_transform``
    odom), measured counter-clockwise from the frame's +X (East) axis.
    """
    siny_cosp = 2.0 * (w * z + x * y)
    cosy_cosp = 1.0 - 2.0 * (y * y + z * z)
    return math.atan2(siny_cosp, cosy_cosp)


def enu_yaw_to_compass(yaw_rad: float) -> float:
    """Convert an ENU yaw (rad, CCW from East) to a compass heading (deg).

    Compass heading is clockwise from North, so ``heading = (90 - yaw) mod
    360`` once yaw is in degrees.
    """
    yaw_deg = math.degrees(yaw_rad)
    return (90.0 - yaw_deg) % 360.0


def pwm_to_unit(microseconds: float,
                center: float = 1500.0,
                half_range: float = 500.0) -> float:
    """Map an RC PWM pulse width (µs) to a normalized −1..1 command.

    ``center`` is the neutral pulse width (1500 µs by convention) and
    ``half_range`` the µs swing to full deflection (500 µs → 1000/2000 µs
    endpoints).  The result is clamped to ``[-1, 1]`` so an out-of-band
    pulse never drives a gauge past its end stop.  A non-positive
    ``half_range`` is treated as "no signal" and returns ``0.0``.
    """
    if half_range <= 0.0:
        return 0.0
    value = (microseconds - center) / half_range
    return max(-1.0, min(1.0, value))


# Command-authority resolution ----------------------------------------------

@dataclass(frozen=True)
class AuthorityState:
    """Resolved command authority: who drives + how to render the banner."""

    source: AuthoritySource
    label: str
    color_token: str


# FCU modes (ArduRover / mavros ``State.mode``) that hand authority to the
# helm-manager command chain rather than to the RC transmitter or a hold.
_OFFBOARD_FCU_MODES = frozenset({
    'GUIDED', 'AUTO', 'OFFBOARD', 'LOITER',
})

# helm-manager ``piloting_mode`` strings grouped by who they hand control to.
_AUTONOMY_PILOTING = frozenset({'autonomous', 'auto', 'autonomy', 'survey'})
_JOYSTICK_PILOTING = frozenset({'joystick', 'manual', 'teleop', 'gamepad'})
# NB: '' (never received) is deliberately NOT standby — see resolve_authority.
_STANDBY_PILOTING = frozenset({'standby', 'idle', 'hold'})


def resolve_authority(fcu_mode: str, piloting_mode: str) -> AuthorityState:
    """Resolve the active command authority from FCU + helm-manager modes.

    The FCU mode (``mavros_msgs/State.mode``) decides the broad regime:

    - empty / ``DISARMED`` → :class:`AuthoritySource.DISARMED`
    - ``MANUAL`` → :class:`AuthoritySource.RC` (the transmitter drives directly)
    - ``HOLD`` → :class:`AuthoritySource.HOLD`
    - any offboard-accepting mode (GUIDED/AUTO/…) → the helm-manager
      ``piloting_mode`` decides between AUTONOMY / JOYSTICK / STANDBY.

    An unknown FCU mode falls through to the piloting-mode arm so a new FCU
    mode string still resolves to *who* the helm manager says is driving
    rather than to a hard error.

    When ``piloting_mode`` is empty (never received / stale) under an
    offboard-accepting FCU mode, the result is :class:`AuthoritySource.UNKNOWN`
    reporting the FCU mode — NOT STANDBY. STANDBY means "ROS is deliberately
    idle"; claiming it while an armed boat moves under GUIDED would be a
    fail-unsafe display, so we only assert STANDBY on an explicit standby
    ``piloting_mode``.
    """
    fcu = (fcu_mode or '').strip().upper()
    pilot = (piloting_mode or '').strip().lower()

    if fcu in ('', 'DISARMED'):
        return AuthorityState(AuthoritySource.DISARMED, 'DISARMED', 'disarmed')
    if fcu == 'MANUAL':
        return AuthorityState(AuthoritySource.RC, 'RC / MANUAL', 'rc')
    if fcu == 'HOLD':
        return AuthorityState(AuthoritySource.HOLD, 'HOLD', 'hold')

    # Offboard-accepting (or unrecognized) FCU mode: the helm manager owns
    # the distinction between autonomy, joystick, and standby.
    if pilot in _AUTONOMY_PILOTING:
        return AuthorityState(AuthoritySource.AUTONOMY, 'AUTONOMY', 'autonomy')
    if pilot in _JOYSTICK_PILOTING:
        return AuthorityState(AuthoritySource.JOYSTICK, 'JOYSTICK', 'joystick')
    if pilot in _STANDBY_PILOTING:
        return AuthorityState(AuthoritySource.STANDBY, 'STANDBY', 'standby')
    # piloting_mode empty (never received) or unrecognized: report the FCU
    # regime and flag the ROS layer unknown rather than mislabeling it STANDBY.
    if pilot == '':
        return AuthorityState(AuthoritySource.UNKNOWN,
                              f'{fcu} — ROS mode ?', 'unknown')
    return AuthorityState(AuthoritySource.UNKNOWN,
                          f'{fcu} / {pilot.upper()}?', 'unknown')


# Staleness helpers ----------------------------------------------------------

def is_stale(age: float, timeout: float) -> bool:
    """True when data of *age* seconds has exceeded its *timeout*.

    A non-positive *timeout* disables staleness gating (never stale).
    """
    if timeout <= 0.0:
        return False
    return age > timeout


def level_for_age(age: float, warn_timeout: float,
                  error_timeout: float = 0.0) -> IndicatorLevel:
    """Map a data age to OK / WARN / ERROR by staleness.

    Exceeding *error_timeout* (default 3× *warn_timeout* when left at 0)
    yields ERROR; exceeding *warn_timeout* yields WARN; otherwise OK.
    """
    err = error_timeout if error_timeout > 0.0 else warn_timeout * 3.0
    if warn_timeout > 0.0 and age > err:
        return IndicatorLevel.ERROR
    if warn_timeout > 0.0 and age > warn_timeout:
        return IndicatorLevel.WARN
    return IndicatorLevel.OK


def should_grey_commanded(source: AuthoritySource,
                          rc_in_age: float,
                          rc_in_timeout: float) -> bool:
    """Whether commanded markers should be greyed for lost RC input.

    When the transmitter is the authority (``RC``) but the ``rc/in`` stream
    has gone stale, the commanded overlay no longer reflects a live operator
    input and must be greyed out.  Other authorities are unaffected.
    """
    if source is not AuthoritySource.RC:
        return False
    return is_stale(rc_in_age, rc_in_timeout)


def is_valid_measurement(value) -> bool:
    """True when *value* is a finite number (not None / NaN / inf)."""
    if value is None:
        return False
    try:
        return math.isfinite(float(value))
    except (TypeError, ValueError):
        return False


def format_measurement(value, fmt: str = '{:.1f}', nan_text: str = '—') -> str:
    """Format *value*, returning *nan_text* ("—") for NaN/None/non-finite.

    Used by the environment readouts so a NaN ``SoundSpeed`` shows a dash
    rather than ``nan``.
    """
    if not is_valid_measurement(value):
        return nan_text
    try:
        return fmt.format(float(value))
    except (ValueError, TypeError, IndexError, KeyError):
        return str(value)


# Configuration dataclass ----------------------------------------------------

def _default_channel_map() -> dict:
    """Default ArduRover RC-out channel map (SERVO1→idx 0, SERVO3→idx 2)."""
    return {'throttle': [0, 1], 'steering': [2, 3]}


def _default_stale_timeouts() -> dict:
    """Per-source staleness timeouts (s); falls back to ``stale_timeout``."""
    return {}


def _coerce_channel_map(raw: dict) -> dict:
    """Sanitize a loaded channel map to ``{name: [int, ...]}``.

    A hand-edited or corrupt config may carry non-integer indices (floats,
    strings, nulls).  Each index is coerced to ``int`` where possible and
    dropped otherwise, so the downstream ``channels[i]`` lookups never raise a
    ``TypeError`` on bad input.
    """
    coerced = {}
    for name, indices in raw.items():
        if not isinstance(indices, (list, tuple)):
            continue
        clean = []
        for i in indices:
            try:
                clean.append(int(i))
            except (TypeError, ValueError):
                continue
        coerced[str(name)] = clean
    return coerced


@dataclass
class BoatStateConfig:
    """All operator-tunable settings for the boat-state panel.

    Topic names default to the BizzyBoat stack; the velocity convention,
    channel map, gauge ranges, and per-source staleness timeouts are all
    overridable from the settings dialog and serialized to ``instance_settings``.
    """

    # Topics.
    odom_topic: str = '/bizzy/odom'
    cmd_vel_topic: str = 'cmd_vel'
    helm_topic: str = 'helm'
    fcu_state_topic: str = 'mavros/state'
    # The piloting mode is read from the helm-manager Heartbeat's KeyValue list
    # (key 'piloting_mode'), not a standalone String topic.
    heartbeat_topic: str = 'marine/heartbeat'
    rc_out_topic: str = 'mavros/rc/out'
    rc_in_topic: str = 'mavros/rc/in'
    battery_topic: str = 'mavros/battery'
    sound_speed_topic: str = 'sound_speed/sound_speed'
    water_temp_topic: str = 'sound_speed/temperature'

    # RC channel map (RC-out index lists) and PWM→unit calibration.
    rc_channel_map: dict = field(default_factory=_default_channel_map)
    pwm_center: float = 1500.0
    pwm_half_range: float = 500.0
    # BizzyBoat's steering servo drives a PORT turn at HIGH PWM, so the raw
    # PWM→unit (high = +stbd) reads mirrored vs. the actual turn; flip it so the
    # steering bar matches the boat's response (verified against rc/out + odom).
    steering_reversed: bool = True

    # (No velocity-frame selector: nav_msgs/Odometry twist is always in
    # child_frame_id (base_link) per REP-103, so COG is always computed by
    # rotating the body-frame velocity into ENU.  A 'ground' option was a
    # footgun that rendered a straight-ahead boat's COG due East.)

    # Gauge ranges / gates.
    cog_min_speed: float = 0.5     # m/s below which COG is not shown
    speed_arc_max: float = 5.0     # KNOTS at the end of the speed arc
    cmd_rotation_max: float = 1.0  # rad/s full-scale for the commanded-rotation bar
    battery_warn_v: float = 23.5
    battery_critical_v: float = 22.0

    # Staleness.
    stale_timeout: float = 2.0  # default per-source warn timeout (s)
    stale_timeouts: dict = field(default_factory=_default_stale_timeouts)

    def stale_timeout_for(self, source: str) -> float:
        """Warn timeout (s) for *source*, falling back to ``stale_timeout``."""
        return self.stale_timeouts.get(source, self.stale_timeout)

    def channel_unit_value(self, channels, name: str):
        """Mean −1..1 command across the RC channels mapped to *name*.

        *channels* is a sequence of PWM pulse widths (µs), e.g.
        ``RCOut.channels``.  Indices that fall outside the sequence are
        skipped; returns ``None`` when no mapped channel is available.
        """
        indices = self.rc_channel_map.get(name, [])
        values = [
            pwm_to_unit(channels[i], self.pwm_center, self.pwm_half_range)
            for i in indices
            if 0 <= i < len(channels)
        ]
        if not values:
            return None
        return sum(values) / len(values)

    def to_dict(self) -> dict:
        """Serialize to a plain dict for YAML output."""
        return {
            'topics': {
                'odom': self.odom_topic,
                'cmd_vel': self.cmd_vel_topic,
                'helm': self.helm_topic,
                'fcu_state': self.fcu_state_topic,
                'heartbeat': self.heartbeat_topic,
                'rc_out': self.rc_out_topic,
                'rc_in': self.rc_in_topic,
                'battery': self.battery_topic,
                'sound_speed': self.sound_speed_topic,
                'water_temp': self.water_temp_topic,
            },
            'rc_channel_map': {k: list(v) for k, v in self.rc_channel_map.items()},
            'pwm_center': self.pwm_center,
            'pwm_half_range': self.pwm_half_range,
            'steering_reversed': self.steering_reversed,
            'cog_min_speed': self.cog_min_speed,
            'speed_arc_max': self.speed_arc_max,
            'cmd_rotation_max': self.cmd_rotation_max,
            'battery_warn_v': self.battery_warn_v,
            'battery_critical_v': self.battery_critical_v,
            'stale_timeout': self.stale_timeout,
            'stale_timeouts': dict(self.stale_timeouts),
        }

    @classmethod
    def from_dict(cls, d: dict) -> 'BoatStateConfig':
        """Deserialize from a YAML-loaded dict (missing keys keep defaults)."""
        d = d or {}
        topics = d.get('topics', {})
        defaults = cls()
        channel_map = d.get('rc_channel_map')
        if not isinstance(channel_map, dict):
            channel_map = _default_channel_map()
        else:
            channel_map = _coerce_channel_map(channel_map)
        stale_timeouts = d.get('stale_timeouts')
        if not isinstance(stale_timeouts, dict):
            stale_timeouts = {}
        return cls(
            odom_topic=topics.get('odom', defaults.odom_topic),
            cmd_vel_topic=topics.get('cmd_vel', defaults.cmd_vel_topic),
            helm_topic=topics.get('helm', defaults.helm_topic),
            fcu_state_topic=topics.get('fcu_state', defaults.fcu_state_topic),
            heartbeat_topic=topics.get(
                'heartbeat', defaults.heartbeat_topic),
            rc_out_topic=topics.get('rc_out', defaults.rc_out_topic),
            rc_in_topic=topics.get('rc_in', defaults.rc_in_topic),
            battery_topic=topics.get('battery', defaults.battery_topic),
            sound_speed_topic=topics.get('sound_speed', defaults.sound_speed_topic),
            water_temp_topic=topics.get('water_temp', defaults.water_temp_topic),
            rc_channel_map=channel_map,
            pwm_center=d.get('pwm_center', defaults.pwm_center),
            pwm_half_range=d.get('pwm_half_range', defaults.pwm_half_range),
            steering_reversed=d.get('steering_reversed', defaults.steering_reversed),
            cog_min_speed=d.get('cog_min_speed', defaults.cog_min_speed),
            speed_arc_max=d.get('speed_arc_max', defaults.speed_arc_max),
            cmd_rotation_max=d.get('cmd_rotation_max', defaults.cmd_rotation_max),
            battery_warn_v=d.get('battery_warn_v', defaults.battery_warn_v),
            battery_critical_v=d.get(
                'battery_critical_v', defaults.battery_critical_v),
            stale_timeout=d.get('stale_timeout', defaults.stale_timeout),
            stale_timeouts=stale_timeouts,
        )

    def to_yaml(self) -> str:
        return yaml.dump(self.to_dict(), default_flow_style=False, sort_keys=False)

    @classmethod
    def from_yaml(cls, text: str) -> 'BoatStateConfig':
        return cls.from_dict(yaml.safe_load(text) or {})

    @classmethod
    def from_file(cls, path: str) -> 'BoatStateConfig':
        with open(path) as f:
            return cls.from_yaml(f.read())

    def to_file(self, path: str):
        with open(path, 'w') as f:
            f.write(self.to_yaml())
