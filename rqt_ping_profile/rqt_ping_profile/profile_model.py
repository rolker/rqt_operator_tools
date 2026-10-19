"""Qt-free, ROS-free model for the ping-profile view.

Everything the unit tests exercise lives here so it runs without a display or a
ROS graph: turning a ``marine_acoustic_msgs/SonarDetections`` into a ``Ping`` of
beam positions, the latest-ping/history ``PingStore`` with staleness, the
fixed/auto ``DepthScale``, the metres-to-pixels ``Viewport`` and the
YAML-persisted ``ProfileConfig``.

Geometry.  ``SonarDetections.rx_angles`` is the across-track steering angle of
each receive beam, in radians, "+ to starboard, - to port for a downlooking
sonar", measured from straight down.  For beam ``i``::

    range  = two_way_travel_times[i] * ping_info.sound_speed / 2
    across = range * sin(rx_angles[i])     # metres, + to starboard
    depth  = range * cos(rx_angles[i])     # metres below the transducer

``mbes_sim`` follows the same convention (its port-most beam has a negative
angle).  Along-track steering (``tx_angles``) is ignored: a single ping is drawn
as a cross-section, and for a flat across-track fan the depth below the
transducer is ``range * cos(rx)``.  Tide and draft offsets are not applied.

Screen convention.  The view looks at the boat from the FRONT, so the boat's
port side is on the viewer's RIGHT and starboard on the LEFT:
``screen_x = centre_x - across / metres_per_pixel``.
"""

import math
from collections import deque
from dataclasses import dataclass, fields
from typing import Deque, List, Optional, Tuple

import yaml

DEFAULT_TOPIC = 'sensors/mbes/detections'

DEPTH_FIXED = 'fixed'
DEPTH_AUTO = 'auto'
DEPTH_MODES = (DEPTH_FIXED, DEPTH_AUTO)

DEFAULT_DEPTH_MIN = 0.0
DEFAULT_DEPTH_MAX = 30.0
DEFAULT_STALE_TIMEOUT = 5.0
HISTORY_MAX = 50

# Auto-scale: this much extra room around the largest depth / half swath.
AUTO_HEADROOM = 1.10
# Auto-scale shrinks only when the need falls below this share of the extent.
AUTO_SHRINK_BELOW = 0.70

# DetectionFlag.DETECT_OK
DETECT_OK = 0

_MIN_EXTENT = 1e-3     # m; keeps scales positive
_MAX_METRES = 1e6      # m; beyond this a value is nonsense, not a depth


class BadPing(ValueError):
    """A message that cannot be drawn; the text says why (shown and logged)."""


@dataclass(frozen=True)
class Beam:
    """One beam: metres to starboard (+) and below the transducer, and its flag."""

    across: float
    depth: float
    ok: bool


