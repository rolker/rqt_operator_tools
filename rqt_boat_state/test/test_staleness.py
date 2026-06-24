"""Tests for the Qt-free staleness gating, RC-in freshness gating, and the
SoundSpeed-NaN handling, all exercised in isolation (no Qt, no display)."""

from rqt_boat_state.config_model import (
    AuthoritySource,
    IndicatorLevel,
    format_measurement,
    is_stale,
    is_valid_measurement,
    level_for_age,
    should_grey_commanded,
)


class TestIsStale:
    def test_fresh(self):
        assert not is_stale(0.5, 2.0)

    def test_stale(self):
        assert is_stale(3.0, 2.0)

    def test_boundary_not_stale(self):
        assert not is_stale(2.0, 2.0)

    def test_disabled_timeout_never_stale(self):
        assert not is_stale(1e6, 0.0)


class TestLevelForAge:
    def test_ok(self):
        assert level_for_age(0.5, 2.0) == IndicatorLevel.OK

    def test_warn(self):
        assert level_for_age(3.0, 2.0) == IndicatorLevel.WARN

    def test_error_default_3x(self):
        # Default error timeout = 3× warn = 6 s.
        assert level_for_age(7.0, 2.0) == IndicatorLevel.ERROR

    def test_explicit_error_timeout(self):
        assert level_for_age(5.0, 2.0, error_timeout=4.0) == IndicatorLevel.ERROR

    def test_disabled_is_ok(self):
        assert level_for_age(1e6, 0.0) == IndicatorLevel.OK


class TestShouldGreyCommanded:
    def test_rc_stale_greys(self):
        assert should_grey_commanded(AuthoritySource.RC, 3.0, 2.0)

    def test_rc_fresh_no_grey(self):
        assert not should_grey_commanded(AuthoritySource.RC, 0.5, 2.0)

    def test_autonomy_never_greyed_by_rc(self):
        # COG/commanded markers for autonomy don't depend on rc/in freshness.
        assert not should_grey_commanded(AuthoritySource.AUTONOMY, 100.0, 2.0)

    def test_joystick_unaffected(self):
        assert not should_grey_commanded(AuthoritySource.JOYSTICK, 100.0, 2.0)


class TestSoundSpeedNaN:
    def test_valid_number(self):
        assert is_valid_measurement(1500.0)

    def test_nan_invalid(self):
        assert not is_valid_measurement(float('nan'))

    def test_none_invalid(self):
        assert not is_valid_measurement(None)

    def test_inf_invalid(self):
        assert not is_valid_measurement(float('inf'))

    def test_format_nan_shows_dash(self):
        assert format_measurement(float('nan')) == '—'

    def test_format_none_shows_dash(self):
        assert format_measurement(None) == '—'

    def test_format_valid(self):
        assert format_measurement(1500.0, '{:.1f} m/s') == '1500.0 m/s'

    def test_format_custom_nan_text(self):
        assert format_measurement(float('nan'), nan_text='N/A') == 'N/A'
