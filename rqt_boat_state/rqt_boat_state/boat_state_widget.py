"""Core boat-state widget — subscriptions, layout, and data distribution.

Owns every ROS subscription, a 1 Hz stale timer, and the child gauges,
authority banner, and environment panel.  ROS callbacks run on the executor
thread and hand messages to the Qt thread via a queued signal (the pattern
borrowed from ``AnnunciatorWidget``).
"""

import importlib
import math
import time

from rclpy.qos import qos_profile_sensor_data

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
    is_valid_measurement,
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

    # Sensor/telemetry sources (mavros, sensor_msgs, the ENU odom) publish
    # BEST_EFFORT; a default RELIABLE subscription silently fails to match them
    # and the panel sits dead.  Subscribe to those with sensor-data QoS — the
    # same fix rqt_sonar_waterfall applies to its BEST_EFFORT publishers.
    # Command/string sources (cmd_vel, helm, heartbeat) are RELIABLE, so
    # they keep a plain depth-10 queue.
    _SENSOR_QOS = qos_profile_sensor_data
    _RELIABLE_DEPTH = 10

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

        # Legend: make the measured-vs-commanded colour convention explicit so
        # a glance distinguishes the live value from the command everywhere.
        layout.addLayout(self._build_legend())

        grid = QGridLayout()
        grid.setSpacing(6)
        self._heading = HeadingGauge()
        self._speed = SpeedGauge(arc_max=self._config.speed_arc_max)
        self._steering = CenterZeroGauge(
            label='Steering', neg_caption='port', pos_caption='stbd',
            secondary_label='cmd rot')
        self._throttle = CenterZeroGauge(
            label='Throttle', neg_caption='reverse', pos_caption='ahead',
            orientation='vertical')
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

    def _build_legend(self):
        """A one-line key: blue = measured/actual, amber = commanded."""
        from python_qt_binding.QtWidgets import QHBoxLayout, QLabel
        from .gauges import ACTUAL_COLOR, COMMAND_COLOR, COURSE_COLOR

        row = QHBoxLayout()
        row.setContentsMargins(6, 0, 6, 0)
        row.setSpacing(12)

        def chip(color, text):
            lab = QLabel(f'⬤ {text}')
            lab.setStyleSheet(f'color: {color.name()}; font-size: 11px;')
            return lab

        row.addWidget(chip(ACTUAL_COLOR, 'measured / actual'))
        row.addWidget(chip(COMMAND_COLOR, 'commanded'))
        row.addWidget(chip(COURSE_COLOR, 'course (COG)'))
        row.addStretch(1)
        return row

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
        """Yield (source-name, msg-class, topic) for every importable source.

        Each message type is resolved independently so a single missing
        package (e.g. ``marine_interfaces`` unbuilt in this worktree) disables
        only the sources that depend on it rather than aborting every
        subscription.  Sources whose message package cannot be imported are
        logged and skipped.
        """
        cfg = self._config
        sensor, reliable = self._SENSOR_QOS, self._RELIABLE_DEPTH
        wanted = [
            ('odom', 'nav_msgs.msg', 'Odometry', cfg.odom_topic, sensor),
            ('cmd_vel', 'geometry_msgs.msg', 'TwistStamped', cfg.cmd_vel_topic,
             reliable),
            ('helm', 'marine_interfaces.msg', 'Helm', cfg.helm_topic, reliable),
            ('fcu_state', 'mavros_msgs.msg', 'State', cfg.fcu_state_topic, sensor),
            ('heartbeat', 'marine_interfaces.msg', 'Heartbeat',
             cfg.heartbeat_topic, reliable),
            ('rc_out', 'mavros_msgs.msg', 'RCOut', cfg.rc_out_topic, sensor),
            ('rc_in', 'mavros_msgs.msg', 'RCIn', cfg.rc_in_topic, sensor),
            ('battery', 'sensor_msgs.msg', 'BatteryState', cfg.battery_topic,
             sensor),
            ('sound_speed', 'marine_interfaces.msg', 'SoundSpeed',
             cfg.sound_speed_topic, sensor),
            ('water_temp', 'sensor_msgs.msg', 'Temperature', cfg.water_temp_topic,
             sensor),
        ]
        specs = []
        for name, module, attr, topic, qos in wanted:
            try:
                msg_class = getattr(importlib.import_module(module), attr)
            except (ImportError, AttributeError) as exc:
                self._node.get_logger().warning(
                    f'Skipping {name}: cannot import {module}.{attr} ({exc})')
                continue
            specs.append((name, msg_class, topic, qos))
        return specs

    def _setup_subscriptions(self):
        specs = self._topic_specs()
        for name, msg_class, topic, qos in specs:
            if not topic:
                continue
            sub = self._node.create_subscription(
                msg_class, topic,
                lambda msg, n=name: self._msg_received.emit(n, msg),
                qos)
            self._subscriptions[name] = sub
            # Seed "never received" (not ``now``) so a source that never
            # publishes reads stale immediately rather than fresh for one
            # stale_timeout after startup / config reload.
            self._last_update[name] = None

    def _teardown_subscriptions(self):
        for sub in self._subscriptions.values():
            self._node.destroy_subscription(sub)
        self._subscriptions.clear()
        self._last_update.clear()

    # -- Qt-thread message handling --------------------------------------------

    def _handle_message(self, source, msg):
        # A message queued before a ``load_config`` teardown can arrive after
        # the source was destroyed; drop it so a torn-down source can't be
        # marked fresh (and resurrected in ``_last_update``).
        if source not in self._subscriptions:
            return
        self._last_update[source] = time.monotonic()
        handler = getattr(self, f'_on_{source}', None)
        if handler is not None:
            handler(msg)

    def _on_odom(self, msg):
        q = msg.pose.pose.orientation
        yaw = quaternion_to_yaw(q.x, q.y, q.z, q.w)
        # A NaN/inf orientation must blank the compass ("---"), not render
        # "nan°" — same finiteness gate the battery/speed gauges already apply.
        heading = enu_yaw_to_compass(yaw)
        self._heading.set_heading(heading if is_valid_measurement(heading) else None)

        lin = msg.twist.twist.linear
        vx, vy = lin.x, lin.y
        sog = math.hypot(vx, vy)
        self._speed.set_speed(sog)  # gates non-finite values itself

        # Course over ground, gated by the min-speed threshold (and finiteness:
        # a NaN yaw or speed yields no COG arrow rather than a garbage bearing).
        if is_valid_measurement(sog) and is_valid_measurement(yaw) \
                and sog >= self._config.cog_min_speed:
            # nav_msgs/Odometry twist is in child_frame_id (base_link) per
            # REP-103, so rotate the body-frame velocity into ENU to get the
            # ground course. (Verified against /bizzy/odom: forward vx with a
            # body rotation yields COG == heading; treating it as ground-frame
            # gave a spurious due-East COG.)
            vx_e = vx * math.cos(yaw) - vy * math.sin(yaw)
            vy_e = vx * math.sin(yaw) + vy * math.cos(yaw)
            cog = enu_yaw_to_compass(math.atan2(vy_e, vx_e))
            self._heading.set_cog(cog)
        else:
            self._heading.set_cog(None)

    def _on_cmd_vel(self, msg):
        grey = self._commanded_greyed()
        lin = msg.twist.linear
        self._speed.set_commanded(math.hypot(lin.x, lin.y), stale=grey)
        # Commanded yaw rate (autonomy) under the rudder bar.  +angular.z is
        # CCW (port) per REP-103, so negate to put a port turn on the port
        # (left) side; text shows magnitude with a P/S direction letter.
        rot = msg.twist.angular.z
        if is_valid_measurement(rot):
            norm = max(-1.0, min(1.0, -rot / self._config.cmd_rotation_max))
            deg = math.degrees(rot)
            side = 'P' if rot > 0 else ('S' if rot < 0 else '')
            self._steering.set_secondary(
                norm, f'{abs(deg):.0f}°/s {side}'.strip(), stale=grey)

    def _on_helm(self, msg):
        grey = self._commanded_greyed()
        self._throttle.set_commanded(msg.throttle, stale=grey)
        self._steering.set_commanded(msg.rudder, stale=grey)

    def _on_fcu_state(self, msg):
        self._fcu_mode = msg.mode
        self._armed = msg.armed
        self._refresh_banner()

    def _on_heartbeat(self, msg):
        # The helm manager stamps the piloting mode into the Heartbeat's
        # KeyValue list (key 'piloting_mode'); pull it out for the banner.
        for kv in msg.values:
            if kv.key == 'piloting_mode':
                self._piloting_mode = kv.value
                break
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
            self._heading.mark_stale()
            self._speed.set_speed(None)
            self._heading.set_cog(None)
        # Steering/throttle actuals come from rc_out; their commanded ghosts
        # from helm; the speed ghost from cmd_vel.  Grey each frozen overlay
        # once its own source stops publishing so it can't read as live.
        if self._is_source_stale('rc_out'):
            self._steering.mark_stale()
            self._throttle.mark_stale()
        if self._is_source_stale('helm'):
            self._steering.mark_commanded_stale()
            self._throttle.mark_commanded_stale()
        if self._is_source_stale('cmd_vel'):
            self._speed.mark_commanded_stale()
            self._steering.mark_secondary_stale()
        if self._is_source_stale('battery'):
            self._battery.set_battery(stale=True)
        if self._is_source_stale('sound_speed'):
            self._environment.set_sound_speed(None, stale=True)
        if self._is_source_stale('water_temp'):
            self._environment.set_water_temp(None, stale=True)
        self._refresh_banner()
