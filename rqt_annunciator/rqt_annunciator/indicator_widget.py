"""Single annunciator indicator widget."""

from python_qt_binding.QtCore import QSize, Qt, Signal
from python_qt_binding.QtGui import QColor, QFont, QFontMetrics
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

    Font sizing is per-cell: on every resize and every value update the
    indicator picks the largest font that fits ``label + value`` in its
    allocated width (also capped by height).  An EMA of the indicator's
    rendered text width at a reference font size is exposed via
    ``ema_width_px`` and ``width_sample_changed``, so the container can
    size its grid columns to fit smoothed content instead of uniformly.
    See issue #23.
    """

    # Intrinsic size hints — independent of current font size or text length
    # so they cannot feed back into an ancestor's minimum size.
    _MIN_HINT = QSize(40, 20)
    _SIZE_HINT = QSize(160, 48)

    # Font-fit bounds, in pixels.
    _MIN_FONT_PX = 8
    _MAX_FONT_PX = 72

    # Reference font size used for all text-width measurements feeding
    # the EMA — using a fixed reference keeps the average decoupled from
    # the actually-rendered font (which varies with cell size).
    _REFERENCE_FONT_PX = 14

    # EMA smoothing factor.  At 2 Hz updates, α = 0.05 ≈ 6 s to visually
    # migrate, ~20 s to fully converge; feels like a gentle drift.
    _EMA_ALPHA = 0.05

    # Emitted after ``set_status`` when the EMA width changes, so the
    # container can restretch its grid columns.
    width_sample_changed = Signal(str, int)  # (indicator_name, ema_width_px)

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

        # Reference font metrics for width measurement — rebuilt on demand
        # in case the font family ever changes (currently it does not).
        ref_font = QFont()
        ref_font.setPixelSize(self._REFERENCE_FONT_PX)
        self._reference_metrics = QFontMetrics(ref_font)

        # Seed the EMA from the label text alone so the first layout pass,
        # before any data has arrived, reflects the natural label width.
        self._ema_width_px = self._measure_combined_width(name, self._value_text)

        self._apply_level()

    def minimumSizeHint(self):  # noqa: N802 (Qt API)
        return self._MIN_HINT

    def sizeHint(self):  # noqa: N802 (Qt API)
        return self._SIZE_HINT

    @property
    def ema_width_px(self) -> int:
        """Current EMA of this indicator's text width at the reference font."""
        return self._ema_width_px

    def set_status(self, level: IndicatorLevel, value_text: str = ''):
        """Update the indicator status and displayed value."""
        if value_text:
            self._value_text = value_text
        if level == IndicatorLevel.STALE:
            self._value_text = '---'
        if level != self._level or value_text:
            self._level = level
            self._apply_level()

        # Update EMA from the currently-displayed text and re-fit the font
        # so a sudden long value shrinks in-cell while the grid migrates.
        sample = self._measure_combined_width(self._label.text(), self._value_text)
        old_width = self._ema_width_px
        self._ema_width_px = int(round(
            self._EMA_ALPHA * sample + (1.0 - self._EMA_ALPHA) * self._ema_width_px
        ))
        self._fit_font()
        if self._ema_width_px != old_width:
            self.width_sample_changed.emit(self._name, self._ema_width_px)

    def set_stale(self):
        """Mark this indicator as stale (no recent data)."""
        self.set_status(IndicatorLevel.STALE)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._fit_font()

    # -- Internals -------------------------------------------------------------

    def _measure_combined_width(self, label_text: str, value_text: str) -> int:
        """Pixel width of ``label + spacer + value`` at the reference font."""
        # Value is rendered bold, so measure bold; label is regular.
        bold_font = QFont()
        bold_font.setPixelSize(self._REFERENCE_FONT_PX)
        bold_font.setBold(True)
        bold_metrics = QFontMetrics(bold_font)
        label_w = self._reference_metrics.horizontalAdvance(label_text)
        value_w = bold_metrics.horizontalAdvance(value_text)
        # A small gap between label and value so they don't visually touch.
        gap = self._reference_metrics.horizontalAdvance('  ')
        return label_w + gap + value_w

    def _fit_font(self):
        """Pick the largest font that fits ``label + value`` in the cell."""
        # Subtract fixed decorations from the cell's usable width:
        # margins (4 + 8) + color bar (8) + 2 spacings of 8 = 36 px.
        usable_w = self.width() - 36
        usable_h = self.height() - 4  # vertical margins
        if usable_w <= 0 or usable_h <= 0:
            return

        ref_w = self._measure_combined_width(
            self._label.text(), self._value_text)
        if ref_w <= 0:
            return

        # Linear scaling: text widths scale roughly linearly with font px.
        horiz_px = self._REFERENCE_FONT_PX * usable_w / ref_w
        # Text height ≈ font pixel size × ~1.2 for ascent/descent; use a
        # conservative ceiling of 0.7·cell_height so descenders don't touch.
        vert_px = usable_h * 0.7
        font_px = int(min(horiz_px, vert_px))
        font_px = max(self._MIN_FONT_PX, min(self._MAX_FONT_PX, font_px))

        label_font = QFont()
        label_font.setPixelSize(font_px)
        self._label.setFont(label_font)

        value_font = QFont()
        value_font.setPixelSize(font_px)
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
