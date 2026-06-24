"""Compass-rose heading gauge.

Shows the vessel's actual heading (a needle) and, when moving faster than the
COG min-speed gate, a course-over-ground arrow.  Headings are compass degrees
(clockwise from North); the widget does not convert frames itself — the panel
feeds it ``heading=(90−yaw) mod 360`` from the ENU odom.
"""

import math

from python_qt_binding.QtCore import QPointF, QRectF, QSize, Qt
from python_qt_binding.QtGui import QFont, QPen, QPolygonF
from python_qt_binding.QtWidgets import QSizePolicy, QWidget

from . import (
    ACTUAL_COLOR,
    BG_COLOR,
    COMMAND_COLOR,
    FACE_COLOR,
    LABEL_COLOR,
    TICK_COLOR,
)

_CARDINALS = ['N', 'E', 'S', 'W']


class HeadingGauge(QWidget):
    """Compass rose with an actual-heading needle and optional COG arrow."""

    _MIN_HINT = QSize(120, 120)
    _SIZE_HINT = QSize(180, 180)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._heading = None  # compass degrees, or None when stale
        self._cog = None      # compass degrees, or None when not moving/stale
        self._stale = True
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), BG_COLOR)
        self.setPalette(palette)

    def minimumSizeHint(self):  # noqa: N802 (Qt API)
        return self._MIN_HINT

    def sizeHint(self):  # noqa: N802 (Qt API)
        return self._SIZE_HINT

    def set_heading(self, heading_deg, stale=False):
        """Set the actual heading (compass deg) or None when unavailable."""
        self._heading = heading_deg
        self._stale = stale
        self.update()

    def mark_stale(self):
        """Grey the gauge (keeps the last heading value but hides the needle)."""
        self._stale = True
        self.update()

    def set_cog(self, cog_deg):
        """Set the course-over-ground arrow (compass deg) or None to hide it."""
        self._cog = cog_deg
        self.update()

    def paintEvent(self, event):  # noqa: N802 (Qt API)
        from python_qt_binding.QtGui import QPainter

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)

        size = min(self.width(), self.height())
        cx = self.width() / 2.0
        cy = self.height() / 2.0
        radius = size / 2.0 - 6

        # Face.
        painter.setPen(QPen(TICK_COLOR, 1))
        painter.setBrush(FACE_COLOR)
        painter.drawEllipse(QPointF(cx, cy), radius, radius)

        # Tick marks every 30°, cardinal labels.
        painter.setPen(QPen(TICK_COLOR, 1))
        font = QFont(painter.font())
        font.setPixelSize(max(8, int(radius * 0.16)))
        painter.setFont(font)
        for deg in range(0, 360, 30):
            ang = math.radians(deg - 90)  # 0° = North = up
            inner = radius * (0.82 if deg % 90 else 0.72)
            x1 = cx + inner * math.cos(ang)
            y1 = cy + inner * math.sin(ang)
            x2 = cx + radius * math.cos(ang)
            y2 = cy + radius * math.sin(ang)
            painter.drawLine(QPointF(x1, y1), QPointF(x2, y2))
            if deg % 90 == 0:
                label = _CARDINALS[deg // 90]
                lx = cx + radius * 0.6 * math.cos(ang)
                ly = cy + radius * 0.6 * math.sin(ang)
                painter.setPen(LABEL_COLOR)
                painter.drawText(
                    QRectF(lx - 10, ly - 8, 20, 16),
                    Qt.AlignCenter, label)
                painter.setPen(QPen(TICK_COLOR, 1))

        # COG arrow (drawn under the heading needle).
        if self._cog is not None:
            self._draw_needle(painter, cx, cy, radius * 0.78,
                              self._cog, COMMAND_COLOR, width=3, arrow=True)

        # Actual heading needle.
        if self._heading is not None and not self._stale:
            self._draw_needle(painter, cx, cy, radius * 0.9,
                              self._heading, ACTUAL_COLOR, width=3, arrow=False)

        # Center digital readout.
        painter.setPen(LABEL_COLOR)
        font.setPixelSize(max(10, int(radius * 0.26)))
        painter.setFont(font)
        if self._heading is None or self._stale:
            text = '---'
        else:
            text = f'{self._heading:03.0f}°'
        painter.drawText(QRectF(cx - radius, cy + radius * 0.3,
                                2 * radius, radius * 0.4),
                         Qt.AlignCenter, text)

    def _draw_needle(self, painter, cx, cy, length, heading_deg, color,
                     width=3, arrow=False):
        ang = math.radians(heading_deg - 90)  # compass → screen (N up)
        tip = QPointF(cx + length * math.cos(ang), cy + length * math.sin(ang))
        painter.setPen(QPen(color, width))
        painter.drawLine(QPointF(cx, cy), tip)
        if arrow:
            head = math.radians(20)
            back = length * 0.18
            left = QPointF(
                tip.x() - back * math.cos(ang - head),
                tip.y() - back * math.sin(ang - head))
            right = QPointF(
                tip.x() - back * math.cos(ang + head),
                tip.y() - back * math.sin(ang + head))
            painter.setBrush(color)
            painter.drawPolygon(QPolygonF([tip, left, right]))
