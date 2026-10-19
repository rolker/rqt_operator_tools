"""Depth scale (fixed and auto), viewport mapping, tick helpers."""

import math

import pytest
from fake_msgs import flat_bottom

from rqt_ping_profile.profile_model import (
    DEPTH_AUTO,
    DEPTH_FIXED,
    DepthScale,
    format_metres,
    nice_step,
    Ping,
    ticks,
    Viewport,
)


def test_fixed_scale_maps_range_onto_plot_height():
    s = DepthScale(DEPTH_FIXED, 0.0, 30.0)
    mpp = s.metres_per_pixel(400, 300)
    assert mpp == pytest.approx(0.1)
    vp = Viewport(0, 0, 400, 300, mpp, depth_top=0.0)
    assert vp.depth_at_bottom() == pytest.approx(30.0)
    assert vp.half_width_visible() == pytest.approx(20.0)   # 200 px * 0.1


def test_fixed_scale_ignores_pings():
    s = DepthScale(DEPTH_FIXED, 0.0, 30.0)
    s.observe(Ping.from_msg(flat_bottom(depth=200.0)))
    assert s.depth_range == (0.0, 30.0)


def test_fixed_range_clips_deeper_points_out_of_view():
    s = DepthScale(DEPTH_FIXED, 5.0, 25.0)
    vp = Viewport(10, 20, 400, 200, s.metres_per_pixel(400, 200), depth_top=5.0)
    assert vp.contains_depth(5.0) and vp.contains_depth(25.0)
    assert not vp.contains_depth(4.9) and not vp.contains_depth(25.1)
    x, y = vp.to_screen(0.0, 40.0)
    assert y > vp.top + vp.height            # below the plot rectangle: clipped when drawn
    _, y_top = vp.to_screen(0.0, 5.0)
    assert y_top == pytest.approx(vp.top)


def test_starboard_is_left_port_is_right_of_centre():
    vp = Viewport(0, 0, 400, 300, 0.1)
    x_stbd, _ = vp.to_screen(10.0, 0.0)
    x_port, _ = vp.to_screen(-10.0, 0.0)
    assert x_stbd < vp.centre_x < x_port
    assert vp.centre_x - x_stbd == pytest.approx(100.0)   # 10 m / 0.1 m per px


def test_one_scale_for_both_axes_so_angles_read_true():
    vp = Viewport(0, 0, 400, 300, 0.1)
    cx, cy = vp.to_screen(0.0, 0.0)
    ax, ay = vp.to_screen(math.tan(math.radians(30)) * 10, 10.0)
    # a 30 degree beam: dx/dy on screen equals tan(30)
    assert abs(ax - cx) / abs(ay - cy) == pytest.approx(math.tan(math.radians(30)))


def test_auto_scale_fits_depth_with_headroom_from_zero():
    s = DepthScale(DEPTH_AUTO)
    s.observe(Ping.from_msg(flat_bottom(depth=20.0, half_angle_deg=30.0)))
    top, bottom = s.depth_range
    assert top == 0.0
    assert bottom == pytest.approx(20.0 * 1.10, rel=1e-3)


def test_auto_scale_always_keeps_the_full_swath_visible():
    s = DepthScale(DEPTH_AUTO)
    ping = Ping.from_msg(flat_bottom(depth=10.0, half_angle_deg=70.0))   # very wide
    s.observe(ping)
    width, height = 300.0, 300.0
    mpp = s.metres_per_pixel(width, height)
    vp = Viewport(0, 0, width, height, mpp)
    assert vp.half_width_visible() >= ping.half_width * 1.0999
    # a wide plot is depth limited instead
    mpp_wide = s.metres_per_pixel(3000.0, 300.0)
    assert mpp_wide < mpp


def test_auto_scale_grows_at_once_and_shrinks_only_when_well_below():
    s = DepthScale(DEPTH_AUTO)
    s.observe(Ping.from_msg(flat_bottom(depth=20.0, half_angle_deg=10.0)))
    first = s.depth_range[1]
    s.observe(Ping.from_msg(flat_bottom(depth=30.0, half_angle_deg=10.0)))
    assert s.depth_range[1] > first                      # grew immediately
    grown = s.depth_range[1]
    for depth in (29.0, 27.0, 25.0, 23.0, 30.0, 28.0):   # jitter within 70 %
        s.observe(Ping.from_msg(flat_bottom(depth=depth, half_angle_deg=10.0)))
        assert s.depth_range[1] == pytest.approx(grown)  # no jump
    s.observe(Ping.from_msg(flat_bottom(depth=10.0, half_angle_deg=10.0)))
    assert s.depth_range[1] == pytest.approx(10.0 * 1.10, rel=1e-3)   # shrank


def test_auto_scale_ignores_pings_without_ok_beams():
    s = DepthScale(DEPTH_AUTO)
    s.observe(Ping.from_msg(flat_bottom(depth=20.0)))
    before = s.depth_range
    s.observe(Ping.from_msg(flat_bottom(depth=5.0, flags=[1] * 11)))
    s.observe(None)
    assert s.depth_range == before


def test_auto_scale_before_any_ping_is_usable():
    s = DepthScale(DEPTH_AUTO)
    assert s.depth_range == (0.0, 30.0)     # the fixed bottom as a placeholder
    assert s.metres_per_pixel(300, 300) > 0


def test_configure_to_fixed_restores_the_fixed_range():
    s = DepthScale(DEPTH_AUTO)
    s.observe(Ping.from_msg(flat_bottom(depth=80.0)))
    s.configure(DEPTH_FIXED, 0.0, 30.0)
    assert s.depth_range == (0.0, 30.0)


def test_degenerate_pixel_sizes_do_not_divide_by_zero():
    s = DepthScale(DEPTH_FIXED, 0.0, 30.0)
    assert s.metres_per_pixel(0, 0) > 0


@pytest.mark.parametrize('span,expected', [(30, 5), (10, 2), (100, 20), (0.5, 0.1), (3, 0.5)])
def test_nice_step(span, expected):
    assert nice_step(span) == pytest.approx(expected)


def test_nice_step_bad_input():
    assert nice_step(0) == 1.0 and nice_step(float('nan')) == 1.0


def test_ticks():
    assert ticks(0, 30, 10) == [0, 10, 20, 30]
    assert ticks(-25, 25, 10) == [-20, -10, 0, 10, 20]
    assert ticks(5, 4, 1) == []
    assert ticks(float('nan'), 1, 1) == []
    assert len(ticks(0, 1e9, 1)) <= 200


def test_format_metres():
    assert format_metres(10.0, 5.0) == '10'
    assert format_metres(0.5, 0.5) == '0.5'
