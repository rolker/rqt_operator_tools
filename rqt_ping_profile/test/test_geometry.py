"""Beam geometry: range, angle, sign, ordering, bad input."""

import math

import pytest
from fake_msgs import detections, flat_bottom

from rqt_ping_profile.profile_model import BadPing, Ping


def test_straight_down_beam():
    ping = Ping.from_msg(detections([2 * 15.0 / 1500.0], [0.0]))
    (b,) = ping.beams
    assert b.across == pytest.approx(0.0, abs=1e-6)
    assert b.depth == pytest.approx(15.0, abs=1e-4)
    assert b.ok


def test_range_uses_half_the_two_way_time_and_the_ping_sound_speed():
    # 0.04 s two way at 1480 m/s: range 29.6 m; at 30 degrees:
    ping = Ping.from_msg(detections([0.04], [math.radians(30)], sound_speed=1480.0))
    (b,) = ping.beams
    assert math.hypot(b.across, b.depth) == pytest.approx(29.6, abs=1e-4)
    assert b.across == pytest.approx(29.6 * 0.5, abs=1e-4)
    assert b.depth == pytest.approx(29.6 * math.sqrt(3) / 2, abs=1e-4)


def test_positive_angle_is_starboard_positive_across():
    ping = Ping.from_msg(detections([0.04, 0.04], [math.radians(-45), math.radians(45)]))
    port, stbd = ping.beams
    assert port.across < 0 < stbd.across
    assert port.depth == pytest.approx(stbd.depth)


def test_flat_bottom_gives_constant_depth():
    ping = Ping.from_msg(flat_bottom(depth=20.0))
    assert len(ping.beams) == 11
    for b in ping.beams:
        assert b.depth == pytest.approx(20.0, abs=1e-3)
    assert ping.max_depth == pytest.approx(20.0, abs=1e-3)
    assert ping.half_width == pytest.approx(20.0 * math.tan(math.radians(60)), rel=1e-4)


def test_beams_sorted_port_to_starboard_whatever_the_message_order():
    msg = flat_bottom()
    msg.two_way_travel_times.reverse()
    msg.rx_angles.reverse()
    ping = Ping.from_msg(msg)
    across = [b.across for b in ping.beams]
    assert across == sorted(across)


def test_flags_split_ok_and_flagged_and_edges_are_the_outer_ok_beams():
    flags = [4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]      # both outermost beams flagged
    ping = Ping.from_msg(flat_bottom(flags=flags))
    assert len(ping.ok_beams) == 9
    assert len(ping.flagged_beams) == 2
    port, stbd = ping.swath_edges
    assert port is ping.ok_beams[0] and stbd is ping.ok_beams[-1]
    assert port.across == ping.beams[1].across
    assert stbd.across == ping.beams[-2].across


def test_no_ok_beams_has_no_swath_edges():
    ping = Ping.from_msg(flat_bottom(flags=[2] * 11))
    assert ping.swath_edges is None
    assert ping.max_depth == 0.0 and ping.half_width == 0.0


def test_flag_accepts_plain_ints():
    msg = flat_bottom()
    msg.flags = [0] * 11
    assert len(Ping.from_msg(msg).ok_beams) == 11


def test_invalid_beams_are_dropped_and_counted():
    nan, inf = float('nan'), float('inf')
    msg = detections([0.03, nan, -0.01, 0.03, inf, 0.03], [0.0, 0.1, 0.1, nan, 0.0, 0.2])
    ping = Ping.from_msg(msg)
    assert len(ping.beams) == 2
    assert ping.dropped == 4


@pytest.mark.parametrize('msg', [
    detections([], []),                                   # empty
    detections([0.03, 0.03], [0.0]),                      # angles shorter
    detections([0.03], [0.0], flags=[0, 0]),              # flags longer
    detections([0.03], [0.0], sound_speed=0.0),           # no sound speed
    detections([0.03], [0.0], sound_speed=float('nan')),
    detections([0.03], [0.0], sound_speed=-1500.0),
    object(),                                             # not a message at all
])
def test_unusable_messages_raise_badping(msg):
    with pytest.raises(BadPing):
        Ping.from_msg(msg)


def test_absurd_range_is_dropped_not_drawn():
    ping = Ping.from_msg(detections([1e12, 0.03], [0.0, 0.0]))
    assert len(ping.beams) == 1 and ping.dropped == 1


def test_stamp_is_read_when_present():
    assert Ping.from_msg(detections([0.03], [0.0], sec=5, nanosec=7)).stamp_ns == 5_000_000_007
