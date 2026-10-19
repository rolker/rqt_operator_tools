"""Platform lookup by topic namespace: pure Python, no ROS or Qt."""

from types import SimpleNamespace as NS

import pytest

from rqt_ping_profile.platform_lookup import (
    match_platform,
    normalize_hex_color,
    parse_hex_color,
    PlatformDirectory,
)


def platform(name, ns, rgba=(1.0, 0.5, 0.0, 1.0)):
    r, g, b, a = rgba
    return NS(name=name, platform_namespace=ns, color=NS(r=r, g=g, b=b, a=a))


def plist(*platforms):
    return NS(platforms=list(platforms))


TOPIC = '/urchin/sensors/mbes/detections'


def test_match_by_namespace():
    m = match_platform(plist(platform('Urchin', 'urchin')), TOPIC)
    assert m.name == 'Urchin' and m.namespace == 'urchin'
    assert m.color == (1.0, 0.5, 0.0, 1.0)


@pytest.mark.parametrize('ns', ['urchin', '/urchin', 'urchin/', '/urchin/', ' urchin '])
@pytest.mark.parametrize('topic', [TOPIC, TOPIC[1:]])
def test_leading_and_trailing_slashes_are_tolerated(ns, topic):
    assert match_platform(plist(platform('U', ns)), topic) is not None


@pytest.mark.parametrize('topic', ['detections', '/detections', '', None, 42])
def test_topic_without_a_namespace_never_matches(topic):
    assert match_platform(plist(platform('U', 'urchin'), platform('D', 'detections')),
                          topic) is None


def test_prefix_of_a_longer_name_is_not_a_match():
    assert match_platform(plist(platform('U', 'urch')), TOPIC) is None
    assert match_platform(plist(platform('U', 'urchin')), '/urchin2/sensors/x') is None


def test_empty_namespace_never_matches():
    assert match_platform(plist(platform('Robot', ''), platform('R', '/')), TOPIC) is None


def test_alpha_zero_means_no_colour_but_the_name_still_matches():
    m = match_platform(plist(platform('Urchin', 'urchin', (1, 0, 0, 0.0))), TOPIC)
    assert m.name == 'Urchin' and m.color is None


def test_two_platforms_pick_the_right_one():
    lst = plist(platform('Bluefin', 'bluefin', (0, 0, 1, 1)),
                platform('Urchin', 'urchin', (1, 0, 0, 1)))
    assert match_platform(lst, TOPIC).color == (1.0, 0.0, 0.0, 1.0)
    assert match_platform(lst, '/bluefin/sensors/mbes/detections').name == 'Bluefin'
    assert match_platform(lst, '/beluga/sensors/mbes/detections') is None


def test_longest_namespace_wins():
    lst = plist(platform('Fleet', 'urchin'), platform('Sub', 'urchin/sub'))
    assert match_platform(lst, '/urchin/sub/x/y').name == 'Sub'
    assert match_platform(lst, '/urchin/x/y').name == 'Fleet'


def test_colour_is_opaque_clamped_and_nan_safe():
    nan = float('nan')
    m = match_platform(plist(platform('U', 'urchin', (2.0, -1.0, 0.25, 0.3))), TOPIC)
    assert m.color == (1.0, 0.0, 0.25, 1.0)
    assert match_platform(plist(platform('U', 'urchin', (nan, 0, 0, 1))), TOPIC).color is None
    assert match_platform(plist(platform('U', 'urchin', (0, 0, 0, nan))), TOPIC).color is None


def test_malformed_platforms_are_skipped_not_fatal():
    lst = plist(object(), NS(platform_namespace=7), NS(platform_namespace='urchin'),
                platform('U', 'urchin'))
    assert match_platform(lst, TOPIC) is not None
    assert match_platform(None, TOPIC) is None
    assert match_platform(plist(), TOPIC) is None


def test_directory_merges_one_boat_lists():
    d = PlatformDirectory()
    assert d.update(plist(platform('Urchin', 'urchin', (1, 0, 0, 1))))
    assert d.update(plist(platform('Bluefin', 'bluefin', (0, 0, 1, 1))))
    assert len(d) == 2
    assert d.lookup(TOPIC).name == 'Urchin'          # the second list did not forget it
    assert d.lookup('/bluefin/s/d').color == (0.0, 0.0, 1.0, 1.0)


def test_directory_reports_changes_only():
    d = PlatformDirectory()
    lst = plist(platform('Urchin', 'urchin'))
    assert d.update(lst) is True
    assert d.update(lst) is False
    assert d.update(plist(platform('Urchin', 'urchin', (0, 1, 0, 1)))) is True
    assert d.lookup(TOPIC).color == (0.0, 1.0, 0.0, 1.0)
    assert d.update(None) is False and d.update(NS()) is False


def test_directory_without_a_match():
    d = PlatformDirectory()
    assert d.lookup(TOPIC) is None
    d.update(plist(platform('B', 'bluefin')))
    assert d.lookup(TOPIC) is None


def test_hex_colours():
    assert parse_hex_color('#ff0000') == (1.0, 0.0, 0.0, 1.0)
    assert parse_hex_color('00FF00') == (0.0, 1.0, 0.0, 1.0)
    assert normalize_hex_color(' #AbCdEf ') == '#abcdef'
    assert normalize_hex_color('abcdef') == '#abcdef'
    for bad in ['', '#fff', '#12345g', '#1234567', 'red', None, 5]:
        assert parse_hex_color(bad) is None
        assert normalize_hex_color(bad) == ''
