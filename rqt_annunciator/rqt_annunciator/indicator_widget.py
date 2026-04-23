"""Single annunciator indicator widget."""

from python_qt_binding.QtCore import QSize, Qt
from python_qt_binding.QtGui import QColor, QFont
from python_qt_binding.QtWidgets import QFrame, QHBoxLayout, QLabel, QLayout, QSizePolicy

from .config_model import IndicatorLevel


# Hardcoded dark-until-problem color palette.
_COLORS = {
    IndicatorLevel.OK: QColor(60, 60, 60),        # dim gray — blends into background
    IndicatorLevel.WARN: QColor(255, 180, 0),      # amber
    IndicatorLevel.ERROR: QColor(220, 40, 40),      # red
    IndicatorLevel.STALE: QColor(90, 90, 90),       # muted gray
}

_TEXT_COLORS = {
    IndicatorLevel.OK: QColor(120, 120, 120),      # subtle
    IndicatorLevel.WARN: QColor(255, 200, 50),      # bright amber
    IndicatorLevel.ERROR: QColor(255, 80, 80),       # bright red
    IndicatorLevel.STALE: QColor(130, 130, 130),    # muted
}

_BG_COLOR = QColor(30, 30, 30)  # dark background


class IndicatorWidget(QFrame):
    """A single indicator showing a status color bar, label, and value.

    Fills whatever space it is given by its parent layout and never demands
    more: its minimum/size hints are fixed constants (not derived from font
    or text length), and the child labels use ``QSizePolicy.Ignored`` so
    their font-scaled hints do not propagate up.  See issue #19.
    """

    # Intrinsic size hints — independent of current font size or text length
    # so they cannot feed back into an ancestor's minimum size.
    _MIN_HINT = QSize(40, 20)
    _SIZE_HINT = QSize(160, 48)

    def __init__(self, name: str, parent=None):
        super().__init__(parent)
        self._name = name
        self._level = IndicatorLevel.STALE
        self._value_text = '---'

        self.setAutoFillBackground(True)
        self._set_bg(_BG_COLOR)

        # Fill the cell the grid gives us; do not ask for more than _SIZE_HINT.
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        self._color_bar = QFrame(self)
        self._color_bar.setFixedWidth(8)
        self._color_bar.setAutoFillBackground(True)

        # Ignored horizontal policy on the labels is the key to breaking the
        # feedback loop: QLabel.minimumSizeHint() scales with font metrics,
        # and without Ignored that minimum bubbles up through every parent
        # layout and widens the top-level window on vertical drags.
        label_policy = QSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)

        self._label = QLabel(name, self)
        self._label.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        self._label.setSizePolicy(label_policy)

        self._value_label = QLabel('---', self)
        self._value_label.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self._value_label.setSizePolicy(label_policy)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(4, 2, 8, 2)
        layout.setSpacing(8)
        layout.setSizeConstraint(QLayout.SetNoConstraint)
        layout.addWidget(self._color_bar)
        layout.addWidget(self._label)
        layout.addWidget(self._value_label)

        self._apply_level()

    def minimumSizeHint(self):  # noqa: N802 (Qt API)
        return self._MIN_HINT

    def sizeHint(self):  # noqa: N802 (Qt API)
        return self._SIZE_HINT

    def set_status(self, level: IndicatorLevel, value_text: str = ''):
        """Update the indicator status and displayed value."""
        if value_text:
            self._value_text = value_text
        if level == IndicatorLevel.STALE:
            self._value_text = '---'
        if level != self._level or value_text:
            self._level = level
            self._apply_level()

    def set_stale(self):
        """Mark this indicator as stale (no recent data)."""
        self.set_status(IndicatorLevel.STALE)

    def update_font_size(self, label_size: int, value_size: int):
        """Update font sizes for the label and value."""
        label_font = QFont()
        label_font.setPixelSize(max(label_size, 8))
        self._label.setFont(label_font)

        value_font = QFont()
        value_font.setPixelSize(max(value_size, 8))
        value_font.setBold(True)
        self._value_label.setFont(value_font)

    def _apply_level(self):
        """Apply visual changes for the current level."""
        bar_color = _COLORS[self._level]
        text_color = _TEXT_COLORS[self._level]

        palette = self._color_bar.palette()
        palette.setColor(self._color_bar.backgroundRole(), bar_color)
        self._color_bar.setPalette(palette)

        style = f'color: {text_color.name()};'
        self._label.setStyleSheet(style)
        self._value_label.setStyleSheet(style)
        self._value_label.setText(self._value_text)

    def _set_bg(self, color: QColor):
        palette = self.palette()
        palette.setColor(self.backgroundRole(), color)
        self.setPalette(palette)
