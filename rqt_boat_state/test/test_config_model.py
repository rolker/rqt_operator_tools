"""Tests for config_model: authority resolution, channel math, conversions,
and config YAML round-trip.  Pure Python — no Qt, no ROS, no display."""

import math

from rqt_boat_state.config_model import (
    AuthoritySource,
    BoatStateConfig,
    enu_yaw_to_compass,
    mps_to_knots,
    pwm_to_unit,
    quaternion_to_yaw,
    resolve_authority,
)


class TestResolveAuthority:
    def test_disarmed_on_empty(self):
        state = resolve_authority('', '')
        assert state.source == AuthoritySource.DISARMED
        assert state.color_token == 'disarmed'

    def test_disarmed_literal(self):
        assert resolve_authority('DISARMED', 'autonomous').source \
            == AuthoritySource.DISARMED

    def test_manual_is_rc(self):
        state = resolve_authority('MANUAL', 'standby')
        assert state.source == AuthoritySource.RC
        assert 'RC' in state.label

    def test_hold(self):
        assert resolve_authority('HOLD', 'autonomous').source \
            == AuthoritySource.HOLD

    def test_guided_autonomous(self):
        assert resolve_authority('GUIDED', 'autonomous').source \
            == AuthoritySource.AUTONOMY

    def test_guided_joystick(self):
        assert resolve_authority('GUIDED', 'joystick').source \
            == AuthoritySource.JOYSTICK

    def test_guided_standby(self):
        # STANDBY only on an EXPLICIT standby piloting_mode.
        assert resolve_authority('GUIDED', 'standby').source \
            == AuthoritySource.STANDBY

    def test_guided_unknown_piloting_is_not_standby(self):
        # Empty piloting_mode (never received) under an offboard FCU mode must
        # NOT read as STANDBY — that would be a fail-unsafe display while an
        # armed boat moves under GUIDED.  Report the FCU mode as UNKNOWN.
        state = resolve_authority('GUIDED', '')
        assert state.source == AuthoritySource.UNKNOWN
        assert 'GUIDED' in state.label

    def test_case_insensitive(self):
        assert resolve_authority('guided', 'AUTONOMOUS').source \
            == AuthoritySource.AUTONOMY

    def test_unknown_fcu_falls_through_to_piloting(self):
        # A new/unrecognized FCU mode still resolves by who the helm manager
        # says is driving rather than erroring.
        assert resolve_authority('SMARTRTL', 'joystick').source \
            == AuthoritySource.JOYSTICK

    def test_unrecognized_piloting_is_unknown_not_standby(self):
        # An unrecognized (non-empty) piloting_mode surfaces as UNKNOWN with the
        # raw mode shown, rather than being silently mislabeled STANDBY.
        state = resolve_authority('GUIDED', 'whatever')
        assert state.source == AuthoritySource.UNKNOWN


class TestPwmToUnit:
    def test_center_is_zero(self):
        assert pwm_to_unit(1500) == 0.0

    def test_full_forward(self):
        assert pwm_to_unit(2000) == 1.0

    def test_full_reverse(self):
        assert pwm_to_unit(1000) == -1.0

    def test_clamps_high(self):
        assert pwm_to_unit(2500) == 1.0

    def test_clamps_low(self):
        assert pwm_to_unit(500) == -1.0

    def test_midpoint(self):
        assert pwm_to_unit(1750) == 0.5

    def test_custom_calibration(self):
        assert pwm_to_unit(1100, center=1100, half_range=400) == 0.0

    def test_zero_half_range_returns_zero(self):
        assert pwm_to_unit(1800, half_range=0) == 0.0


