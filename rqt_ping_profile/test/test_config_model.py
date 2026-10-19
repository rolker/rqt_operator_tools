"""ProfileConfig YAML round-trip and tolerance of bad persisted settings."""

import pytest

from rqt_ping_profile.profile_model import ProfileConfig


def test_defaults_match_the_owner_decisions():
    c = ProfileConfig()
    assert c.topic == 'sensors/mbes/detections'
    assert c.depth_mode == 'fixed'
    assert (c.depth_min, c.depth_max) == (0.0, 30.0)
    assert c.stale_timeout == 5.0
    assert c.history == 0
    assert c.title == ''
    assert not c.show_flagged
    assert c.color == ''


def test_round_trip():
    c = ProfileConfig(topic='/boat2/sensors/mbes/detections', title='Boat 2',
                      depth_mode='auto', depth_min=2.0, depth_max=80.0,
                      stale_timeout=0.0, history=7, show_flagged=True,
                      color='#ff8800')
    assert ProfileConfig.from_yaml(c.to_yaml()) == c


def test_unknown_keys_ignored():
    assert ProfileConfig.from_yaml('depth_mode: auto\nfuture: 1\n').depth_mode == 'auto'


@pytest.mark.parametrize('text', ['', 'not: [valid', '- a\n- b\n', '42', 'null'])
def test_unusable_yaml_gives_defaults(text):
    assert ProfileConfig.from_yaml(text) == ProfileConfig()


def test_bad_values_fall_back_or_clamp():
    c = ProfileConfig.from_yaml(
        'depth_mode: sideways\ndepth_min: nan\ndepth_max: oops\nstale_timeout: -3\n'
        'history: 9999\ntopic: "  "\nshow_flagged: yes please\ntitle: 5\n')
    assert c.depth_mode == 'fixed'
    assert (c.depth_min, c.depth_max) == (0.0, 30.0)
    assert c.stale_timeout == 5.0
    assert c.history == 50
    assert c.topic == 'sensors/mbes/detections'
    assert c.show_flagged is False
    assert c.title == '5'


def test_inverted_or_empty_depth_range_falls_back():
    for text in ('depth_min: 30\ndepth_max: 10\n', 'depth_min: 5\ndepth_max: 5\n'):
        c = ProfileConfig.from_yaml(text)
        assert (c.depth_min, c.depth_max) == (0.0, 30.0)


def test_negative_history_and_bool_history():
    assert ProfileConfig.from_yaml('history: -4').history == 0
    assert ProfileConfig.from_yaml('history: true').history == 0
    assert ProfileConfig.from_yaml('history: 3.0').history == 3


def test_huge_numbers_are_clamped():
    c = ProfileConfig.from_yaml('depth_max: 1e300\ndepth_min: -1e300\n')
    assert c.depth_max == 20000.0 and c.depth_min == -1000.0


def test_bool_is_not_a_number():
    c = ProfileConfig.from_yaml('depth_min: true\ndepth_max: false\nstale_timeout: true\n')
    assert (c.depth_min, c.depth_max, c.stale_timeout) == (0.0, 30.0, 5.0)


def test_colour_override_round_trips_and_is_canonical():
    c = ProfileConfig(color=' #FF8800 ')
    assert c.normalized().color == '#ff8800'
    assert ProfileConfig.from_yaml(c.to_yaml()).color == '#ff8800'
    assert ProfileConfig.from_yaml('color: "00ff00"').color == '#00ff00'


@pytest.mark.parametrize('bad', ['red', '#fff', '#12345g', '#1234567', '5', '[1]', 'null'])
def test_bad_colour_override_is_rejected_to_off(bad):
    assert ProfileConfig.from_yaml(f'color: {bad}\n').color == ''
    assert ProfileConfig(color=bad).normalized().color == ''
