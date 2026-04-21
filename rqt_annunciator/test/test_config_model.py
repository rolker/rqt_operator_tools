"""Tests for config_model: parsing, serialization, and matching."""

import pytest

from rqt_annunciator.config_model import (
    AnnunciatorConfig,
    IndicatorConfig,
    IndicatorLevel,
    MatchMode,
)


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