class TestChannelUnitValue:
    def test_throttle_default_channel(self):
        cfg = BoatStateConfig(rc_channel_map={'throttle': [0], 'steering': [2]})
        assert cfg.channel_unit_value([2000, 1500, 1500], 'throttle') == 1.0

    def test_steering_index(self):
        cfg = BoatStateConfig(rc_channel_map={'throttle': [0], 'steering': [2]})
        assert cfg.channel_unit_value([1500, 1500, 1000], 'steering') == -1.0

    def test_mean_across_channels(self):
        cfg = BoatStateConfig(rc_channel_map={'throttle': [0, 1]})
        # 2000 → +1, 1500 → 0; mean = 0.5.
        assert cfg.channel_unit_value([2000, 1500], 'throttle') == 0.5

    def test_out_of_range_index_skipped(self):
        cfg = BoatStateConfig(rc_channel_map={'throttle': [0, 5]})
        assert cfg.channel_unit_value([2000], 'throttle') == 1.0

    def test_no_channels_returns_none(self):
        cfg = BoatStateConfig(rc_channel_map={'throttle': [5]})
        assert cfg.channel_unit_value([1500], 'throttle') is None


class TestConversions:
    def test_mps_to_knots(self):
        assert math.isclose(mps_to_knots(1.0), 1.943844, rel_tol=1e-6)

    def test_enu_yaw_to_compass_east(self):
        # Facing East (yaw 0) → compass 090.
        assert math.isclose(enu_yaw_to_compass(0.0), 90.0)

    def test_enu_yaw_to_compass_north(self):
        # Facing North (yaw +90°) → compass 000.
        assert math.isclose(enu_yaw_to_compass(math.radians(90.0)), 0.0)

    def test_enu_yaw_to_compass_west(self):
        # Facing West (yaw 180°) → compass 270.
        assert math.isclose(enu_yaw_to_compass(math.radians(180.0)), 270.0)

    def test_quaternion_to_yaw_identity(self):
        assert math.isclose(quaternion_to_yaw(0, 0, 0, 1), 0.0)

    def test_quaternion_to_yaw_90(self):
        # 90° about Z.
        q = (0, 0, math.sin(math.pi / 4), math.cos(math.pi / 4))
        assert math.isclose(quaternion_to_yaw(*q), math.pi / 2, rel_tol=1e-6)


class TestConfigRoundTrip:
    def test_defaults(self):
        cfg = BoatStateConfig()
        assert cfg.odom_topic == '/bizzy/odom'
        assert cfg.rc_channel_map == {'throttle': [0, 1], 'steering': [2, 3]}
        assert cfg.battery_warn_v == 23.5

    def test_yaml_roundtrip(self):
        cfg = BoatStateConfig(
            odom_topic='/foo/odom',
            speed_arc_max=8.0,
            cmd_rotation_max=2.0,
            steering_reversed=False,
            heartbeat_topic='/foo/hb',
            battery_warn_v=23.0,
            rc_channel_map={'throttle': [4], 'steering': [5, 6]},
            stale_timeouts={'odom': 1.0},
        )
        restored = BoatStateConfig.from_yaml(cfg.to_yaml())
        assert restored.odom_topic == '/foo/odom'
        assert restored.speed_arc_max == 8.0
        assert restored.cmd_rotation_max == 2.0
        assert restored.steering_reversed is False
        assert restored.heartbeat_topic == '/foo/hb'
        assert restored.battery_warn_v == 23.0
        assert restored.rc_channel_map == {'throttle': [4], 'steering': [5, 6]}
        assert restored.stale_timeouts == {'odom': 1.0}

    def test_new_field_defaults(self):
        cfg = BoatStateConfig()
        assert cfg.steering_reversed is True       # BizzyBoat servo convention
        assert cfg.cmd_rotation_max == 1.0
        assert cfg.heartbeat_topic == 'marine/heartbeat'

    def test_from_empty_yaml_keeps_defaults(self):
        cfg = BoatStateConfig.from_yaml('')
        assert cfg.odom_topic == '/bizzy/odom'
        assert cfg.cmd_vel_topic == 'cmd_vel'

    def test_partial_dict_keeps_defaults(self):
        cfg = BoatStateConfig.from_dict({'speed_arc_max': 12.0})
        assert cfg.speed_arc_max == 12.0
        assert cfg.battery_warn_v == BoatStateConfig().battery_warn_v

    def test_stale_timeout_for_fallback(self):
        cfg = BoatStateConfig(stale_timeout=3.0, stale_timeouts={'battery': 10.0})
        assert cfg.stale_timeout_for('battery') == 10.0
        assert cfg.stale_timeout_for('odom') == 3.0
