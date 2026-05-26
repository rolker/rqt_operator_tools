"""Tests for config_model: parsing, serialization, and matching."""

from collections import namedtuple

import pytest

from rqt_annunciator.config_model import (
    AnnunciatorConfig,
    IndicatorConfig,
    IndicatorLevel,
    MatchMode,
)

# Stand-in for diagnostic_msgs/KeyValue (key/value string pair) so these
# stay pure-Python unit tests with no ROS message dependency.
_KV = namedtuple('_KV', ['key', 'value'])


class TestIndicatorConfig:
    def test_from_dict_topic(self):
        d = {
            'name': 'Battery',
            'source': 'topic',
            'topic': '/battery/voltage',
            'msg_type': 'std_msgs/msg/Float64',
            'value_field': 'data',
            'format': '{:.1f}V',
            'thresholds': {
                'warn': 'value < 12.5',
                'error': 'value < 11.5',
            },
        }
        config = IndicatorConfig.from_dict(d)
        assert config.name == 'Battery'
        assert config.source == 'topic'
        assert config.topic == '/battery/voltage'
        assert config.threshold_warn == 'value < 12.5'
        assert config.threshold_error == 'value < 11.5'

    def test_from_dict_diagnostics(self):
        d = {
            'name': 'NTP Sync',
            'source': 'diagnostics',
            'diagnostic_name': 'NTP offset',
            'match_mode': 'regex',
            'format': '{:.0f}ms',
        }
        config = IndicatorConfig.from_dict(d)
        assert config.source == 'diagnostics'
        assert config.diagnostic_name == 'NTP offset'
        assert config.match_mode == MatchMode.REGEX

    def test_roundtrip_yaml(self):
        original = IndicatorConfig(
            name='Test',
            source='topic',
            topic='/test',
            msg_type='std_msgs/msg/Float64',
            format='{:.2f}',
            threshold_warn='value > 10',
        )
        d = original.to_dict()
        restored = IndicatorConfig.from_dict(d)
        assert restored.name == original.name
        assert restored.topic == original.topic
        assert restored.threshold_warn == original.threshold_warn

    def test_matches_diagnostic_substring(self):
        config = IndicatorConfig(
            name='NTP',
            source='diagnostics',
            diagnostic_name='NTP offset',
            match_mode=MatchMode.SUBSTRING,
        )
        assert config.matches_diagnostic('NTP offset from router.op')
        assert not config.matches_diagnostic('CPU usage')

    def test_matches_diagnostic_exact(self):
        config = IndicatorConfig(
            name='NTP',
            source='diagnostics',
            diagnostic_name='NTP offset',
            match_mode=MatchMode.EXACT,
        )
        assert config.matches_diagnostic('NTP offset')
        assert not config.matches_diagnostic('NTP offset from router.op')

    def test_matches_diagnostic_regex(self):
        config = IndicatorConfig(
            name='NTP',
            source='diagnostics',
            diagnostic_name=r'NTP offset.*router',
            match_mode=MatchMode.REGEX,
        )
        assert config.matches_diagnostic('NTP offset from router.op')
        assert not config.matches_diagnostic('NTP offset from salmon')

    def test_diagnostics_value_key_thresholds_roundtrip(self):
        original = IndicatorConfig(
            name='Battery',
            source='diagnostics',
            diagnostic_name='mavros: Battery',
            value_key='Voltage',
            threshold_warn='value < 23.0',
            threshold_error='value < 21.5',
        )
        d = original.to_dict()
        assert d['value_key'] == 'Voltage'
        assert d['thresholds'] == {'warn': 'value < 23.0', 'error': 'value < 21.5'}
        restored = IndicatorConfig.from_dict(d)
        assert restored.source == 'diagnostics'
        assert restored.value_key == 'Voltage'
        assert restored.threshold_warn == 'value < 23.0'
        assert restored.threshold_error == 'value < 21.5'

    def test_to_dict_diagnostics_minimal_omits_new_keys(self):
        # Backward compat: a plain diagnostics row emits no value_key/thresholds.
        config = IndicatorConfig(
            name='GPS', source='diagnostics', diagnostic_name='mavros: GPS')
        d = config.to_dict()
        assert 'value_key' not in d
        assert 'thresholds' not in d

    def test_to_dict_normalizes_value_key(self):
        config = IndicatorConfig(
            name='B', source='diagnostics', diagnostic_name='t', value_key=' Voltage ')
        assert config.to_dict()['value_key'] == 'Voltage'

    def test_to_dict_omits_whitespace_only_thresholds(self):
        # Whitespace-only thresholds are "no threshold" — don't serialize them
        # (keeps to_dict consistent with has_thresholds/evaluate_level).
        config = IndicatorConfig(
            name='B', source='diagnostics', diagnostic_name='test',
            threshold_warn='   ', threshold_error='\t\n')
        d = config.to_dict()
        assert 'thresholds' not in d


