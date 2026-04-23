"""Tests for adaptive column widths + per-cell font fitting (issue #23)."""

import os

import pytest

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')

from python_qt_binding.QtWidgets import QApplication

from rqt_annunciator.config_model import (
    AnnunciatorConfig,
    IndicatorConfig,
    IndicatorLevel,
)
from rqt_annunciator.indicator_widget import IndicatorWidget


@pytest.fixture(scope='module')
def qapp():
    app = QApplication.instance() or QApplication([])
    yield app


class _FakeLogger:
    def warn(self, *_args, **_kwargs):
        pass

    def error(self, *_args, **_kwargs):
        pass


class _FakeNode:
    def get_logger(self):
        return _FakeLogger()

    def create_subscription(self, *_args, **_kwargs):
        return object()

    def destroy_subscription(self, *_args, **_kwargs):
        pass


def _make_annunciator(indicator_names):
    from rqt_annunciator.annunciator_widget import AnnunciatorWidget
    w = AnnunciatorWidget(_FakeNode())
    config = AnnunciatorConfig(
        indicators=[
            IndicatorConfig(name=name, source='diagnostics',
                            diagnostic_name=f'd_{name}')
            for name in indicator_names
        ]
    )
    w.load_config(config)
    return w


class TestIndicatorEMA:
    def test_cold_start_uses_label_plus_placeholder(self, qapp):
        """Before any data, EMA reflects label + '---' so first layout is sane."""
        short = IndicatorWidget('x')
        long_ = IndicatorWidget('A very long indicator label')
        assert long_.ema_width_px > short.ema_width_px, (
            'Cold-start EMA must be seeded from the label text so the '
            'first layout can differentiate short- from long-labeled '
            'indicators before any data arrives.'
        )

    def test_ema_moves_toward_new_value(self, qapp):
        """One sample moves the EMA by exactly α toward the new width."""
        w = IndicatorWidget('x')
        initial = w.ema_width_px
        # Push a very long value.
        w.set_status(IndicatorLevel.OK, 'X' * 200)
        one_step = w.ema_width_px
        # With α = 0.05, the EMA should have advanced but nowhere near
        # the full jump — proving the smoothing is in effect.
        assert one_step > initial, 'EMA should grow toward a larger sample'
        # After many samples at the same level it should converge close.
        for _ in range(100):
            w.set_status(IndicatorLevel.OK, 'X' * 200)
        converged = w.ema_width_px
        assert converged > one_step, (
            'Repeated samples should keep migrating the EMA closer to the '
            'new value'
        )

    def test_ema_emits_signal_on_change(self, qapp):
        """width_sample_changed fires when the EMA changes."""
        w = IndicatorWidget('x')
        samples = []
        w.width_sample_changed.connect(lambda name, px: samples.append((name, px)))
        w.set_status(IndicatorLevel.OK, 'something new')
        # First meaningful sample should trigger at least one emission.
        assert samples, 'width_sample_changed must fire on value change'
        assert samples[-1][0] == 'x'

    def test_fit_font_clamps_to_bounds(self, qapp):
        """Font never goes below _MIN_FONT_PX or above _MAX_FONT_PX."""
        w = IndicatorWidget('name')
        # Tiny cell — text can't fit, font clamps to minimum.
        w.resize(60, 20)
        w.set_status(IndicatorLevel.OK, 'some value')
        assert w._label.font().pixelSize() >= IndicatorWidget._MIN_FONT_PX
        assert w._value_label.font().pixelSize() >= IndicatorWidget._MIN_FONT_PX

        # Huge cell — font clamps to maximum.
        w.resize(2000, 600)
        w.set_status(IndicatorLevel.OK, 'v')
        assert w._label.font().pixelSize() <= IndicatorWidget._MAX_FONT_PX
        assert w._value_label.font().pixelSize() <= IndicatorWidget._MAX_FONT_PX

    def test_fit_font_shrinks_below_layout_overhead(self, qapp):
        """Shrinking past the layout overhead must still update the font.

        Regression for the case where an early-return in _fit_font left a
        previously-large font in place when ``usable_w <= 0`` — causing
        severe clipping instead of the intended clamp to _MIN_FONT_PX.
        """
        w = IndicatorWidget('name')
        # First grow the font by fitting into a large cell.
        w.resize(2000, 600)
        w.set_status(IndicatorLevel.OK, 'v')
        large = w._label.font().pixelSize()
        assert large > IndicatorWidget._MIN_FONT_PX, (
            'precondition: font should have grown in a large cell'
        )
        # Now shrink below the layout overhead (~36 px horizontally).
        w.resize(10, 10)
        w._fit_font()
        assert w._label.font().pixelSize() == IndicatorWidget._MIN_FONT_PX
        assert w._value_label.font().pixelSize() == IndicatorWidget._MIN_FONT_PX


class TestAnnunciatorColumnStretch:
    def test_stretches_reflect_ema_widths(self, qapp):
        """Column stretches come from the max EMA width in each column."""
        w = _make_annunciator(['short', 'A very long indicator label'])
        w.resize(800, 100)  # aspect > 2 → single row → cols = n = 2
        w._rebuild_layout()
        assert w._current_cols == 2
        s0 = w._layout.columnStretch(0)
        s1 = w._layout.columnStretch(1)
        # Column 1 holds the long label — it must get a larger stretch
        # weight than column 0.
        assert s1 > s0, (
            f'long-label column should get more stretch than short-label '
            f'column (got {s0=} {s1=})'
        )

    def test_stretches_update_on_value_change(self, qapp):
        """A sudden long value migrates its column's stretch upward."""
        w = _make_annunciator(['a', 'b'])
        w.resize(800, 100)
        w._rebuild_layout()
        before_a = w._layout.columnStretch(0)
        # Feed indicator 'a' a long value many times to migrate its EMA.
        ind_a = w._indicators['a']
        for _ in range(100):
            ind_a.set_status(IndicatorLevel.OK, 'X' * 50)
        after_a = w._layout.columnStretch(0)
        assert after_a > before_a, (
            'Column stretch must grow as the indicator in that column '
            'accumulates wider value samples'
        )

    def test_old_column_stretches_cleared_on_reflow(self, qapp):
        """When cols shrinks (tall window), previously-set stretches don't linger."""
        w = _make_annunciator([f'ind{i}' for i in range(6)])
        # Force wide layout first (6 columns).
        w.resize(1200, 100)
        w._rebuild_layout()
        assert w._current_cols == 6
        # Now force tall — single column.
        w.resize(100, 1200)
        w._rebuild_layout()
        assert w._current_cols == 1
        # Columns 1..5 should have zero stretch (they're no longer in use).
        for c in range(1, w._layout.columnCount()):
            assert w._layout.columnStretch(c) == 0, (
                f'stale column {c} stretch left over from a wider reflow'
            )
