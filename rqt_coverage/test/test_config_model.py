"""CoverageConfig YAML round-trip and tolerance of bad persisted settings."""

import pytest

from rqt_coverage.score_model import CoverageConfig


def test_defaults_match_the_owner_decisions():
    c = CoverageConfig()
    assert c.view == 'glance'
    assert c.aging_timeout == 60.0
    assert (c.weight_spec, c.weight_covered) == (1.0, 0.5)
    assert c.topic == 'coverage_assessment'
    assert c.selected_areas == []
    assert not c.show_combined


def test_round_trip():
    c = CoverageConfig(topic='/x/cov', view='detail', selected_areas=['a', 'b'],
                       font_scale=2.5, show_combined=True, weight_spec=0.9,
                       weight_covered=0.2, aging_timeout=0.0)
    assert CoverageConfig.from_yaml(c.to_yaml()) == c


def test_unknown_keys_ignored():
    c = CoverageConfig.from_yaml('view: detail\nfuture_option: 3\n')
    assert c.view == 'detail'


@pytest.mark.parametrize('text', ['', 'not: [valid', '- a\n- b\n', '42', 'null'])
def test_unusable_yaml_gives_defaults(text):
    assert CoverageConfig.from_yaml(text) == CoverageConfig()


def test_bad_values_fall_back_or_clamp():
    c = CoverageConfig.from_yaml(
        'view: sideways\nfont_scale: 99\nweight_spec: 5\nweight_covered: nan\n'
        'aging_timeout: -3\ntopic: "  "\nselected_areas: oops\n')
    assert c.view == 'glance'
    assert c.font_scale == 4.0
    assert c.weight_spec == 1.0
    assert c.weight_covered == 0.5
    assert c.aging_timeout == 60.0
    assert c.topic == 'coverage_assessment'
    assert c.selected_areas == []


def test_font_scale_lower_clamp():
    assert CoverageConfig.from_yaml('font_scale: 0.01').font_scale == 0.5


def test_non_numeric_font_scale():
    assert CoverageConfig.from_yaml('font_scale: big').font_scale == 1.0


def test_selected_areas_are_deduplicated_keeping_order():
    c = CoverageConfig.from_yaml('selected_areas: [b, a, b, a, c]')
    assert c.selected_areas == ['b', 'a', 'c']


@pytest.mark.parametrize('key', ['font_scale', 'aging_timeout', 'weight_spec',
                                 'weight_covered'])
def test_huge_integers_fall_back_instead_of_raising(key):
    c = CoverageConfig.from_yaml(f'{key}: 1' + '0' * 400)
    assert getattr(c, key) == getattr(CoverageConfig(), key)


def test_show_combined_must_be_a_real_boolean():
    assert CoverageConfig.from_yaml('show_combined: "false"').show_combined is False
    assert CoverageConfig.from_yaml('show_combined: true').show_combined is True
    assert CoverageConfig.from_yaml('show_combined: 1').show_combined is False
