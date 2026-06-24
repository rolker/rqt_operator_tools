"""Center-zero bar gauge shared by Steering and Throttle.

Both steering and throttle are −1..1 commands with a meaningful zero, so they
share one widget: a track with the actual value filled from the center and a
commanded ghost marker.  Steering reads left/right, so it renders
**horizontal**; throttle reads reverse/ahead, so it renders **vertical**
(ahead up, reverse down).  Orientation is chosen per instance.
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
    """−1..1 bar with a center zero and a commanded ghost marker.

    ``orientation`` is ``'horizontal'`` (default; positive → right) or
    ``'vertical'`` (positive → up).
    """

    _MIN_HINT_H = QSize(120, 48)
    _SIZE_HINT_H = QSize(200, 64)
    _MIN_HINT_V = QSize(72, 120)
    _SIZE_HINT_V = QSize(96, 200)
    # Shared bar thickness so the horizontal track's height matches the
    # vertical track's width (steering bar height == throttle bar width).
    _BAR_THICKNESS = 24

    def __init__(self, label='', neg_caption='', pos_caption='',
                 secondary_label='', orientation='horizontal', parent=None):
        super().__init__(parent)
        self._label = label
        self._neg_caption = neg_caption
        self._pos_caption = pos_caption
        self._vertical = (orientation == 'vertical')
        self._value = None       # −1..1 actual, or None when stale
        self._value_stale = False  # True greys a frozen actual reading
        self._commanded = None   # −1..1 commanded, or None when absent
        self._commanded_stale = False
        # Optional secondary commanded indicator (horizontal only) — e.g. the
        # autonomy's commanded yaw rate under the rudder bar.
        self._secondary_label = secondary_label
        self._secondary = None       # −1..1 normalized, or None when absent
        self._secondary_text = ''    # human-readable value (e.g. "12°/s")
        self._secondary_stale = False
        if self._vertical:
            self.setSizePolicy(QSizePolicy.Preferred, QSizePolicy.Expanding)
        else:
            self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), BG_COLOR)
        self.setPalette(palette)

    def minimumSizeHint(self):  # noqa: N802 (Qt API)
        return self._MIN_HINT_V if self._vertical else self._MIN_HINT_H

    def sizeHint(self):  # noqa: N802 (Qt API)
        return self._SIZE_HINT_V if self._vertical else self._SIZE_HINT_H

    def set_value(self, value):
        self._value = value
        self._value_stale = False
        self.update()

    def set_commanded(self, value, stale=False):
        self._commanded = value
        self._commanded_stale = stale
        self.update()

    def set_secondary(self, value, text='', stale=False):
        """Set the secondary commanded indicator (normalized −1..1) + label."""
        self._secondary = value
        self._secondary_text = text
        self._secondary_stale = stale
        self.update()

    def mark_secondary_stale(self):
        self._secondary_stale = True
        self.update()

    def mark_stale(self):
        """Grey a frozen actual reading once its source (rc_out) goes stale."""
        self._value_stale = True
        self.update()

    def mark_commanded_stale(self):
        """Grey a frozen commanded marker once its source (helm) goes stale."""
        self._commanded_stale = True
        self.update()

    def paintEvent(self, event):  # noqa: N802 (Qt API)
        from python_qt_binding.QtGui import QPainter

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)

        w = self.width()
        h = self.height()
        # Cap the label font: scaling it by the (tall) widget height made the
        # text overflow its 16 px rect and get clipped ("Steering" half-hidden).
        font = QFont(painter.font())
        label_px = max(10, min(15, int(w * 0.10)))
        font.setPixelSize(label_px)
        painter.setFont(font)

        # Value row: the signed actual, centered (the captions identify the
        # gauge, so no text label).  A stale reading is dimmed so a frozen value
        # is not mistaken for a live one.
        label_h = label_px + 4
        if self._value is not None:
            painter.setPen(TICK_COLOR if self._value_stale else LABEL_COLOR)
            painter.drawText(QRectF(4, 2, w - 8, label_h),
                             Qt.AlignHCenter, f'{self._value:+.2f}')

        if self._vertical:
            self._paint_vertical(painter, w, h, font)
        else:
            self._paint_horizontal(painter, w, h, font)

    def _fill_brush(self, stale):
        return COMMAND_STALE_COLOR if stale else ACTUAL_COLOR

    def _cmd_pen(self, stale):
        color = COMMAND_STALE_COLOR if stale else COMMAND_COLOR
        return QPen(color, 2)

    def _paint_horizontal(self, painter, w, h, font):
        # Anchor the track just below the label row rather than at the widget's
        # vertical middle, so it doesn't float when sharing a tall row with the
        # vertical throttle.
        track_h = self._BAR_THICKNESS
        track_y = 26.0
        track = QRectF(6, track_y, w - 12, track_h)
        painter.setPen(QPen(TICK_COLOR, 1))
        painter.setBrush(FACE_COLOR)
        painter.drawRect(track)

        center_x = track.center().x()
        painter.setPen(QPen(TICK_COLOR, 1))
        painter.drawLine(QRectF(center_x, track_y, 0, track_h).topLeft(),
                         QRectF(center_x, track_y, 0, track_h).bottomLeft())
        half_w = track.width() / 2.0

        if self._value is not None:
            val = max(-1.0, min(1.0, self._value))
            fill_w = half_w * abs(val)
            painter.setPen(Qt.NoPen)
            painter.setBrush(self._fill_brush(self._value_stale))
            if val >= 0:
                painter.drawRect(QRectF(center_x, track_y + 2, fill_w, track_h - 4))
            else:
                painter.drawRect(QRectF(center_x - fill_w, track_y + 2,
                                        fill_w, track_h - 4))

        if self._commanded is not None:
            cmd = max(-1.0, min(1.0, self._commanded))
            mx = center_x + half_w * cmd
            painter.setPen(self._cmd_pen(self._commanded_stale))
            painter.drawLine(QRectF(mx, track_y - 2, 0, track_h + 4).topLeft(),
                             QRectF(mx, track_y - 2, 0, track_h + 4).bottomLeft())

        if self._neg_caption or self._pos_caption:
            font.setPixelSize(max(9, min(13, int(w * 0.06))))
            painter.setFont(font)
            painter.setPen(TICK_COLOR)
            cap_y = track_y + track_h + 2
            cap_h = min(16.0, h - cap_y)
            painter.drawText(QRectF(6, cap_y, half_w, cap_h),
                             Qt.AlignLeft, self._neg_caption)
            painter.drawText(QRectF(center_x, cap_y, half_w, cap_h),
                             Qt.AlignRight, self._pos_caption)

        # Secondary commanded indicator (e.g. autonomy yaw-rate) in the space
        # below the rudder bar.  Center-zero thin track + amber marker.
        if self._secondary_label and track_y + track_h + 56 <= h:
            font.setPixelSize(max(9, min(12, int(w * 0.055))))
            painter.setFont(font)
            lbl_y = track_y + track_h + 22
            txt = self._secondary_label
            if self._secondary_text:
                txt = f'{self._secondary_label}: {self._secondary_text}'
            painter.setPen(COMMAND_STALE_COLOR if self._secondary_stale
                           else COMMAND_COLOR)
            painter.drawText(QRectF(6, lbl_y, w - 12, 14), Qt.AlignLeft, txt)

            st_y = lbl_y + 16
            st_h = max(6, int(h * 0.08))
            st = QRectF(6, st_y, w - 12, st_h)
            painter.setPen(QPen(TICK_COLOR, 1))
            painter.setBrush(FACE_COLOR)
            painter.drawRect(st)
            sc_x = st.center().x()
            painter.drawLine(QRectF(sc_x, st_y, 0, st_h).topLeft(),
                             QRectF(sc_x, st_y, 0, st_h).bottomLeft())
            if self._secondary is not None:
                sval = max(-1.0, min(1.0, self._secondary))
                smx = sc_x + (st.width() / 2.0) * sval
                painter.setPen(self._cmd_pen(self._secondary_stale))
                painter.drawLine(QRectF(smx, st_y - 2, 0, st_h + 4).topLeft(),
                                 QRectF(smx, st_y - 2, 0, st_h + 4).bottomLeft())

    def _paint_vertical(self, painter, w, h, font):
        # Track runs vertically; positive (ahead) fills UP from the center,
        # negative (reverse) fills DOWN.  Reserve distinct bands so the label,
        # the 'ahead' caption, the track, and the 'reverse' caption never
        # overlap: label [2..20], ahead [20..34], track [34..h-32],
        # reverse [h-30..h-14].
        cap_px = max(9, min(13, int(w * 0.11)))
        top = 36.0
        bottom = h - 32.0
        track_w = self._BAR_THICKNESS
        track_x = (w - track_w) / 2.0
        track = QRectF(track_x, top, track_w, max(1.0, bottom - top))
        painter.setPen(QPen(TICK_COLOR, 1))
        painter.setBrush(FACE_COLOR)
        painter.drawRect(track)

        center_y = track.center().y()
        painter.setPen(QPen(TICK_COLOR, 1))
        painter.drawLine(QRectF(track_x, center_y, track_w, 0).topLeft(),
                         QRectF(track_x, center_y, track_w, 0).topRight())
        half_h = track.height() / 2.0

        if self._value is not None:
            val = max(-1.0, min(1.0, self._value))
            fill_h = half_h * abs(val)
            painter.setPen(Qt.NoPen)
            painter.setBrush(self._fill_brush(self._value_stale))
            if val >= 0:  # ahead → up from center
                painter.drawRect(QRectF(track_x + 2, center_y - fill_h,
                                        track_w - 4, fill_h))
            else:         # reverse → down from center
                painter.drawRect(QRectF(track_x + 2, center_y,
                                        track_w - 4, fill_h))

        if self._commanded is not None:
            cmd = max(-1.0, min(1.0, self._commanded))
            my = center_y - half_h * cmd
            painter.setPen(self._cmd_pen(self._commanded_stale))
            painter.drawLine(QRectF(track_x - 2, my, track_w + 4, 0).topLeft(),
                             QRectF(track_x - 2, my, track_w + 4, 0).topRight())

        if self._neg_caption or self._pos_caption:
            font.setPixelSize(cap_px)
            painter.setFont(font)
            painter.setPen(TICK_COLOR)
            painter.drawText(QRectF(2, top - 16, w - 4, 14),
                             Qt.AlignHCenter, self._pos_caption)   # ahead, above track
            painter.drawText(QRectF(2, bottom + 2, w - 4, 14),
                             Qt.AlignHCenter, self._neg_caption)   # reverse, below track