@dataclass(frozen=True)
class Ping:
    """A validated ping: beams sorted port to starboard (by across-track)."""

    beams: Tuple[Beam, ...]
    stamp_ns: int = 0
    dropped: int = 0     # beams with a non-finite or negative value, left out

    @property
    def ok_beams(self) -> Tuple[Beam, ...]:
        """The OK detections, port to starboard: the bathymetry profile."""
        return tuple(b for b in self.beams if b.ok)

    @property
    def flagged_beams(self) -> Tuple[Beam, ...]:
        """Beams flagged as not OK."""
        return tuple(b for b in self.beams if not b.ok)

    @property
    def swath_edges(self) -> Optional[Tuple[Beam, Beam]]:
        """``(port-most OK, starboard-most OK)`` beams, ``None`` without OK beams."""
        ok = self.ok_beams
        if not ok:
            return None
        return ok[0], ok[-1]

    @property
    def max_depth(self) -> float:
        """Deepest OK detection (0 without any)."""
        return max((b.depth for b in self.ok_beams), default=0.0)

    @property
    def half_width(self) -> float:
        """Largest ``|across|`` of the OK detections (0 without any)."""
        return max((abs(b.across) for b in self.ok_beams), default=0.0)

    @classmethod
    def from_msg(cls, msg) -> 'Ping':
        """Build from a ``SonarDetections`` (or anything shaped like one).

        Raises ``BadPing`` when the arrays are empty or of different lengths or
        the sound speed is unusable; individual bad beams are dropped and counted.
        """
        try:
            twtt = list(msg.two_way_travel_times)
            angles = list(msg.rx_angles)
            flags = [getattr(f, 'flag', f) for f in msg.flags]
            speed = float(msg.ping_info.sound_speed)
        except (AttributeError, TypeError, ValueError, OverflowError) as exc:
            raise BadPing(f'unreadable message: {exc!r}') from exc
        n = len(twtt)
        if n == 0:
            raise BadPing('no beams (two_way_travel_times is empty)')
        if len(angles) != n or len(flags) != n:
            raise BadPing(
                f'array lengths differ: two_way_travel_times={n}, '
                f'rx_angles={len(angles)}, flags={len(flags)}')
        if not (math.isfinite(speed) and speed > 0.0):
            raise BadPing(f'unusable sound speed {speed!r}')

        beams: List[Beam] = []
        dropped = 0
        for t, a, f in zip(twtt, angles, flags):
            try:
                t, a, f = float(t), float(a), int(f)
            except (TypeError, ValueError, OverflowError):
                dropped += 1
                continue
            if not (math.isfinite(t) and math.isfinite(a)) or t < 0.0:
                dropped += 1
                continue
            rng = t * speed / 2.0
            if rng > _MAX_METRES:
                dropped += 1
                continue
            beams.append(Beam(across=rng * math.sin(a), depth=rng * math.cos(a),
                              ok=(f == DETECT_OK)))
        beams.sort(key=lambda b: b.across)
        stamp = getattr(getattr(msg, 'header', None), 'stamp', None)
        try:
            stamp_ns = int(stamp.sec) * 1_000_000_000 + int(stamp.nanosec)
        except (AttributeError, TypeError, ValueError):
            stamp_ns = 0
        return cls(beams=tuple(beams), stamp_ns=stamp_ns, dropped=dropped)


# -- Store ---------------------------------------------------------------------


class PingStore:
    """Latest ping, an optional history of earlier ones, and staleness."""

    def __init__(self, history: int = 0):
        self._history_len = max(0, int(history))
        self._older: Deque[Ping] = deque(maxlen=self._history_len or None)
        self.latest: Optional[Ping] = None
        self.received: Optional[float] = None   # clock value at receipt
        self.count = 0
        self.last_error = ''

    def set_history(self, history: int):
        """Change how many earlier pings are kept (0 drops them)."""
        self._history_len = max(0, int(history))
        keep = list(self._older)[-self._history_len:] if self._history_len else []
        self._older = deque(keep, maxlen=self._history_len or None)

    def update(self, msg, now: float) -> Optional[Ping]:
        """Take a message; return the ping, or ``None`` (reason in ``last_error``)."""
        try:
            ping = Ping.from_msg(msg)
        except BadPing as exc:
            self.last_error = str(exc)
            return None
        self.last_error = ''
        if self.latest is not None and self._history_len:
            self._older.append(self.latest)
        self.latest = ping
        self.received = now
        self.count += 1
        return ping

    def history(self) -> List[Ping]:
        """Earlier pings, oldest first (excludes the latest)."""
        return list(self._older) if self._history_len else []

    def age(self, now: float) -> Optional[float]:
        """Seconds since the last good ping, ``None`` before the first."""
        if self.received is None:
            return None
        return max(0.0, now - self.received)

    def is_stale(self, now: float, timeout: float) -> bool:
        """True once ``timeout`` s passed without a good ping (0 disables)."""
        age = self.age(now)
        if age is None or timeout <= 0.0:
            return False
        return age > timeout

    def clear(self):
        """Forget everything (a topic change)."""
        self._older.clear()
        self.latest = None
        self.received = None
        self.count = 0
        self.last_error = ''


# -- Scale and viewport -----------------------------------------------------------


