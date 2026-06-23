"""Center-zero horizontal bar gauge shared by Steering and Throttle.

Both steering and throttle are −1..1 commands with a meaningful zero, so they
share one widget: a horizontal track with the actual value filled from the
center and a commanded ghost marker.  The label and the −/+ end captions are
set per instance.
"""

from python_qt_binding.QtCore import QRectF, QSize, Qt
from python_qt_binding.QtGui import QFont, QPen
from python_qt_binding.QtWidgets import QSizePolicy, QWidget

from . import (
    ACTUAL_COLOR,
    BG_COLOR,
    COMMAND_COLOR,
    COMMAND_STALE_COLOR,
    FACE_COLOR,
    LABEL_COLOR,
    TICK_COLOR,
)


class CenterZeroGauge(QWidget):
    """Horizontal −1..1 bar with a center zero and a commanded ghost marker."""

    _MIN_HINT = QSize(120, 48)
    _SIZE_HINT = QSize(200, 64)

    def __init__(self, label='', neg_caption='', pos_caption='', parent=None):
        super().__init__(parent)
        self._label = label
        self._neg_caption = neg_caption
        self._pos_caption = pos_caption
        self._value = None       # −1..1 actual, or None when stale
        self._commanded = None   # −1..1 commanded, or None when absent
        self._commanded_stale = False
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), BG_COLOR)
        self.setPalette(palette)

    def minimumSizeHint(self):  # noqa: N802 (Qt API)
        return self._MIN_HINT

    def sizeHint(self):  # noqa: N802 (Qt API)
        return self._SIZE_HINT

    def set_value(self, value):
        self._value = value
        self.update()

    def set_commanded(self, value, stale=False):
        self._commanded = value
        self._commanded_stale = stale
        self.update()

    def paintEvent(self, event):  # noqa: N802 (Qt API)
        from python_qt_binding.QtGui import QPainter

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)

        w = self.width()
        h = self.height()
        font = QFont(painter.font())
        font.setPixelSize(max(9, int(h * 0.22)))
        painter.setFont(font)

        # Label row.
        painter.setPen(LABEL_COLOR)
        painter.drawText(QRectF(4, 2, w - 8, h * 0.3), Qt.AlignLeft, self._label)
        if self._value is not None:
            painter.drawText(QRectF(4, 2, w - 8, h * 0.3),
                             Qt.AlignRight, f'{self._value:+.2f}')

        # Track.
        track_h = max(8, int(h * 0.32))
        track_y = h * 0.45
        track = QRectF(6, track_y, w - 12, track_h)
        painter.setPen(QPen(TICK_COLOR, 1))
        painter.setBrush(FACE_COLOR)
        painter.drawRect(track)

        center_x = track.center().x()
        # Center zero line.
        painter.setPen(QPen(TICK_COLOR, 1))
        painter.drawLine(QRectF(center_x, track_y, 0, track_h).topLeft(),
                         QRectF(center_x, track_y, 0, track_h).bottomLeft())

        half_w = track.width() / 2.0

        # Actual fill from center.
        if self._value is not None:
            val = max(-1.0, min(1.0, self._value))
            fill_w = half_w * abs(val)
            painter.setPen(Qt.NoPen)
            painter.setBrush(ACTUAL_COLOR)
            if val >= 0:
                painter.drawRect(QRectF(center_x, track_y + 2, fill_w, track_h - 4))
            else:
                painter.drawRect(QRectF(center_x - fill_w, track_y + 2,
                                        fill_w, track_h - 4))

        # Commanded ghost marker.
        if self._commanded is not None:
            cmd = max(-1.0, min(1.0, self._commanded))
            mx = center_x + half_w * cmd
            color = COMMAND_STALE_COLOR if self._commanded_stale else COMMAND_COLOR
            painter.setPen(QPen(color, 2))
            painter.drawLine(QRectF(mx, track_y - 2, 0, track_h + 4).topLeft(),
                             QRectF(mx, track_y - 2, 0, track_h + 4).bottomLeft())

        # End captions.
        if self._neg_caption or self._pos_caption:
            font.setPixelSize(max(8, int(h * 0.18)))
            painter.setFont(font)
            painter.setPen(TICK_COLOR)
            cap_y = track_y + track_h
            painter.drawText(QRectF(6, cap_y, half_w, h - cap_y),
                             Qt.AlignLeft, self._neg_caption)
            painter.drawText(QRectF(center_x, cap_y, half_w, h - cap_y),
                             Qt.AlignRight, self._pos_caption)
