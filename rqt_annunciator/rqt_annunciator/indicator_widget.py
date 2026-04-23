"""Single annunciator indicator widget."""

from python_qt_binding.QtCore import QEvent, QSize, Qt, Signal
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

        # Cached reference font metrics at a fixed pixel size.  Using a
        # fixed size keeps the EMA decoupled from the rendered font
        # (which varies per-cell); separate bold metrics because the
        # value label is bold.  Rebuilt on font/style change events via
        # changeEvent so a runtime theme/DPI change doesn't leave the
        # cache measuring against an obsolete font family.
        self._rebuild_reference_metrics()

        # Seed the EMA from the label plus the '---' placeholder so the
        # first layout pass, before any data has arrived, reflects that
        # cold-start combined width (label + placeholder + gap).
        #
        # Stored as float so small sample-to-EMA deltas accumulate; only
        # the exposed ema_width_px property rounds to int.  Quantizing
        # the internal state on every update would freeze the EMA for
        # changes smaller than 1/α pixels — e.g., at α=0.05 any sample
        # within 10 px of the current value never moves it.
        self._ema_width = float(
            self._measure_combined_width(name, self._value_text)
        )

        self._apply_level()

    def minimumSizeHint(self):  # noqa: N802 (Qt API)
        return self._MIN_HINT

    def sizeHint(self):  # noqa: N802 (Qt API)
        return self._SIZE_HINT

    @property
    def ema_width_px(self) -> int:
        """Current EMA of this indicator's text width at the reference font.

        Rounded to int on read; the underlying state is float so small
        samples still migrate the average over time.
        """
        return int(round(self._ema_width))

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
        old_int = self.ema_width_px
        self._ema_width = (
            self._EMA_ALPHA * sample
            + (1.0 - self._EMA_ALPHA) * self._ema_width
        )
        self._fit_font()
        new_int = self.ema_width_px
        if new_int != old_int:
            self.width_sample_changed.emit(self._name, new_int)

    def set_stale(self):
        """Mark this indicator as stale (no recent data)."""
        self.set_status(IndicatorLevel.STALE)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._fit_font()

    def changeEvent(self, event):
        super().changeEvent(event)
        if event.type() in (QEvent.FontChange, QEvent.StyleChange):
            # Font family, style, or DPI changed — the cached reference
            # metrics were measured against the old font.  Rebuild and
            # re-fit so the EMA and rendered font stay in sync with
            # what the user actually sees.
            self._rebuild_reference_metrics()
            self._fit_font()

    # -- Internals -------------------------------------------------------------

    def _rebuild_reference_metrics(self):
        """(Re)build the cached reference QFontMetrics at the fixed pixel size.

        Base the reference font on ``self._label.font()`` (the effective
        post-style font, incorporating any stylesheet family, weight,
        letter spacing, etc.) and only override ``pixelSize`` / ``bold``.
        Pass ``self._label`` as the ``QPaintDevice`` so metrics use the
        label's actual DPI — otherwise a HiDPI display would measure
        against pre-scale metrics and misreport widths.
        """
        base_font = self._label.font()

        ref_font = QFont(base_font)
        ref_font.setPixelSize(self._REFERENCE_FONT_PX)
        ref_font.setBold(False)
        self._reference_metrics = QFontMetrics(ref_font, self._label)

        ref_font_bold = QFont(base_font)
        ref_font_bold.setPixelSize(self._REFERENCE_FONT_PX)
        ref_font_bold.setBold(True)
        self._reference_metrics_bold = QFontMetrics(ref_font_bold, self._label)

    def _measure_combined_width(self, label_text: str, value_text: str) -> int:
        """Pixel width of ``label + value`` at the reference font.

        Uses the cached reference metrics built in ``__init__`` so this
        can run on every ``set_status`` and every ``_fit_font`` without
        per-call font allocation.

        The visual gap between label and value is already reserved by
        the real layout spacing (``layout.spacing() × (count − 1)``) in
        ``_fit_font``.  Adding it here too would double-count it and
        make fitted fonts systematically smaller than what renders.
        """
        label_w = self._reference_metrics.horizontalAdvance(label_text)
        value_w = self._reference_metrics_bold.horizontalAdvance(value_text)
        return label_w + value_w

    def _fit_font(self):
        """Pick the largest font that fits ``label + value`` in the cell."""
        # Derive usable interior from the layout's actual margins and
        # spacing plus the color bar width, so tweaks to those constants
        # do not silently break fitting.
        layout = self.layout()
        if layout is not None:
            margins = layout.contentsMargins()
            h_overhead = margins.left() + margins.right()
            v_overhead = margins.top() + margins.bottom()
            spacing = max(0, layout.spacing())
            h_overhead += spacing * max(0, layout.count() - 1)
        else:
            h_overhead = 0
            v_overhead = 0

        # Use the enforced width constraint rather than current geometry.
        # ``setFixedWidth(8)`` pins minimumWidth == maximumWidth == 8, so
        # minimumWidth() is reliable even before the first layout pass,
        # whereas width() may still be the default geometry and
        # overestimate the overhead (making fonts unnecessarily small).
        color_bar_w = max(
            self._color_bar.minimumWidth(),
            self._color_bar.sizeHint().width(),
        )
        h_overhead += color_bar_w

        usable_w = self.width() - h_overhead
        usable_h = self.height() - v_overhead
        ref_w = self._measure_combined_width(
            self._label.text(), self._value_text)

        if usable_w <= 0 or usable_h <= 0 or ref_w <= 0:
            # Widget is tiny or text is empty — fall through with the
            # minimum font so we still overwrite any previously-large
            # font (otherwise shrinking the cell below the layout
            # overhead leaves huge text clipping the cell).
            font_px = self._MIN_FONT_PX
        else:
            # Linear scaling: text widths scale roughly linearly with font px.
            horiz_px = self._REFERENCE_FONT_PX * usable_w / ref_w
            # Text height ≈ font pixel size × ~1.2 for ascent/descent; use
            # a conservative ceiling of 0.7·cell_height so descenders don't
            # touch.
            vert_px = usable_h * 0.7
            font_px = int(min(horiz_px, vert_px))
            font_px = max(self._MIN_FONT_PX, min(self._MAX_FONT_PX, font_px))

        # Start from each label's current font so we preserve any
        # app-wide or theme-applied attributes (family, letter spacing,
        # hinting) and only override what we control here.
        label_font = QFont(self._label.font())
        label_font.setPixelSize(font_px)
        self._label.setFont(label_font)

        value_font = QFont(self._value_label.font())
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