class DepthScale:
    """The depth range and the extent the picture must cover.

    ``fixed``: ``depth_min`` to ``depth_max`` as set.  ``auto``: from 0 (the
    transducer) to the needed depth, with headroom.  The needed depth and half
    swath width each grow immediately, so the whole swath is always visible, and
    shrink only once the need is well below the current extent, so the picture
    does not jump with every ping.
    """

    def __init__(self, mode=DEPTH_FIXED, depth_min=DEFAULT_DEPTH_MIN,
                 depth_max=DEFAULT_DEPTH_MAX):
        self.mode = mode
        self.fixed_min = depth_min
        self.fixed_max = depth_max
        self._depth: Optional[float] = None   # auto extents; unset until a ping
        self._half = 0.0

    def configure(self, mode, depth_min, depth_max):
        """Apply new settings; leaving auto resets its memory."""
        if mode != self.mode:
            self._depth = None
            self._half = 0.0
        self.mode, self.fixed_min, self.fixed_max = mode, depth_min, depth_max

    def observe(self, ping: Optional[Ping]):
        """Feed the latest ping (auto mode only; a ping without OK beams is ignored)."""
        if self.mode != DEPTH_AUTO or ping is None or not ping.ok_beams:
            return
        if self._depth is None:                 # the first ping sets the scale
            self._depth, self._half = 0.0, 0.0
        self._depth = self._track(self._depth, ping.max_depth)
        self._half = self._track(self._half, ping.half_width)

    @staticmethod
    def _track(current: float, need: float) -> float:
        target = max(need, 0.0) * AUTO_HEADROOM
        if target >= current:
            return target                       # grow at once
        if target < current * AUTO_SHRINK_BELOW:
            return target                       # shrink only when well below
        return current

    @property
    def depth_range(self) -> Tuple[float, float]:
        """``(top, bottom)`` depth in metres of the plot area."""
        if self.mode == DEPTH_FIXED:
            return self.fixed_min, self.fixed_max
        if self._depth is None:                 # no ping yet: show the fixed bottom
            return 0.0, max(self.fixed_max, _MIN_EXTENT)
        return 0.0, max(self._depth, _MIN_EXTENT)

    @property
    def half_width_needed(self) -> float:
        """Half swath width to keep visible (auto mode; 0 in fixed mode)."""
        return self._half if self.mode == DEPTH_AUTO else 0.0

    def metres_per_pixel(self, width_px: float, height_px: float) -> float:
        """One scale for both axes, so angles read true.

        Fixed: the depth range fills the plot height; the width then decides how
        much across-track shows.  Auto: the larger of what the depth and the full
        swath width need, so the whole swath fits.
        """
        height_px, width_px = max(float(height_px), 1.0), max(float(width_px), 1.0)
        top, bottom = self.depth_range
        mpp = max(bottom - top, _MIN_EXTENT) / height_px
        if self.mode == DEPTH_AUTO and self._half > 0.0:
            mpp = max(mpp, 2.0 * self._half / width_px)
        return mpp


@dataclass(frozen=True)
class Viewport:
    """Metres to pixels for a plot rectangle; the transducer is at the top centre."""

    left: float
    top: float
    width: float
    height: float
    metres_per_pixel: float
    depth_top: float = 0.0

    @property
    def centre_x(self) -> float:
        return self.left + self.width / 2.0

    def to_screen(self, across: float, depth: float) -> Tuple[float, float]:
        """Pixel of a point.  Starboard (+across) goes LEFT: the view is from the front."""
        return (self.centre_x - across / self.metres_per_pixel,
                self.top + (depth - self.depth_top) / self.metres_per_pixel)

    def depth_at_bottom(self) -> float:
        return self.depth_top + self.height * self.metres_per_pixel

    def half_width_visible(self) -> float:
        """Across-track metres visible on each side of the centre line."""
        return self.width / 2.0 * self.metres_per_pixel

    def contains_depth(self, depth: float) -> bool:
        """Whether ``depth`` lies inside the plotted depth range."""
        return self.depth_top <= depth <= self.depth_at_bottom()


def nice_step(span: float, target_ticks: int = 6) -> float:
    """A 1-2-5 tick step giving about ``target_ticks`` ticks over ``span`` metres."""
    if not (math.isfinite(span) and span > 0.0):
        return 1.0
    raw = span / max(target_ticks, 1)
    magnitude = 10.0 ** math.floor(math.log10(raw))
    for factor in (1.0, 2.0, 5.0, 10.0):
        if raw <= factor * magnitude:
            return factor * magnitude
    return 10.0 * magnitude


