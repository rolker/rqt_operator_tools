"""QPainter sparkline widget backed by the Qt-free TrendBuffer.

Session-only history (no persistence): the panel pushes one sample per fixed
cadence and the widget paints a min/max band plus a mid-line, auto-scaled to
the data range.  Shared by the battery-voltage, sound-speed, and water-temp
readouts.
"""

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

    The history lives in a :class:`TrendBuffer` (default 720 samples —
    ≈2 h only at a nominal 1 sample / 10 s).  ``add_sample`` is called by the
    owning panel once per received message; the widget never times anything
    itself, so the window spanned tracks message arrival rate, not a fixed 2 h.
    """

    _MIN_HINT = QSize(60, 24)
    _SIZE_HINT = QSize(160, 40)

    def __init__(self, capacity: int = 720, parent=None):
        super().__init__(parent)
        self._buffer = TrendBuffer(capacity)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), _BG_COLOR)
        self.setPalette(palette)

    def minimumSizeHint(self):  # noqa: N802 (Qt API)
        return self._MIN_HINT

    def sizeHint(self):  # noqa: N802 (Qt API)
        return self._SIZE_HINT

    def add_sample(self, value):
        """Push a sample (ignored if non-finite) and repaint."""
        self._buffer.push(value)
        self.update()

    def clear(self):
        self._buffer.clear()
        self.update()

    def paintEvent(self, event):  # noqa: N802 (Qt API)
        from python_qt_binding.QtGui import QPainter

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)

        w = self.width()
        h = self.height()
        pairs = self._buffer.minmax_pairs()
        if len(pairs) < 2:
            return

        rng = self._buffer.value_range()
        lo, hi = rng
        span = hi - lo
        if span <= 0:
            span = 1.0
            lo -= 0.5

        n = len(pairs)

        def x_of(i):
            return w * i / (n - 1)

        def y_of(v):
            # Invert: high values near the top, with a small vertical margin.
            margin = 3
            usable = max(1, h - 2 * margin)
            return margin + usable * (1.0 - (v - lo) / span)

        # Min/max band as a filled polygon (top = maxes, bottom = mins back).
        from python_qt_binding.QtGui import QPolygonF
        from python_qt_binding.QtCore import QPointF

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

        # Mid-line.
        painter.setBrush(Qt.NoBrush)
        painter.setPen(QPen(_LINE_COLOR, 1.5))
        mids = self._buffer.values()
        prev = None
        for i, v in enumerate(mids):
            pt = QPointF(x_of(i), y_of(v))
            if prev is not None:
                painter.drawLine(prev, pt)
            prev = pt
