"""The ping-profile view: the latest multibeam ping seen from the front.

The widget subscribes to one ``SonarDetections`` topic, keeps the latest ping
(and optionally a few earlier ones) in a ``profile_model.PingStore`` and paints
it: the transducer at the top centre, a line to each outermost OK beam (the
swath), the OK detections joined as the bathymetry profile, depth downwards.
Port is on the viewer's right (see ``profile_model``).  The swath, profile and
title use the colour of the platform that owns the topic (``/marine/platforms``,
see ``platform_lookup``) or the ``color`` setting.  ROS callbacks only emit a Qt
signal; every store and widget access happens on the Qt thread.
"""

import importlib
import math
import threading
import time
from types import SimpleNamespace as NS
from typing import List

from python_qt_binding.QtCore import QPointF, QRectF, Qt, QTimer, Signal
from python_qt_binding.QtGui import (
    QColor,
    QFont,
    QFontMetrics,
    QPainter,
    QPen,
    QPolygonF,
)
from python_qt_binding.QtWidgets import QSizePolicy, QWidget
from rclpy.qos import (
    DurabilityPolicy,
    HistoryPolicy,
    QoSProfile,
    ReliabilityPolicy,
)

from .platform_lookup import (
    normalize_namespace,
    parse_hex_color,
    PlatformDirectory,
)
from .profile_model import (
    DepthScale,
    format_age,
    format_metres,
    nice_step,
    PingStore,
    ProfileConfig,
    ticks,
    Viewport,
)

_BACKGROUND = QColor(24, 26, 30)
_GRID = QColor(70, 74, 82)
_AXIS_TEXT = QColor(170, 175, 185)
_TEXT = QColor(235, 235, 235)
_SWATH = QColor(90, 176, 255)
_PROFILE = QColor(94, 214, 138)
_FLAGGED = QColor(200, 90, 90)
_BOAT = QColor(240, 192, 80)
_AMBER = QColor(255, 176, 0)
_STALE_ALPHA = 90
_MARGIN_LEFT = 46
_MARGIN_RIGHT = 12
_MARGIN_TOP = 26
_MARGIN_BOTTOM = 30
_MAX_PIXEL = 1.0e5     # coordinates beyond this are clamped before reaching Qt
_ERROR_LOG_INTERVAL = 10.0   # s; at most one 'unusable ping' log line per this

# Detections are a sensor stream: best_effort matches both reliable and
# best_effort publishers; only the newest ping matters.
DETECTIONS_QOS = QoSProfile(
    reliability=ReliabilityPolicy.BEST_EFFORT,
    durability=DurabilityPolicy.VOLATILE,
    history=HistoryPolicy.KEEP_LAST,
    depth=1,
)

PLATFORMS_TOPIC = '/marine/platforms'

# platform_send.py publishes /marine/platforms reliable and volatile, depth 5
# (once a second, so a late joiner waits at most that long); CAMP subscribes
# the same way.
PLATFORMS_QOS = QoSProfile(
    reliability=ReliabilityPolicy.RELIABLE,
    durability=DurabilityPolicy.VOLATILE,
    history=HistoryPolicy.KEEP_LAST,
    depth=5,
)


def load_platforms_class():
    """Import ``PlatformList``; ``None`` when marine_interfaces is not built."""
    try:
        return importlib.import_module('marine_interfaces.msg').PlatformList
    except (ImportError, AttributeError):
        return None


def load_message_class():
    """Import ``SonarDetections``; ``None`` when marine_acoustic_msgs lacks it."""
    try:
        return importlib.import_module('marine_acoustic_msgs.msg').SonarDetections
    except (ImportError, AttributeError):
        return None


def _clamp(value: float) -> float:
    if not math.isfinite(value):
        return 0.0
    return max(-_MAX_PIXEL, min(_MAX_PIXEL, value))