def ticks(lo: float, hi: float, step: float) -> List[float]:
    """Multiples of ``step`` within ``[lo, hi]`` (capped, whatever the inputs)."""
    if not (math.isfinite(lo) and math.isfinite(hi) and step > 0.0) or hi < lo:
        return []
    first = math.ceil(lo / step - 1e-9)
    out = []
    k = first
    while k * step <= hi + 1e-9 and len(out) < 200:
        out.append(round(k * step, 9))
        k += 1
    return out


def format_metres(value: float, step: float) -> str:
    """A tick label with the decimals the step needs."""
    decimals = 0 if step >= 1.0 else min(3, int(math.ceil(-math.log10(step))))
    return f'{value:.{decimals}f}'


def format_age(seconds: float) -> str:
    """``3 s`` or ``2 min 5 s`` style age."""
    seconds = max(0.0, seconds)
    if seconds < 60.0:
        return f'{seconds:.0f} s'
    return f'{int(seconds // 60)} min {int(seconds % 60)} s'


# -- Config --------------------------------------------------------------------


def _finite(value, default, lo=None, hi=None):
    try:
        v = float(value)
    except (TypeError, ValueError, OverflowError):
        return default
    if not math.isfinite(v):
        return default
    if lo is not None:
        v = max(v, lo)
    if hi is not None:
        v = min(v, hi)
    return v


@dataclass
class ProfileConfig:
    """Settings persisted through rqt ``instance_settings`` as YAML."""

    topic: str = DEFAULT_TOPIC
    title: str = ''                  # empty = show the topic
    depth_mode: str = DEPTH_FIXED
    depth_min: float = DEFAULT_DEPTH_MIN
    depth_max: float = DEFAULT_DEPTH_MAX
    stale_timeout: float = DEFAULT_STALE_TIMEOUT   # seconds; 0 disables
    history: int = 0                 # earlier pings faded behind the current one
    show_flagged: bool = False       # draw not-OK beams as dim dots

    def normalized(self) -> 'ProfileConfig':
        """Return a copy with every field coerced to something usable."""
        d = ProfileConfig()
        topic = str(self.topic).strip() if self.topic else ''
        title = str(self.title).strip() if self.title else ''
        mode = self.depth_mode if self.depth_mode in DEPTH_MODES else d.depth_mode
        dmin = _finite(self.depth_min, d.depth_min, -1000.0, 20000.0)
        dmax = _finite(self.depth_max, d.depth_max, -1000.0, 20000.0)
        if dmax <= dmin:                      # an empty or inverted range
            dmin, dmax = d.depth_min, d.depth_max
            if dmax <= dmin:
                dmax = dmin + 1.0
        timeout = _finite(self.stale_timeout, d.stale_timeout)
        if timeout < 0.0:
            timeout = d.stale_timeout
        try:
            history = int(self.history)
        except (TypeError, ValueError, OverflowError):
            history = d.history
        if isinstance(self.history, bool):
            history = d.history
        history = min(max(history, 0), HISTORY_MAX)
        return ProfileConfig(
            topic=topic or d.topic,
            title=title,
            depth_mode=mode,
            depth_min=dmin,
            depth_max=dmax,
            stale_timeout=timeout,
            history=history,
            show_flagged=(self.show_flagged if isinstance(self.show_flagged, bool)
                          else d.show_flagged),
        )

    def to_yaml(self) -> str:
        n = self.normalized()
        return yaml.safe_dump({f.name: getattr(n, f.name) for f in fields(n)},
                              sort_keys=True)

    @classmethod
    def from_yaml(cls, text: str) -> 'ProfileConfig':
        """Parse leniently: unknown keys are ignored, bad values fall back."""
        try:
            data = yaml.safe_load(text)
        except yaml.YAMLError:
            return cls()
        if not isinstance(data, dict):
            return cls()
        known = {f.name for f in fields(cls)}
        return cls(**{k: v for k, v in data.items() if k in known}).normalized()
