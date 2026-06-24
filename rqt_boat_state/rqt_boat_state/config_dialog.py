"""Settings dialog for the boat-state panel.

Covers every ``BoatStateConfig`` field, including the environment topics, and
documents the ENU/NED velocity convention and the default ArduRover RC-out
channel map (SERVO1→idx 0, SERVO3→idx 2) with inline help text.
"""

import json

from python_qt_binding.QtWidgets import (
    QCheckBox,
    QDialog,
    QDialogButtonBox,
    QDoubleSpinBox,
    QFormLayout,
    QGroupBox,
    QLabel,
    QLineEdit,
    QMessageBox,
    QVBoxLayout,
)

from .config_model import BoatStateConfig, _coerce_channel_map


class ConfigDialog(QDialog):
    """Dialog for configuring the boat-state panel."""

    def __init__(self, config: BoatStateConfig, parent=None):
        super().__init__(parent)
        self.setWindowTitle('Boat State Settings')
        self.setMinimumSize(480, 600)

        main_layout = QVBoxLayout(self)

        # -- Topics ------------------------------------------------------------
        topics_group = QGroupBox('Topics')
        topics_form = QFormLayout(topics_group)
        self._topic_edits = {}
        for key, label, value in (
            ('odom', 'Odometry (nav_msgs/Odometry):', config.odom_topic),
            ('cmd_vel', 'Commanded vel (geometry_msgs/TwistStamped):',
             config.cmd_vel_topic),
            ('helm', 'Helm (marine_interfaces/Helm):', config.helm_topic),
            ('fcu_state', 'FCU state (mavros_msgs/State):', config.fcu_state_topic),
            ('heartbeat', 'Heartbeat (marine_interfaces/Heartbeat):',
             config.heartbeat_topic),
            ('rc_out', 'RC out (mavros_msgs/RCOut):', config.rc_out_topic),
            ('rc_in', 'RC in (mavros_msgs/RCIn):', config.rc_in_topic),
            ('battery', 'Battery (sensor_msgs/BatteryState):', config.battery_topic),
            ('sound_speed', 'Sound speed (marine_interfaces/SoundSpeed):',
             config.sound_speed_topic),
            ('water_temp', 'Water temp (sensor_msgs/Temperature):',
             config.water_temp_topic),
        ):
            edit = QLineEdit(value)
            self._topic_edits[key] = edit
            topics_form.addRow(label, edit)
        main_layout.addWidget(topics_group)

        # -- RC channel map ----------------------------------------------------
        rc_group = QGroupBox('RC channel map (PWM µs → −1..1)')
        rc_form = QFormLayout(rc_group)
        help_label = QLabel(
            'Channel indices into RCOut.channels. ArduRover default: '
            'throttle = SERVO1 (idx 0), steering = SERVO3 (idx 2). '
            'Lists may name multiple channels (mean is used).')
        help_label.setWordWrap(True)
        help_label.setStyleSheet('color: #888; font-size: 10px;')
        rc_form.addRow(help_label)

        self._channel_map_edit = QLineEdit(
            json.dumps(config.rc_channel_map))
        self._channel_map_edit.setPlaceholderText(
            '{"throttle": [0, 1], "steering": [2, 3]}')
        rc_form.addRow('Channel map (JSON):', self._channel_map_edit)

        self._pwm_center_spin = self._spin(800.0, 2200.0, config.pwm_center, ' µs')
        rc_form.addRow('PWM center:', self._pwm_center_spin)
        self._pwm_half_spin = self._spin(50.0, 1000.0, config.pwm_half_range, ' µs')
        rc_form.addRow('PWM half-range:', self._pwm_half_spin)
        self._steer_rev_check = QCheckBox('Reverse steering (high PWM = port)')
        self._steer_rev_check.setChecked(config.steering_reversed)
        rc_form.addRow('', self._steer_rev_check)
        main_layout.addWidget(rc_group)

        # -- Gauge ranges -----------------------------------------------------
        conv_group = QGroupBox('Gauge ranges')
        conv_form = QFormLayout(conv_group)
        conv_help = QLabel(
            'Odom from mru_transform is ENU (REP-103) with body-frame twist; '
            'COG is always computed by rotating into ENU (no frame selector).')
        conv_help.setWordWrap(True)
        conv_help.setStyleSheet('color: #888; font-size: 10px;')
        conv_form.addRow(conv_help)

        self._cog_min_spin = self._spin(0.0, 10.0, config.cog_min_speed, ' m/s')
        conv_form.addRow('COG min speed:', self._cog_min_spin)
        self._speed_max_spin = self._spin(0.5, 50.0, config.speed_arc_max, ' kn')
        conv_form.addRow('Speed arc max:', self._speed_max_spin)
        self._rot_max_spin = self._spin(0.1, 5.0, config.cmd_rotation_max, ' rad/s')
        conv_form.addRow('Cmd rotation max:', self._rot_max_spin)
        self._batt_warn_spin = self._spin(0.0, 60.0, config.battery_warn_v, ' V')
        conv_form.addRow('Battery warn:', self._batt_warn_spin)
        self._batt_crit_spin = self._spin(0.0, 60.0, config.battery_critical_v, ' V')
        conv_form.addRow('Battery critical:', self._batt_crit_spin)
        self._stale_spin = self._spin(0.0, 60.0, config.stale_timeout, ' s')
        conv_form.addRow('Default stale timeout:', self._stale_spin)
        main_layout.addWidget(conv_group)

        # Keep the prior config so unedited fields (e.g. per-source stale
        # overrides not surfaced here) survive a round-trip.
        self._base_config = config

        button_box = QDialogButtonBox(
            QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        button_box.accepted.connect(self.accept)
        button_box.rejected.connect(self.reject)
        main_layout.addWidget(button_box)

    @staticmethod
    def _spin(lo, hi, value, suffix):
        spin = QDoubleSpinBox()
        spin.setRange(lo, hi)
        spin.setValue(value)
        spin.setSuffix(suffix)
        spin.setDecimals(2)
        return spin

    def get_config(self) -> BoatStateConfig:
        """Build a BoatStateConfig from the dialog fields."""
        try:
            channel_map = json.loads(self._channel_map_edit.text())
            if not isinstance(channel_map, dict):
                raise ValueError
            channel_map = _coerce_channel_map(channel_map)
        except (ValueError, TypeError):
            channel_map = self._base_config.rc_channel_map
            QMessageBox.warning(
                self, 'Invalid channel map',
                'The RC channel map is not valid JSON; keeping the previous '
                'value:\n\n'
                f'{json.dumps(channel_map)}')

        return BoatStateConfig(
            odom_topic=self._topic_edits['odom'].text(),
            cmd_vel_topic=self._topic_edits['cmd_vel'].text(),
            helm_topic=self._topic_edits['helm'].text(),
            fcu_state_topic=self._topic_edits['fcu_state'].text(),
            heartbeat_topic=self._topic_edits['heartbeat'].text(),
            rc_out_topic=self._topic_edits['rc_out'].text(),
            rc_in_topic=self._topic_edits['rc_in'].text(),
            battery_topic=self._topic_edits['battery'].text(),
            sound_speed_topic=self._topic_edits['sound_speed'].text(),
            water_temp_topic=self._topic_edits['water_temp'].text(),
            rc_channel_map=channel_map,
            pwm_center=self._pwm_center_spin.value(),
            pwm_half_range=self._pwm_half_spin.value(),
            steering_reversed=self._steer_rev_check.isChecked(),
            cog_min_speed=self._cog_min_spin.value(),
            speed_arc_max=self._speed_max_spin.value(),
            cmd_rotation_max=self._rot_max_spin.value(),
            battery_warn_v=self._batt_warn_spin.value(),
            battery_critical_v=self._batt_crit_spin.value(),
            stale_timeout=self._stale_spin.value(),
            stale_timeouts=dict(self._base_config.stale_timeouts),
        )