class PingProfileWidget(QWidget):
    """Paints the latest ping of one topic; staleness and errors are shown on it."""

    # Wake-up: ROS thread -> Qt thread.  The message itself goes through a
    # one-slot mailbox (newest wins), so a stalled GUI cannot build a queue.
    _msg_received = Signal()
    _platforms_received = Signal()

    def __init__(self, node, parent=None, clock=time.monotonic, config=None):
        super().__init__(parent)
        self._node = node
        self._clock = clock
        self._config = config.normalized() if config else ProfileConfig()
        self._store = PingStore(self._config.history)
        self._scale = DepthScale(self._config.depth_mode, self._config.depth_min,
                                 self._config.depth_max)
        self._subscription = None
        self._generation = 0
        self._msg_class = load_message_class()
        self._subscribe_error = ''
        self._last_error_log = None
        self._paint_error = ''
        self._refresh_pending = False
        self._closed = False
        self._mailbox_lock = threading.Lock()
        self._mailbox = None            # (generation, msg, receipt clock) or None
        self._platforms = PlatformDirectory()
        self._platforms_sub = None
        self._platforms_error = ''
        # Pending platforms by namespace, newest per namespace (same lock).  Not
        # 'newest list wins': each boat publishes its own one-platform list on
        # the shared topic, so a burst of lists must not drop any boat.
        self._platforms_mailbox = {}
        self.setMinimumSize(160, 120)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self._msg_received.connect(self._take_message)
        self._platforms_received.connect(self._take_platforms)

        self._timer = QTimer(self)
        self._timer.timeout.connect(self.update)    # staleness is time, not messages
        self._timer.start(500)
        self._setup_subscription()
        self._setup_platforms_subscription()

    # -- Config ----------------------------------------------------------------

    def get_config(self) -> ProfileConfig:
        return self._config

    def load_config(self, config: ProfileConfig):
        """Apply a configuration; resubscribe only when the topic changed."""
        new = config.normalized()
        topic_changed = new.topic != self._config.topic
        self._config = new
        self._store.set_history(new.history)
        self._scale.configure(new.depth_mode, new.depth_min, new.depth_max)
        if topic_changed:
            self._store.clear()          # a ping of the old topic is not this boat's
            self._scale.reset()
            with self._mailbox_lock:
                self._mailbox = None
        self._scale.observe(self._store.latest)
        # Retry also when the last attempt failed, so applying the settings
        # again recovers from a bad topic name.
        if topic_changed or (self._subscription is None and self._msg_class):
            self._teardown_subscription()
            self._setup_subscription()
        self.update()

    def shutdown(self):
        self._closed = True
        self._timer.stop()
        self._teardown_subscription()
        if self._platforms_sub is not None:
            self._node.destroy_subscription(self._platforms_sub)
            self._platforms_sub = None

    # -- Subscription ----------------------------------------------------------

    def _setup_subscription(self):
        self._generation += 1
        if self._msg_class is None:
            self._subscribe_error = 'marine_acoustic_msgs is not importable'
            self._node.get_logger().warning(
                'marine_acoustic_msgs/SonarDetections is not importable; '
                'build marine_acoustic_msgs to use the ping profile')
            return
        generation = self._generation
        self._subscribe_error = ''
        try:
            self._subscription = self._node.create_subscription(
                self._msg_class, self._config.topic,
                lambda msg, g=generation: self._on_ros_message(g, msg),
                DETECTIONS_QOS)
        except Exception as exc:  # e.g. InvalidTopicNameException
            self._subscription = None
            self._subscribe_error = f'cannot subscribe to {self._config.topic}: {exc}'
            self._node.get_logger().error(self._subscribe_error)

    def _on_ros_message(self, generation, msg):
        """ROS thread: park the newest message and wake the Qt thread.

        The receipt time is taken here, so a message that waits for a busy GUI
        is not made to look fresh when it is finally handled.
        """
        with self._mailbox_lock:
            wake = self._mailbox is None
            self._mailbox = (generation, msg, self._clock())
        if wake and not self._closed:
            try:
                self._msg_received.emit()
            except RuntimeError:        # the widget was deleted during shutdown
                pass

    def _teardown_subscription(self):
        # Bumping the generation drops messages already queued for the Qt
        # thread from the subscription being destroyed.
        self._generation += 1
        if self._subscription is not None:
            self._node.destroy_subscription(self._subscription)
            self._subscription = None

    def _setup_platforms_subscription(self):
        """Follow /marine/platforms for the colour and name; failure leaves the defaults."""
        msg_class = load_platforms_class()
        if msg_class is None:
            self._platforms_error = 'marine_interfaces is not importable'
            self._node.get_logger().warning(
                'marine_interfaces/PlatformList is not importable; the ping profile '
                'cannot take the platform colour (use the colour setting)')
            return
        try:
            self._platforms_sub = self._node.create_subscription(
                msg_class, PLATFORMS_TOPIC, self._on_platforms_message, PLATFORMS_QOS)
        except Exception as exc:  # noqa: BLE001 - the plot works without the colour
            self._platforms_error = f'cannot subscribe to {PLATFORMS_TOPIC}: {exc}'
            self._node.get_logger().error(self._platforms_error)

    def _on_platforms_message(self, msg):
        """ROS thread: park the platforms (newest per namespace) and wake the Qt thread."""
        try:
            platforms = [(normalize_namespace(p.platform_namespace), p)
                         for p in msg.platforms]
        except Exception:  # noqa: BLE001 - unusable list; nothing to take from it
            platforms = []
        with self._mailbox_lock:
            wake = not self._platforms_mailbox
            for key, platform in platforms:
                if key:
                    self._platforms_mailbox[key] = platform
            wake = wake and bool(self._platforms_mailbox)
        if wake and not self._closed:
            try:
                self._platforms_received.emit()
            except RuntimeError:        # the widget was deleted during shutdown
                pass

    # -- Qt-thread message handling ---------------------------------------------

    def _take_message(self):
        with self._mailbox_lock:
            item, self._mailbox = self._mailbox, None
        if item is None:
            return
        generation, msg, received = item
        if generation != self._generation:
            return
        self._handle_message(msg, received)

    def _take_platforms(self):
        with self._mailbox_lock:
            pending, self._platforms_mailbox = self._platforms_mailbox, {}
        if not pending:
            return
        try:
            changed = self._platforms.update(NS(platforms=list(pending.values())))
        except Exception as exc:  # a Qt slot must not raise (PyQt aborts)
            self._node.get_logger().warning(f'Ignoring unusable PlatformList: {exc!r}')
            return
        if changed:
            self.update()

    # -- Platform identity -------------------------------------------------------

    def platform_match(self):
        """The platform owning the topic (a ``PlatformMatch``) or ``None``."""
        return self._platforms.lookup(self._config.topic)

    def effective_color(self):
        """The colour to draw in (a ``QColor``): the override, else the platform's, else ``None``."""
        rgba = parse_hex_color(self._config.color)
        if rgba is None:
            match = self.platform_match()
            rgba = match.color if match else None
        if rgba is None:
            return None
        return QColor.fromRgbF(rgba[0], rgba[1], rgba[2])

    def color_note(self):
        """Where the colour comes from, for the status line."""
        if parse_hex_color(self._config.color) is not None:
            return 'colour: manual override'
        match = self.platform_match()
        if match and match.color:
            return f'colour: platform {match.name or match.namespace}'
        if match:
            return f'colour: default (platform {match.name or match.namespace} has no colour)'
        if self._platforms_error:
            return f'colour: default ({self._platforms_error})'
        return 'colour: default (no platform match)'

    def display_title(self):
        """The title setting, else the platform's name, else the topic."""
        if self._config.title:
            return self._config.title
        match = self.platform_match()
        return match.name if match and match.name else self._config.topic

    def _handle_message(self, msg, now):
        try:
            ping = self._store.update(msg, now)
            reason = self._store.last_error
        except Exception as exc:  # a Qt slot must not raise (PyQt aborts)
            ping, reason = None, f'unreadable message: {exc!r}'
            self._store.last_error = reason
        if ping is None:
            # One log line per interval whatever the reason: a sonar publishing
            # garbage at ping rate, or alternating faults, cannot flood the log.
            if (self._last_error_log is None
                    or now - self._last_error_log >= _ERROR_LOG_INTERVAL):
                self._node.get_logger().warning(
                    f'Ignoring unusable SonarDetections on {self._config.topic}: '
                    f'{reason}')
                self._last_error_log = now
        else:
            self._scale.observe(ping)
        self._schedule_refresh()

    def _schedule_refresh(self):
        """Repaint once per burst of messages."""
        if not self._refresh_pending:
            self._refresh_pending = True
            QTimer.singleShot(0, self._refresh)

    def _refresh(self):
        self._refresh_pending = False
        if not self._closed:
            self.update()

    # -- Status ----------------------------------------------------------------

    def status_text(self):
        """``(text, is_problem)`` for the line under the title; the colour source comes last."""
        text, problem = self._ping_status()
        note = self.color_note()
        return (f'{text} | {note}' if text else note), problem

    def _ping_status(self):
        now = self._clock()
        if self._paint_error:
            return self._paint_error, True
        if self._subscribe_error:
            return self._subscribe_error, True
        if self._store.latest is None:
            if self._store.last_error:
                return f'unusable ping: {self._store.last_error}', True
            return f'waiting for the first ping on {self._config.topic}', True
        parts = []
        problem = False
        age = self._store.age(now)
        if self._store.is_stale(now, self._config.stale_timeout):
            parts.append(f'NO PING FOR {format_age(age)}')
            problem = True
        if self._store.last_error:
            parts.append(f'unusable ping ignored: {self._store.last_error}')
            problem = True
        ping = self._store.latest
        if not ping.ok_beams:
            parts.append('no OK detections in the last ping')
            problem = True
        if ping.dropped:
            parts.append(f'{ping.dropped} invalid beams dropped')
        return ' | '.join(parts), problem

    # -- Painting --------------------------------------------------------------

    def paintEvent(self, _event):
        painter = QPainter(self)
        try:
            self._paint(painter)
            self._paint_error = ''
        except Exception as exc:  # a paint handler must not raise
            text = f'display error: {exc!r}'
            if text != self._paint_error:
                self._node.get_logger().error(text)
            self._paint_error = text
            try:    # say so on the display, too, as far as the painter still works
                painter.setPen(_AMBER)
                painter.drawText(QPointF(8, 17), text[:120])
            except Exception:  # noqa: BLE001 - nothing more can be done
                pass
        finally:
            painter.end()

    def viewport_for(self, width: float, height: float) -> Viewport:
        """The plot rectangle and scale for a widget of this size."""
        plot_w = max(width - _MARGIN_LEFT - _MARGIN_RIGHT, 1.0)
        plot_h = max(height - _MARGIN_TOP - _MARGIN_BOTTOM, 1.0)
        top, _bottom = self._scale.depth_range
        return Viewport(left=_MARGIN_LEFT, top=_MARGIN_TOP, width=plot_w,
                        height=plot_h,
                        metres_per_pixel=self._scale.metres_per_pixel(plot_w, plot_h),
                        depth_top=top)

    def _point(self, vp: Viewport, across: float, depth: float) -> QPointF:
        x, y = vp.to_screen(across, depth)
        return QPointF(_clamp(x), _clamp(y))

    def _paint(self, p: QPainter):
        p.setRenderHint(QPainter.Antialiasing, True)
        p.fillRect(self.rect(), _BACKGROUND)
        vp = self.viewport_for(self.width(), self.height())
        status, problem = self.status_text()
        stale = self._store.is_stale(self._clock(), self._config.stale_timeout)
        plot = QRectF(vp.left, vp.top, vp.width, vp.height)

        small = QFont(self.font())
        small.setPixelSize(11)
        p.setFont(small)
        self._paint_grid(p, vp)

        p.save()
        try:
            p.setClipRect(plot)
            self._paint_history(p, vp, dim=stale)
            ping = self._store.latest
            if ping is not None:
                self._paint_ping(p, vp, ping, dim=stale)
        finally:
            p.restore()

        # Transducer: the boat's reference point at depth 0 (when in view).
        if vp.contains_depth(0.0):
            c = self._point(vp, 0.0, 0.0)
            p.setPen(Qt.NoPen)
            p.setBrush(_BOAT)
            p.drawPolygon(QPolygonF([QPointF(c.x() - 6, c.y() - 8),
                                     QPointF(c.x() + 6, c.y() - 8),
                                     QPointF(c.x(), c.y() + 2)]))

        accent = self.effective_color()
        title = self.display_title()
        big = QFont(self.font())
        big.setPixelSize(14)
        big.setBold(True)
        p.setFont(big)
        p.setPen(accent or _TEXT)
        # The status line must stay visible: the title gives way (elided to at
        # most 60 % of the width) and the status takes what is left.
        width = max(self.width() - 16, 20)
        shown = QFontMetrics(big).elidedText(title, Qt.ElideRight, int(width * 0.6))
        p.drawText(QPointF(8, 17), shown)
        title_w = QFontMetrics(big).horizontalAdvance(shown)
        p.setFont(small)
        if status:
            p.setPen(_AMBER if problem else _AXIS_TEXT)
            avail = max(width - title_w - 12, 20)
            text = QFontMetrics(small).elidedText(status, Qt.ElideRight, int(avail))
            p.drawText(QPointF(8 + title_w + 12, 17), text)

    def _paint_grid(self, p: QPainter, vp: Viewport):
        depth_step = nice_step(vp.depth_at_bottom() - vp.depth_top)
        across_step = depth_step    # same metres per pixel, same step: a square grid
        half = vp.half_width_visible()
        plot = QRectF(vp.left, vp.top, vp.width, vp.height)
        p.save()
        p.setClipRect(plot.adjusted(-1, -1, 1, 1))
        p.setPen(QPen(_GRID, 1))
        for d in ticks(vp.depth_top, vp.depth_at_bottom(), depth_step):
            y = self._point(vp, 0.0, d).y()
            p.drawLine(QPointF(vp.left, y), QPointF(vp.left + vp.width, y))
        for a in ticks(-half, half, across_step):
            x = self._point(vp, a, vp.depth_top).x()
            p.drawLine(QPointF(x, vp.top), QPointF(x, vp.top + vp.height))
        # centre line (under the boat), dashed
        pen = QPen(_AXIS_TEXT, 1, Qt.DashLine)
        p.setPen(pen)
        cx = vp.centre_x
        p.drawLine(QPointF(cx, vp.top), QPointF(cx, vp.top + vp.height))
        p.restore()

        p.setPen(_AXIS_TEXT)
        fm = p.fontMetrics()
        for d in ticks(vp.depth_top, vp.depth_at_bottom(), depth_step):
            y = self._point(vp, 0.0, d).y()
            label = format_metres(d, depth_step)
            p.drawText(QPointF(vp.left - 6 - fm.horizontalAdvance(label), y + 4), label)
        base = vp.top + vp.height + 14
        for a in ticks(-half, half, across_step):
            x = self._point(vp, a, 0.0).x()
            label = format_metres(abs(a), across_step)
            p.drawText(QPointF(x - fm.horizontalAdvance(label) / 2, base), label)
        p.drawText(QPointF(vp.left - 30, base), 'm')   # both axes are in metres
        # Viewed from the front: starboard on the viewer's left, port on the right.
        p.drawText(QPointF(vp.left, base + 12), 'STBD')
        port = 'PORT'
        p.drawText(QPointF(vp.left + vp.width - fm.horizontalAdvance(port), base + 12),
                   port)
        p.setPen(QPen(_GRID, 1))
        p.setBrush(Qt.NoBrush)
        p.drawRect(plot)

    def _paint_history(self, p: QPainter, vp: Viewport, dim: bool = False):
        older = self._store.history()
        n = len(older)
        for i, ping in enumerate(older):
            alpha = int(25 + 85 * (i + 1) / (n + 1))     # oldest faintest
            if dim:
                alpha //= 2
            color = QColor(self.effective_color() or _PROFILE)
            color.setAlpha(alpha)
            p.setPen(QPen(color, 1))
            self._polyline(p, vp, ping.ok_beams)

    def _paint_ping(self, p: QPainter, vp: Viewport, ping, dim: bool):
        alpha = _STALE_ALPHA if dim else 255
        accent = self.effective_color()
        swath, profile, flagged = (QColor(accent or _SWATH), QColor(accent or _PROFILE),
                                   QColor(_FLAGGED))
        for c in (swath, profile, flagged):
            c.setAlpha(alpha)
        if self._config.show_flagged:
            fl = QColor(flagged)
            fl.setAlpha(min(alpha, 120))
            p.setPen(Qt.NoPen)
            p.setBrush(fl)
            for b in ping.flagged_beams:
                p.drawEllipse(self._point(vp, b.across, b.depth), 2.0, 2.0)
        edges = ping.swath_edges
        origin = self._point(vp, 0.0, 0.0)
        if edges is not None:
            p.setPen(QPen(swath, 2))
            for b in edges:
                p.drawLine(origin, self._point(vp, b.across, b.depth))
        p.setPen(QPen(profile, 2))
        self._polyline(p, vp, ping.ok_beams)
        p.setPen(Qt.NoPen)
        p.setBrush(profile)
        for b in ping.ok_beams:
            p.drawEllipse(self._point(vp, b.across, b.depth), 1.8, 1.8)

    def _polyline(self, p: QPainter, vp: Viewport, beams):
        if len(beams) < 2:
            return
        p.setBrush(Qt.NoBrush)
        p.drawPolyline(QPolygonF([self._point(vp, b.across, b.depth) for b in beams]))

    # -- Test and tooling helpers ------------------------------------------------

    def latest_profile(self) -> List[tuple]:
        """``[(across, depth)]`` of the OK detections of the latest ping."""
        ping = self._store.latest
        return [(b.across, b.depth) for b in ping.ok_beams] if ping else []
