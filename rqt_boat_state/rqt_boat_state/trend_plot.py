"""QPainter sparkline widget backed by the Qt-free TrendBuffer.

Session-only history (no persistence): the panel pushes one sample per fixed
cadence and the widget paints a min/max band plus a mid-line, auto-scaled to
the data range.  Shared by the battery-voltage, sound-speed, and water-temp
readouts.
"""

import time

from python_qt_binding.QtCore import QSize, Qt
from python_qt_binding.QtGui import QColor, QPen
from python_qt_binding.QtWidgets import QSizePolicy, QWidget

from .trend_buffer import TrendBuffer

_BG_COLOR = QColor(30, 30, 30)
_BAND_COLOR = QColor(70, 110, 150)
_LINE_COLOR = QColor(120, 180, 240)
_AXIS_COLOR = QColor(70, 70, 70)


class TrendPlot(QWidget):
    """Auto-scaling sparkline of a single scalar over time.

    The history lives in a time-binned :class:`TrendBuffer` (default 240 bins
    * 30 s = **2 h**), so the span is a real wall-clock window regardless of how
    fast samples arrive.  ``add_sample`` stamps each sample with the receipt
    time (``time.monotonic``) and the buffer folds it into the right bin; the
    painted line is the per-bin *aggregate* (``agg``) — e.g. ``'max'`` for
    battery voltage so the line tracks resting voltage while the min/max band
    still shows load sag.
    """

    _MIN_HINT = QSize(60, 24)
    _SIZE_HINT = QSize(160, 40)

    def __init__(self, capacity: int = 240, bin_seconds: float = 30.0,
                 agg: str = 'mean', y_min=None, y_max=None, zones=None,
                 parent=None):
        super().__init__(parent)
        self._buffer = TrendBuffer(capacity, bin_seconds, agg)
        # Fixed vertical scale when both are set (e.g. battery 21..29 V); else
        # the plot auto-scales to the data range.
        self._y_min = y_min
        self._y_max = y_max
        # Optional colored value bands [(low, high, QColor), ...] drawn faintly
        # behind the trace — e.g. battery red/yellow/green health zones.
        self._zones = zones or []
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), _BG_COLOR)
        self.setPalette(palette)

    def minimumSizeHint(self):  # noqa: N802 (Qt API)
        return self._MIN_HINT

    def sizeHint(self):  # noqa: N802 (Qt API)
        return self._SIZE_HINT

    def add_sample(self, value, t=None):
        """Fold a sample into its time bin (ignored if non-finite) and repaint.

        *t* defaults to ``time.monotonic()`` (the receipt time), so callers need
        only pass the value.
        """
        if t is None:
            t = time.monotonic()
        self._buffer.add(value, t)
        self.update()

    def set_zones(self, zones):
        """Replace the colored value bands [(low, high, QColor), ...]."""
        self._zones = zones or []
        self.update()

    def clear(self):
        self._buffer.clear()
        self.update()

    def paintEvent(self, event):  # noqa: N802 (Qt API)
        from python_qt_binding.QtGui import QPainter

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)

        from python_qt_binding.QtCore import QPointF, QRectF
        from python_qt_binding.QtGui import QPolygonF

        w = self.width()
        h = self.height()
        pairs = self._buffer.minmax_pairs()
        fixed = self._y_min is not None and self._y_max is not None

        # Vertical scale: fixed bounds if given, else auto from the data.
        if fixed:
            lo, hi = self._y_min, self._y_max
        elif pairs:
            lo, hi = self._buffer.value_range()
        else:
            return  # nothing to scale or draw yet
        span = hi - lo
        if span <= 0:
            span = 1.0
            lo -= 0.5

        margin = 3
        usable = max(1, h - 2 * margin)

        def y_of(v):
            # Invert (high values up); clamp so out-of-scale values pin to the
            # edge rather than painting outside the widget.
            frac = max(0.0, min(1.0, (v - lo) / span))
            return margin + usable * (1.0 - frac)

        # Colored value zones (e.g. battery red/yellow/green), drawn first so
        # the band and line sit on top.  Shown even before the trace has data.
        for zlo, zhi, zcolor in self._zones:
            y_top = y_of(min(hi, zhi))
            y_bot = y_of(max(lo, zlo))
            c = QColor(zcolor)
            c.setAlpha(55)
            painter.setPen(Qt.NoPen)
            painter.setBrush(c)
            painter.drawRect(QRectF(0, y_top, w, y_bot - y_top))

        if len(pairs) < 2:
            return  # zones are drawn; the trace needs >= 2 bins

        n = len(pairs)

        # Fixed-width time axis: the full width always represents the whole
        # window (capacity bins = 2 h), so samples scroll in from the right and
        # off the left without the trace horizontally rescaling as it fills.
        cap = self._buffer.capacity
        denom = max(1, cap - 1)

        def x_of(i):
            # Newest bin (i = n-1) sits at the right edge; each bin is one fixed
            # slot wide regardless of how many are currently held.
            slot = cap - n + i
            return w * slot / denom

        # Min/max band as a filled polygon (top = maxes, bottom = mins back).
        poly = QPolygonF()
        for i, (_mn, mx) in enumerate(pairs):
            poly.append(QPointF(x_of(i), y_of(mx)))
        for i in range(n - 1, -1, -1):
            mn = pairs[i][0]
            poly.append(QPointF(x_of(i), y_of(mn)))
        painter.setPen(Qt.NoPen)
        band = QColor(_BAND_COLOR)
        band.setAlpha(110)
        painter.setBrush(band)
        painter.drawPolygon(poly)

        # Per-bin aggregate line (e.g. max voltage per 30 s bin for battery).
        painter.setBrush(Qt.NoBrush)
        painter.setPen(QPen(_LINE_COLOR, 1.5))
        aggs = self._buffer.values()
        prev = None
        for i, v in enumerate(aggs):
            pt = QPointF(x_of(i), y_of(v))
            if prev is not None:
                painter.drawLine(prev, pt)
            prev = pt
