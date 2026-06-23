"""Core boat-state widget — subscriptions, layout, and data distribution.

Owns every ROS subscription, a 1 Hz stale timer, and the child gauges,
authority banner, and environment panel.  ROS callbacks run on the executor
thread and hand messages to the Qt thread via a queued signal (the pattern
borrowed from ``AnnunciatorWidget``).
"""

import math
import time

from python_qt_binding.QtCore import QTimer, Signal
from python_qt_binding.QtGui import QColor
from python_qt_binding.QtWidgets import (
    QGridLayout,
    QVBoxLayout,
    QWidget,
)

from .authority_banner import AuthorityBanner
from .config_model import (
    BoatStateConfig,
    enu_yaw_to_compass,
    is_stale,
    quaternion_to_yaw,
    resolve_authority,
    should_grey_commanded,
)
from .environment_panel import EnvironmentPanel
from .gauges.battery_gauge import BatteryGauge
from .gauges.center_zero_gauge import CenterZeroGauge
from .gauges.heading_gauge import HeadingGauge
from .gauges.speed_gauge import SpeedGauge


class BoatStateWidget(QWidget):
    """Container that subscribes to the boat-state topics and drives the gauges."""

    # Thread-safe hand-off: (source-name, message) from ROS thread to Qt thread.
    _msg_received = Signal(str, object)

    def __init__(self, node, parent=None):
        super().__init__(parent)
        self._node = node
        self._config = BoatStateConfig()
        self._subscriptions = {}
        self._last_update = {}

        # Latched mode state for authority resolution.
        self._fcu_mode = ''
        self._armed = False
        self._piloting_mode = ''

        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), QColor(30, 30, 30))
        self.setPalette(palette)

        self._build_ui()

        self._msg_received.connect(self._handle_message)

        self._stale_timer = QTimer(self)
        self._stale_timer.timeout.connect(self._check_stale)
        self._stale_timer.start(1000)

        self._setup_subscriptions()

    # -- UI --------------------------------------------------------------------

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)
        layout.setSpacing(6)

        self._banner = AuthorityBanner()
        layout.addWidget(self._banner)

        grid = QGridLayout()
        grid.setSpacing(6)
        self._heading = HeadingGauge()
        self._speed = SpeedGauge(arc_max=self._config.speed_arc_max)
        self._steering = CenterZeroGauge(
            label='Steering', neg_caption='port', pos_caption='stbd')
        self._throttle = CenterZeroGauge(
            label='Throttle', neg_caption='reverse', pos_caption='ahead')
        self._battery = BatteryGauge(
            warn_v=self._config.battery_warn_v,
            critical_v=self._config.battery_critical_v)
        grid.addWidget(self._heading, 0, 0)
        grid.addWidget(self._speed, 0, 1)
        grid.addWidget(self._steering, 1, 0)
        grid.addWidget(self._throttle, 1, 1)
        grid.addWidget(self._battery, 0, 2, 2, 1)
        for c in range(3):
            grid.setColumnStretch(c, 1)
        layout.addLayout(grid, 1)

        self._environment = EnvironmentPanel()
        layout.addWidget(self._environment)

    # -- Config ----------------------------------------------------------------

    def load_config(self, config: BoatStateConfig):
        """Apply a new configuration and rebuild subscriptions."""
        self._teardown_subscriptions()
        self._config = config
        self._speed.set_arc_max(config.speed_arc_max)
        self._battery.set_thresholds(config.battery_warn_v, config.battery_critical_v)
        self._setup_subscriptions()

    def get_config(self) -> BoatStateConfig:
        return self._config

    def shutdown(self):
        self._stale_timer.stop()
        self._teardown_subscriptions()

    # -- Subscriptions ---------------------------------------------------------

    def _topic_specs(self):
        """Yield (source-name, msg-class, topic) for every configured source."""
        from nav_msgs.msg import Odometry
        from geometry_msgs.msg import TwistStamped
        from sensor_msgs.msg import BatteryState, Temperature
        from std_msgs.msg import String
        from marine_interfaces.msg import Helm, SoundSpeed
        from mavros_msgs.msg import RCIn, RCOut, State

        cfg = self._config
        return [
            ('odom', Odometry, cfg.odom_topic),
            ('cmd_vel', TwistStamped, cfg.cmd_vel_topic),
            ('helm', Helm, cfg.helm_topic),
            ('fcu_state', State, cfg.fcu_state_topic),
            ('piloting_mode', String, cfg.piloting_mode_topic),
            ('rc_out', RCOut, cfg.rc_out_topic),
            ('rc_in', RCIn, cfg.rc_in_topic),
            ('battery', BatteryState, cfg.battery_topic),
            ('sound_speed', SoundSpeed, cfg.sound_speed_topic),
            ('water_temp', Temperature, cfg.water_temp_topic),
        ]

    def _setup_subscriptions(self):
        try:
            specs = self._topic_specs()
        except Exception as exc:  # message packages unavailable
            self._node.get_logger().error(
                f'Cannot import message types: {exc}')
            return
        now = time.monotonic()
        for name, msg_class, topic in specs:
            if not topic:
                continue
            sub = self._node.create_subscription(
                msg_class, topic,
                lambda msg, n=name: self._msg_received.emit(n, msg),
                10)
            self._subscriptions[name] = sub
            self._last_update[name] = now

    def _teardown_subscriptions(self):
        for sub in self._subscriptions.values():
            self._node.destroy_subscription(sub)
        self._subscriptions.clear()
        self._last_update.clear()

    # -- Qt-thread message handling --------------------------------------------

    def _handle_message(self, source, msg):
        self._last_update[source] = time.monotonic()
        handler = getattr(self, f'_on_{source}', None)
        if handler is not None:
            handler(msg)

    def _on_odom(self, msg):
        q = msg.pose.pose.orientation
        yaw = quaternion_to_yaw(q.x, q.y, q.z, q.w)
        self._heading.set_heading(enu_yaw_to_compass(yaw))

        lin = msg.twist.twist.linear
        vx, vy = lin.x, lin.y
        sog = math.hypot(vx, vy)
        self._speed.set_speed(sog)

        # Course over ground, gated by the min-speed threshold.
        if sog >= self._config.cog_min_speed:
            if self._config.velocity_reference == 'body':
                # Rotate body velocity into the ENU world frame.
                vx_e = vx * math.cos(yaw) - vy * math.sin(yaw)
                vy_e = vx * math.sin(yaw) + vy * math.cos(yaw)
            else:
                vx_e, vy_e = vx, vy
            cog = enu_yaw_to_compass(math.atan2(vy_e, vx_e))
            self._heading.set_cog(cog)
        else:
            self._heading.set_cog(None)

    def _on_cmd_vel(self, msg):
        lin = msg.twist.linear
        self._speed.set_commanded(math.hypot(lin.x, lin.y),
                                  stale=self._commanded_greyed())

    def _on_helm(self, msg):
        grey = self._commanded_greyed()
        self._throttle.set_commanded(msg.throttle, stale=grey)
        self._steering.set_commanded(msg.rudder, stale=grey)

    def _on_fcu_state(self, msg):
        self._fcu_mode = msg.mode
        self._armed = msg.armed
        self._refresh_banner()

    def _on_piloting_mode(self, msg):
        self._piloting_mode = msg.data
        self._refresh_banner()

    def _on_rc_out(self, msg):
        channels = list(msg.channels)
        throttle = self._config.channel_unit_value(channels, 'throttle')
        steering = self._config.channel_unit_value(channels, 'steering')
        if throttle is not None:
            self._throttle.set_value(throttle)
        if steering is not None:
            self._steering.set_value(steering)

    def _on_rc_in(self, msg):
        # Freshness only — used to grey commanded markers under RC authority.
        pass

    def _on_battery(self, msg):
        pct = msg.percentage if msg.percentage >= 0 else None
        self._battery.set_battery(
            voltage=msg.voltage, percentage=pct, current=msg.current)

    def _on_sound_speed(self, msg):
        self._environment.set_sound_speed(msg.sound_speed)

    def _on_water_temp(self, msg):
        self._environment.set_water_temp(msg.temperature)

    # -- Authority / staleness -------------------------------------------------

    def _effective_fcu_mode(self):
        return self._fcu_mode if self._armed else 'DISARMED'

    def _refresh_banner(self):
        banner_stale = self._is_source_stale('fcu_state')
        self._banner.set_modes(
            self._effective_fcu_mode(), self._piloting_mode, stale=banner_stale)

    def _is_source_stale(self, source):
        last = self._last_update.get(source)
        if last is None:
            return True
        age = time.monotonic() - last
        return is_stale(age, self._config.stale_timeout_for(source))

    def _commanded_greyed(self):
        """True when commanded markers should grey out for lost RC input."""
        state = resolve_authority(self._effective_fcu_mode(), self._piloting_mode)
        last = self._last_update.get('rc_in')
        age = time.monotonic() - last if last is not None else 1e9
        return should_grey_commanded(
            state.source, age, self._config.stale_timeout_for('rc_in'))

    def _check_stale(self):
        """1 Hz sweep: grey gauges whose source has gone stale."""
        if self._is_source_stale('odom'):
            self._heading.set_heading(self._heading._heading, stale=True)
            self._speed.set_speed(None)
            self._heading.set_cog(None)
        if self._is_source_stale('battery'):
            self._battery.set_battery(stale=True)
        if self._is_source_stale('sound_speed'):
            self._environment.set_sound_speed(None, stale=True)
        if self._is_source_stale('water_temp'):
            self._environment.set_water_temp(None, stale=True)
        self._refresh_banner()
