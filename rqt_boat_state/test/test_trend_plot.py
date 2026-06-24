"""Tests for the Qt-free time-binned TrendBuffer behind the TrendPlot:
time binning, per-bin aggregation (max/min/mean/last), the min/max band, bin
wraparound, and window sizing.  No Qt, no display."""

import pytest

from rqt_boat_state.trend_buffer import TrendBuffer


class TestWindowSizing:
    def test_default_window_is_two_hours(self):
        # 240 bins * 30 s = 7200 s = 2 h, independent of sample arrival rate.
        buf = TrendBuffer()
        assert buf.capacity == 240
        assert buf.bin_seconds == 30.0
        assert buf.window_seconds == 7200.0

    def test_rejects_nonpositive_capacity(self):
        with pytest.raises(ValueError):
            TrendBuffer(0)

    def test_rejects_nonpositive_bin_seconds(self):
        with pytest.raises(ValueError):
            TrendBuffer(bin_seconds=0)

    def test_rejects_unknown_agg(self):
        with pytest.raises(ValueError):
            TrendBuffer(agg='median')


class TestBinning:
    def test_samples_in_same_bin_fold_into_one(self):
        buf = TrendBuffer(capacity=10, bin_seconds=10.0, agg='max')
        buf.add(11.0, 0.0)
        buf.add(12.5, 3.0)
        buf.add(11.8, 9.0)   # all in bin [0,10)
        # One open bin: line = max, band = (min, max).
        assert buf.values() == [12.5]
        assert buf.minmax_pairs() == [(11.0, 12.5)]

    def test_crossing_boundary_opens_new_bin(self):
        buf = TrendBuffer(capacity=10, bin_seconds=10.0, agg='last')
        buf.add(1.0, 1.0)    # bin 0
        buf.add(2.0, 12.0)   # bin 1 (finalizes bin 0)
        buf.add(3.0, 25.0)   # bin 2
        assert buf.values() == [1.0, 2.0, 3.0]
        assert len(buf) == 3

    def test_nonfinite_timestamp_ignored(self):
        buf = TrendBuffer(capacity=5, bin_seconds=10.0)
        buf.add(5.0, float('nan'))
        assert len(buf) == 0

    def test_backward_timestamp_dropped(self):
        # A sample timestamped before the open bin must not rewrite history.
        buf = TrendBuffer(capacity=5, bin_seconds=10.0, agg='last')
        buf.add(1.0, 25.0)   # bin 2
        buf.add(9.0, 5.0)    # bin 0 (backward) — dropped
        assert buf.values() == [1.0]


class TestAggregation:
    def _bin(self, agg):
        buf = TrendBuffer(capacity=5, bin_seconds=10.0, agg=agg)
        for v in (2.0, 8.0, 4.0):   # one open bin
            buf.add(v, 1.0)
        return buf.values()[0]

    def test_max(self):
        assert self._bin('max') == 8.0

    def test_min(self):
        assert self._bin('min') == 2.0

    def test_mean(self):
        assert self._bin('mean') == pytest.approx((2.0 + 8.0 + 4.0) / 3.0)

    def test_last(self):
        assert self._bin('last') == 4.0

    def test_band_tracks_excursion_regardless_of_agg(self):
        buf = TrendBuffer(capacity=5, bin_seconds=10.0, agg='max')
        for v in (2.0, 8.0, 4.0):
            buf.add(v, 1.0)
        assert buf.minmax_pairs() == [(2.0, 8.0)]


class TestWraparound:
    def test_drops_oldest_bins_past_capacity(self):
        buf = TrendBuffer(capacity=2, bin_seconds=10.0, agg='last')
        # bins 0..3, each one sample; only the most recent 2 are retained.
        for i in range(4):
            buf.add(float(i), i * 10.0 + 1.0)
        assert buf.values() == [2.0, 3.0]
        assert len(buf) == 2

    def test_clear(self):
        buf = TrendBuffer(capacity=3, bin_seconds=10.0)
        buf.add(1.0, 1.0)
        buf.add(2.0, 12.0)
        buf.clear()
        assert len(buf) == 0
        assert buf.value_range() is None


class TestRange:
    def test_range_spans_all_bins(self):
        buf = TrendBuffer(capacity=5, bin_seconds=10.0, agg='mean')
        buf.add(3.0, 1.0)    # bin 0
        buf.add(1.0, 11.0)   # bin 1
        buf.add(9.0, 21.0)   # bin 2
        assert buf.value_range() == (1.0, 9.0)


class TestNonFinite:
    def test_nan_value_ignored(self):
        buf = TrendBuffer(capacity=3, bin_seconds=10.0)
        buf.add(float('nan'), 1.0)
        buf.add(1.0, 2.0)
        assert buf.values() == [1.0]

    def test_inf_value_ignored(self):
        buf = TrendBuffer(capacity=3, bin_seconds=10.0)
        buf.add(float('inf'), 1.0)
        assert len(buf) == 0

    def test_none_value_ignored(self):
        buf = TrendBuffer(capacity=3, bin_seconds=10.0)
        buf.add(None, 1.0)
        assert len(buf) == 0