class TestSelectKeyValue:
    def test_exact_match(self):
        config = IndicatorConfig(name='B', source='diagnostics', value_key='Voltage')
        values = [_KV('Current', '0.0'), _KV('Voltage', '22.4')]
        assert config.select_keyvalue(values) == '22.4'

    def test_empty_value_key_uses_first(self):
        config = IndicatorConfig(name='B', source='diagnostics')
        values = [_KV('Voltage', '22.4'), _KV('Current', '0.0')]
        assert config.select_keyvalue(values) == '22.4'

    def test_value_key_not_found_returns_none(self):
        config = IndicatorConfig(name='B', source='diagnostics', value_key='Voltage')
        values = [_KV('Current', '0.0')]
        assert config.select_keyvalue(values) is None

    def test_no_values_returns_none(self):
        config = IndicatorConfig(name='B', source='diagnostics', value_key='Voltage')
        assert config.select_keyvalue([]) is None

    def test_value_key_whitespace_tolerant(self):
        # Benign surrounding whitespace in value_key must still match (else the
        # row would wrongly go ERROR). Copilot review finding, PR #36.
        config = IndicatorConfig(name='B', source='diagnostics', value_key='Voltage ')
        values = [_KV('Current', '0.0'), _KV('Voltage', '22.4')]
        assert config.select_keyvalue(values) == '22.4'


class TestCombineLevels:
    def test_native_ok_keeps_threshold(self):
        assert IndicatorConfig.combine_levels(
            IndicatorLevel.OK, IndicatorLevel.WARN) == IndicatorLevel.WARN
        assert IndicatorConfig.combine_levels(
            IndicatorLevel.OK, IndicatorLevel.ERROR) == IndicatorLevel.ERROR

    def test_native_error_not_downgraded(self):
        assert IndicatorConfig.combine_levels(
            IndicatorLevel.ERROR, IndicatorLevel.OK) == IndicatorLevel.ERROR

    def test_more_severe_wins(self):
        assert IndicatorConfig.combine_levels(
            IndicatorLevel.WARN, IndicatorLevel.ERROR) == IndicatorLevel.ERROR
        assert IndicatorConfig.combine_levels(
            IndicatorLevel.WARN, IndicatorLevel.OK) == IndicatorLevel.WARN

    def test_stale_native_masks_threshold(self):
        # A stale diagnostic is not trustworthy for thresholding — STALE wins
        # even over a threshold-computed ERROR (deliberate).
        assert IndicatorConfig.combine_levels(
            IndicatorLevel.STALE, IndicatorLevel.ERROR) == IndicatorLevel.STALE
        assert IndicatorConfig.combine_levels(
            IndicatorLevel.STALE, IndicatorLevel.OK) == IndicatorLevel.STALE


class TestHasThresholds:
    def test_warn_only(self):
        assert IndicatorConfig(name='B', threshold_warn='value < 1').has_thresholds

    def test_error_only(self):
        assert IndicatorConfig(name='B', threshold_error='value < 1').has_thresholds

    def test_none(self):
        assert not IndicatorConfig(name='B').has_thresholds

    def test_whitespace_only_is_absent(self):
        # A whitespace-only threshold is treated as "no threshold" (matches
        # the dialog validator), so a healthy value is not forced to ERROR.
        config = IndicatorConfig(name='B', threshold_warn='   ')
        assert not config.has_thresholds
        assert config.evaluate_level(42) == IndicatorLevel.OK


class TestAnnunciatorConfig:
    def test_yaml_roundtrip(self):
        config = AnnunciatorConfig(
            diagnostics_topic='/diagnostics',
            indicators=[
                IndicatorConfig(name='A', source='diagnostics',
                                diagnostic_name='test'),
                IndicatorConfig(name='B', source='topic',
                                topic='/foo', msg_type='std_msgs/msg/Float64'),
            ],
        )
        yaml_str = config.to_yaml()
        restored = AnnunciatorConfig.from_yaml(yaml_str)
        assert len(restored.indicators) == 2
        assert restored.indicators[0].name == 'A'
        assert restored.indicators[1].topic == '/foo'

    def test_from_yaml_empty(self):
        config = AnnunciatorConfig.from_yaml('')
        assert config.diagnostics_topic == '/diagnostics'
        assert len(config.indicators) == 0


class TestThresholdEvaluation:
    def test_evaluate_level_ok(self):
        config = IndicatorConfig(
            name='Test',
            threshold_warn='value < 12.5',
            threshold_error='value < 11.5',
        )
        assert config.evaluate_level(13.0) == IndicatorLevel.OK

    def test_evaluate_level_warn(self):
        config = IndicatorConfig(
            name='Test',
            threshold_warn='value < 12.5',
            threshold_error='value < 11.5',
        )
        assert config.evaluate_level(12.0) == IndicatorLevel.WARN

    def test_evaluate_level_error(self):
        config = IndicatorConfig(
            name='Test',
            threshold_warn='value < 12.5',
            threshold_error='value < 11.5',
        )
        assert config.evaluate_level(11.0) == IndicatorLevel.ERROR

    def test_evaluate_level_no_thresholds(self):
        config = IndicatorConfig(name='Test')
        assert config.evaluate_level(42) == IndicatorLevel.OK
