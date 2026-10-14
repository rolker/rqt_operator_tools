"""Aging indicator: receipt time, not header.stamp."""

from fake_msgs import assessment
from rqt_coverage.score_model import (
    AGING,
    FRESH,
    NEVER,
    AreaScore,
    Store,
    aging_state,
    format_age,
)


def test_fresh_and_aging():
    assert aging_state(100.0, 130.0, 60.0) == FRESH
    assert aging_state(100.0, 161.0, 60.0) == AGING


def test_exactly_the_timeout_is_not_aging():
    assert aging_state(100.0, 160.0, 60.0) == FRESH


def test_disabled_timeout_never_ages():
    assert aging_state(0.0, 1e9, 0.0) == FRESH


def test_never_received():
    assert aging_state(None, 5.0, 60.0) == NEVER


def test_header_stamp_far_from_wall_clock_is_still_fresh():
    # A sim-time stamp (or one from hours ago) must not make a just-received
    # assessment look old: age is measured from receipt.
    store = Store()
    store.update(AreaScore.from_msg(assessment(sec=4)), now=1000.0)
    held = store.get('a')
    assert aging_state(held.received, 1010.0, 60.0) == FRESH


def test_oldest_received():
    from rqt_coverage.score_model import StoredArea, oldest_received
    a = StoredArea(AreaScore.from_msg(assessment('a')), received=5.0)
    b = StoredArea(AreaScore.from_msg(assessment('b')), received=2.0)
    assert oldest_received([a, b]) is b
    assert oldest_received([]) is None


def test_format_age():
    assert format_age(75) == '75 s'
    assert format_age(119.9) == '119 s'
    assert format_age(300) == '5 min'
    assert format_age(7300) == '2 h'
    assert format_age(-4) == '0 s'
