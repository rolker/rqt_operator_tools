"""Tests for the Qt-free TrendBuffer behind the TrendPlot sparkline:
ring-buffer wraparound, per-slot min/max, and the 2-hour window sizing.
No Qt, no display."""

import pytest

from rqt_boat_state.trend_buffer import TrendBuffer


class TestWindowSizing:
    def test_default_capacity_is_two_hours(self):
        # 720 slots × 10 s/slot = 7200 s = 2 h.
        buf = TrendBuffer()
        assert buf.capacity == 720
        assert buf.capacity * 10 == 7200

    def test_rejects_nonpositive_capacity(self):
        with pytest.raises(ValueError):
            TrendBuffer(0)


class TestWraparound:
    def test_fills_then_drops_oldest(self):
        buf = TrendBuffer(capacity=3)
        for v in (1, 2, 3):
            buf.push(v)
        assert buf.is_full()
        assert buf.values() == [1.0, 2.0, 3.0]
        # One more drops the oldest (1) and appends 4.
        buf.push(4)
        assert buf.values() == [2.0, 3.0, 4.0]
        buf.push(5)
        assert buf.values() == [3.0, 4.0, 5.0]

    def test_len_caps_at_capacity(self):
        buf = TrendBuffer(capacity=2)
        for v in range(10):
            buf.push(v)
        assert len(buf) == 2
        assert buf.values() == [8.0, 9.0]

    def test_chronological_order_after_many_wraps(self):
        buf = TrendBuffer(capacity=4)
        for v in range(100):
            buf.push(v)
        assert buf.values() == [96.0, 97.0, 98.0, 99.0]

    def test_clear(self):
        buf = TrendBuffer(capacity=3)
        buf.push(1)
        buf.push(2)
        buf.clear()
        assert len(buf) == 0
        assert buf.value_range() is None


class TestMinMax:
    def test_single_value_min_equals_max(self):
        buf = TrendBuffer(capacity=3)
        buf.push(5)
        assert buf.minmax_pairs() == [(5.0, 5.0)]

    def test_folded_pair_orders_min_max(self):
        buf = TrendBuffer(capacity=3)
        buf.push(8, 2)  # passed high-then-low; stored as (2, 8)
        assert buf.minmax_pairs() == [(2.0, 8.0)]

    def test_range_spans_buffer(self):
        buf = TrendBuffer(capacity=4)
        buf.push(3)
        buf.push(1, 9)
        buf.push(5)
        assert buf.value_range() == (1.0, 9.0)

    def test_midpoint_values(self):
        buf = TrendBuffer(capacity=2)
        buf.push(2, 8)
        assert buf.values() == [5.0]


class TestNonFinite:
    def test_nan_ignored(self):
        buf = TrendBuffer(capacity=3)
        buf.push(float('nan'))
        buf.push(1.0)
        assert buf.values() == [1.0]

    def test_inf_ignored(self):
        buf = TrendBuffer(capacity=3)
        buf.push(float('inf'))
        assert len(buf) == 0

    def test_none_ignored(self):
        buf = TrendBuffer(capacity=3)
        buf.push(None)
        assert len(buf) == 0
