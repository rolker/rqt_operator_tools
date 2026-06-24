"""Arc speed gauge.

Actual speed-over-ground (‖odom twist‖, m/s) is the filled arc with a needle;
the commanded speed (from ``cmd_vel``) is a thin ghost needle.  Knots are the
primary readout, m/s secondary.
"""

import math

from python_qt_binding.QtCore import QPointF, QRectF, QSize, Qt
from python_qt_binding.QtGui import QFont, QPen
from python_qt_binding.QtWidgets import QSizePolicy, QWidget

from ..config_model import is_valid_measurement, mps_to_knots
from . import (
    ACTUAL_COLOR,
    BG_COLOR,
    COMMAND_COLOR,
    COMMAND_STALE_COLOR,
    FACE_COLOR,
    LABEL_COLOR,
    TICK_COLOR,
)

# Arc geometry: sweep 240° centered on the top (−210°..+30° in screen space).
_START_DEG = 210.0   # left end
_SWEEP_DEG = 240.0   # clockwise sweep down through the bottom to the right


class SpeedGauge(QWidget):
    """240° arc gauge for speed-over-ground with a commanded ghost needle."""

    _MIN_HINT = QSize(120, 120)
    _SIZE_HINT = QSize(180, 180)

    def __init__(self, arc_max=5.0, parent=None):
        super().__init__(parent)
        self._max = max(0.1, arc_max)
        self._speed = None       # m/s actual, or None when stale
        self._commanded = None   # m/s commanded, or None when absent
        self._commanded_stale = False
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), BG_COLOR)
        self.setPalette(palette)

    def minimumSizeHint(self):  # noqa: N802 (Qt API)
        return self._MIN_HINT

    def sizeHint(self):  # noqa: N802 (Qt API)
        return self._SIZE_HINT

    def set_arc_max(self, arc_max):
        self._max = max(0.1, arc_max)
        self.update()

    def set_speed(self, speed_mps):
        # Non-finite (NaN/inf) speed is treated as no-data so the arc/readout
        # do not show a full-deflection "nan kn" instead of a dash.
        self._speed = speed_mps if is_valid_measurement(speed_mps) else None
        self.update()

    def set_commanded(self, speed_mps, stale=False):
        self._commanded = speed_mps if is_valid_measurement(speed_mps) else None
        self._commanded_stale = stale
        self.update()

    def mark_commanded_stale(self):
        """Grey a frozen commanded needle once its source (cmd_vel) goes stale."""
        self._commanded_stale = True
        self.update()

    def _frac(self, value):
        return max(0.0, min(1.0, value / self._max))

    def paintEvent(self, event):  # noqa: N802 (Qt API)
        from python_qt_binding.QtGui import QPainter

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)

        size = min(self.width(), self.height())
        cx = self.width() / 2.0
        cy = self.height() / 2.0
        radius = size / 2.0 - 6
        rect = QRectF(cx - radius, cy - radius, 2 * radius, 2 * radius)

        # Background arc.  Qt angles: 0° at 3 o'clock, CCW positive, in
        # 1/16-degree units.  Our scale starts at 210° and sweeps -240°.
        painter.setPen(QPen(FACE_COLOR, max(4, int(radius * 0.18)),
                            Qt.SolidLine, Qt.FlatCap))
        painter.drawArc(rect, int(_START_DEG * 16), int(-_SWEEP_DEG * 16))

        # Filled actual-speed arc.
        if self._speed is not None:
            frac = self._frac(self._speed)
            painter.setPen(QPen(ACTUAL_COLOR, max(4, int(radius * 0.18)),
                                Qt.SolidLine, Qt.FlatCap))
            painter.drawArc(rect, int(_START_DEG * 16),
                            int(-_SWEEP_DEG * frac * 16))

        # Commanded ghost needle.
        if self._commanded is not None:
            color = COMMAND_STALE_COLOR if self._commanded_stale else COMMAND_COLOR
            self._draw_needle(painter, cx, cy, radius * 0.95,
                              self._frac(self._commanded), color, width=2)

        # Ticks.
        painter.setPen(QPen(TICK_COLOR, 1))
        for i in range(0, 6):
            frac = i / 5.0
            ang = math.radians(_START_DEG - _SWEEP_DEG * frac)
            x1 = cx + radius * 0.72 * math.cos(ang)
            y1 = cy - radius * 0.72 * math.sin(ang)
            x2 = cx + radius * 0.86 * math.cos(ang)
            y2 = cy - radius * 0.86 * math.sin(ang)
            painter.drawLine(QPointF(x1, y1), QPointF(x2, y2))

        # Digital readout: knots primary, m/s secondary.
        font = QFont(painter.font())
        painter.setPen(LABEL_COLOR)
        if self._speed is None:
            knots_text, mps_text = '--', '-- m/s'
        else:
            knots_text = f'{mps_to_knots(self._speed):.1f} kn'
            mps_text = f'{self._speed:.2f} m/s'
        font.setPixelSize(max(12, int(radius * 0.3)))
        painter.setFont(font)
        painter.drawText(QRectF(cx - radius, cy - radius * 0.1,
                                2 * radius, radius * 0.4),
                         Qt.AlignCenter, knots_text)
        font.setPixelSize(max(8, int(radius * 0.16)))
        painter.setFont(font)
        painter.setPen(TICK_COLOR)
        painter.drawText(QRectF(cx - radius, cy + radius * 0.3,
                                2 * radius, radius * 0.3),
                         Qt.AlignCenter, mps_text)

    def _draw_needle(self, painter, cx, cy, length, frac, color, width=2):
        ang = math.radians(_START_DEG - _SWEEP_DEG * frac)
        tip = QPointF(cx + length * math.cos(ang), cy - length * math.sin(ang))
        painter.setPen(QPen(color, width))
        painter.drawLine(QPointF(cx, cy), tip)
