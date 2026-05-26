"""Tests for threshold expression evaluation."""

from collections import namedtuple

import pytest

from rqt_annunciator.config_model import (
    IndicatorConfig,
    IndicatorLevel,
    evaluate_threshold,
    validate_expression,
    preview_expression,
)

# Stand-in for diagnostic_msgs/KeyValue (key/value string pair).
_KV = namedtuple('_KV', ['key', 'value'])


class TestEvaluateThreshold:
    def test_greater_than(self):
        assert evaluate_threshold('value > 10', 15) is True
        assert evaluate_threshold('value > 10', 5) is False

    def test_less_than(self):
        assert evaluate_threshold('value < 12.5', 12.0) is True
        assert evaluate_threshold('value < 12.5', 13.0) is False

    def test_equal(self):
        assert evaluate_threshold('value == 0', 0) is True
        assert evaluate_threshold('value == 0', 1) is False

    def test_not_equal(self):
        assert evaluate_threshold('value != 0', 1) is True

    def test_greater_equal(self):
        assert evaluate_threshold('value >= 10', 10) is True
        assert evaluate_threshold('value >= 10', 9) is False

    def test_less_equal(self):
        assert evaluate_threshold('value <= 10', 10) is True

    def test_chained_comparison(self):
        assert evaluate_threshold('10 < value < 20', 15) is True
        assert evaluate_threshold('10 < value < 20', 25) is False
        assert evaluate_threshold('10 < value < 20', 5) is False

    def test_boolean_and(self):
        assert evaluate_threshold('value > 10 and value < 20', 15) is True
        assert evaluate_threshold('value > 10 and value < 20', 25) is False

    def test_boolean_or(self):
        assert evaluate_threshold('value < 5 or value > 20', 3) is True
        assert evaluate_threshold('value < 5 or value > 20', 10) is False

    def test_not(self):
        assert evaluate_threshold('not value > 10', 5) is True
        assert evaluate_threshold('not value > 10', 15) is False

    def test_string_comparison(self):
        assert evaluate_threshold('value == "RTK"', 'RTK') is True
        assert evaluate_threshold('value == "RTK"', 'Float') is False

    def test_rejects_function_call(self):
        with pytest.raises(ValueError, match='Unsupported'):
            evaluate_threshold('len(value) > 0', 'test')

    def test_rejects_import(self):
        with pytest.raises(ValueError):
            evaluate_threshold('__import__("os")', 0)

    def test_rejects_attribute_access(self):
        with pytest.raises(ValueError, match='Unsupported'):
            evaluate_threshold('value.__class__', 0)

    def test_syntax_error(self):
        with pytest.raises(ValueError, match='Invalid expression syntax'):
            evaluate_threshold('value >', 0)


class TestValidateExpression:
    def test_valid(self):
        assert validate_expression('value > 10') is None
        assert validate_expression('value > 10 and value < 20') is None

    def test_invalid_syntax(self):
        result = validate_expression('value >')
        assert result is not None
        assert 'Syntax error' in result

    def test_unsafe_construct(self):
        result = validate_expression('len(value)')
        assert result is not None
        assert 'Unsupported' in result


class TestPreviewExpression:
    def test_pass(self):
        result = preview_expression('value > 10', 15)
        assert 'PASS' in result
        assert '15' in result

    def test_fail(self):
        result = preview_expression('value > 10', 5)
        assert 'FAIL' in result

    def test_error(self):
        result = preview_expression('value >', 5)
        assert 'Error' in result


class TestEvaluateDiagnostic:
    """End-to-end of the diagnostics threshold path (issue #35)."""

    def _battery(self):
        return IndicatorConfig(
            name='Battery',
            source='diagnostics',
            diagnostic_name='mavros: Battery',
            value_key='Voltage',
            format='{:.1f} V',
            threshold_warn='value < 23.0',
            threshold_error='value < 21.5',
        )

    def _values(self, voltage):
        return [_KV('Current', '0.0'), _KV('Voltage', str(voltage))]

    def test_native_ok_healthy_value_ok(self):
        level, text = self._battery().evaluate_diagnostic(
            IndicatorLevel.OK, self._values(24.0))
        assert level == IndicatorLevel.OK
        assert text == '24.0 V'

    def test_native_ok_warn_value_warn(self):
        level, _ = self._battery().evaluate_diagnostic(
            IndicatorLevel.OK, self._values(22.4))
        assert level == IndicatorLevel.WARN

    def test_native_ok_error_value_error(self):
        level, _ = self._battery().evaluate_diagnostic(
            IndicatorLevel.OK, self._values(21.0))
        assert level == IndicatorLevel.ERROR

    def test_native_error_healthy_value_stays_error(self):
        # Producer ERROR is never downgraded by a healthy threshold value.
        level, _ = self._battery().evaluate_diagnostic(
            IndicatorLevel.ERROR, self._values(24.0))
        assert level == IndicatorLevel.ERROR

    def test_native_warn_error_value_escalates(self):
        level, _ = self._battery().evaluate_diagnostic(
            IndicatorLevel.WARN, self._values(21.0))
        assert level == IndicatorLevel.ERROR

    def test_native_stale_masks_threshold_error(self):
        # Stale diagnostic isn't trusted for thresholding (deliberate).
        level, _ = self._battery().evaluate_diagnostic(
            IndicatorLevel.STALE, self._values(21.0))
        assert level == IndicatorLevel.STALE

    def test_missing_key_is_error(self):
        level, text = self._battery().evaluate_diagnostic(
            IndicatorLevel.OK, [_KV('Current', '0.0')])
        assert level == IndicatorLevel.ERROR
        assert text == 'Voltage?'

    def test_non_numeric_value_is_error(self):
        level, text = self._battery().evaluate_diagnostic(
            IndicatorLevel.OK, [_KV('Voltage', 'n/a')])
        assert level == IndicatorLevel.ERROR
        assert text == 'Voltage?'

    def test_no_thresholds_passes_native_level_and_first_value(self):
        # Backward compat: no thresholds → native level, first KeyValue text.
        config = IndicatorConfig(
            name='GPS', source='diagnostics', diagnostic_name='mavros: GPS')
        level, text = config.evaluate_diagnostic(
            IndicatorLevel.WARN, [_KV('Satellites', '12')], message='ok')
        assert level == IndicatorLevel.WARN
        assert text == '12.0'  # float-formatted with default '{}'

    def test_no_thresholds_no_values_falls_back_to_message(self):
        config = IndicatorConfig(
            name='GPS', source='diagnostics', diagnostic_name='mavros: GPS')
        level, text = config.evaluate_diagnostic(
            IndicatorLevel.OK, [], message='No fix')
        assert level == IndicatorLevel.OK
        assert text == 'No fix'
