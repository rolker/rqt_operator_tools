"""PingStore: latest ping, history, staleness, bad messages."""

import pytest
from fake_msgs import detections, flat_bottom

from rqt_ping_profile.profile_model import PingStore


def test_empty_store():
    s = PingStore()
    assert s.latest is None
    assert s.age(10.0) is None
    assert not s.is_stale(10.0, 5.0)    # nothing received is "waiting", not "stale"


def test_latest_ping_replaces_the_previous_one():
    s = PingStore()
    s.update(flat_bottom(depth=10.0), 1.0)
    s.update(flat_bottom(depth=20.0), 2.0)
    assert s.latest.max_depth == pytest.approx(20.0, abs=1e-3)
    assert s.count == 2
    assert s.history() == []        # history is off by default


def test_history_keeps_the_last_n_oldest_first():
    s = PingStore(history=2)
    for i, depth in enumerate((10.0, 11.0, 12.0, 13.0)):
        s.update(flat_bottom(depth=depth), float(i))
    assert [round(p.max_depth) for p in s.history()] == [11, 12]
    assert round(s.latest.max_depth) == 13


def test_set_history_shrinks_and_disables():
    s = PingStore(history=3)
    for depth in (10.0, 11.0, 12.0, 13.0):
        s.update(flat_bottom(depth=depth), 0.0)
    s.set_history(1)
    assert [round(p.max_depth) for p in s.history()] == [12]
    s.set_history(0)
    assert s.history() == []
    s.update(flat_bottom(depth=14.0), 0.0)
    assert s.history() == []


def test_staleness_follows_the_clock_and_the_timeout():
    s = PingStore()
    s.update(flat_bottom(), 100.0)
    assert not s.is_stale(104.9, 5.0)
    assert s.is_stale(105.1, 5.0)
    assert not s.is_stale(1e6, 0.0)     # 0 disables
    assert s.age(103.0) == pytest.approx(3.0)
    s.update(flat_bottom(), 110.0)
    assert not s.is_stale(111.0, 5.0)   # a new ping clears it


def test_clock_going_backwards_gives_zero_age_not_negative():
    s = PingStore()
    s.update(flat_bottom(), 100.0)
    assert s.age(90.0) == 0.0


def test_bad_message_keeps_the_last_good_ping_and_records_why():
    s = PingStore()
    s.update(flat_bottom(depth=20.0), 1.0)
    assert s.update(detections([], []), 2.0) is None
    assert 'no beams' in s.last_error
    assert s.latest.max_depth == pytest.approx(20.0, abs=1e-3)
    assert s.received == 1.0                    # a bad message is not a ping for staleness
    s.update(flat_bottom(), 3.0)
    assert s.last_error == ''


def test_mismatched_lengths_are_reported():
    s = PingStore()
    assert s.update(detections([0.03, 0.03], [0.0]), 1.0) is None
    assert 'lengths differ' in s.last_error


def test_clear_forgets_everything():
    s = PingStore(history=2)
    s.update(flat_bottom(), 1.0)
    s.update(flat_bottom(), 2.0)
    s.clear()
    assert s.latest is None and s.history() == [] and s.count == 0
