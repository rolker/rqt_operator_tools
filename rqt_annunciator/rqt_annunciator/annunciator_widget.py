"""Core annunciator widget — manages indicators, subscriptions, and layout."""

import math
import time

from python_qt_binding.QtCore import QSize, QTimer, Signal
from python_qt_binding.QtGui import QColor
from python_qt_binding.QtWidgets import QGridLayout, QLayout, QWidget

from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus
from rosidl_runtime_py.utilities import get_message

from .config_model import (
    AnnunciatorConfig,
    IndicatorConfig,
    IndicatorLevel,
)
from .indicator_widget import IndicatorWidget


def _extract_value(msg, field_path: str):
    """Extract a value from a message via a dot-separated field path."""
    obj = msg
    for attr in field_path.split('.'):
        obj = getattr(obj, attr)
    return obj


def _diagnostic_level_to_indicator(level: int) -> IndicatorLevel:
    """Map DiagnosticStatus level byte to IndicatorLevel."""
    if level == DiagnosticStatus.OK:
        return IndicatorLevel.OK
    if level == DiagnosticStatus.WARN:
        return IndicatorLevel.WARN
    if level == DiagnosticStatus.STALE:
        return IndicatorLevel.STALE
    return IndicatorLevel.ERROR


class AnnunciatorWidget(QWidget):
    """Container widget for a set of annunciator indicators.

    Uses a single QGridLayout for the widget lifetime.  On resize the
    indicators are reflowed into a different number of columns/rows
    based on the window aspect ratio.

    The widget fills whatever space its parent allocates and never pushes
    for more: size hints are fixed constants, the layout uses
    ``SetNoConstraint`` so the grid's aggregated minimum does not enforce
    a minimum on this widget, and the child indicators break the
    font-minimum feedback loop via ``QSizePolicy.Ignored`` on their
    labels.  See issue #19.
    """

    # Signals for thread-safe UI updates from ROS callbacks.
    _diagnostics_received = Signal(object)
    _topic_received = Signal(str, object)  # indicator name, message

    # Intrinsic hints — do not depend on indicator count or font size.
    _MIN_HINT = QSize(80, 40)
    _SIZE_HINT = QSize(320, 160)

    def __init__(self, node, parent=None):
        super().__init__(parent)
        self._node = node
        self._config = AnnunciatorConfig()
        self._indicators: dict[str, IndicatorWidget] = {}
        self._indicator_configs: dict[str, IndicatorConfig] = {}
        self._last_update: dict[str, float] = {}
        self._subscriptions: dict[str, object] = {}
        self._diag_sub = None
        self._current_cols = 0
        # Last stretch weights applied via setColumnStretch, indexed by
        # column.  Used to skip redundant calls when the weight is
        # unchanged — see _restretch_columns.
        self._last_column_stretches: list[int] = []

        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), QColor(30, 30, 30))
        self.setPalette(palette)

        # Leave the size policy at the default (Preferred/Preferred).  This
        # widget sits inside an rqt dock that may share a splitter with
        # other plugins — using Expanding here would make the annunciator
        # fight its neighbors for space and resist being shrunk when the
        # user drags the splitter handle.  The grid layout still fills
        # whatever space the dock gives us; Preferred only affects how
        # surplus space is *shared* with siblings, not whether we fill
        # our allocation.

        self._layout = QGridLayout(self)
        self._layout.setContentsMargins(4, 4, 4, 4)
        self._layout.setSpacing(4)
        # Prevent the grid's aggregated minimumSize (= sum of indicator
        # minimums across columns) from being imposed as this widget's
        # own minimum, which would propagate up to the rqt window.
        self._layout.setSizeConstraint(QLayout.SetNoConstraint)

        # Wire signals for thread-safe updates.
        self._diagnostics_received.connect(self._handle_diagnostics)
        self._topic_received.connect(self._handle_topic_message)

        # Stale detection timer (1 Hz).
        self._stale_timer = QTimer(self)
        self._stale_timer.timeout.connect(self._check_stale)
        self._stale_timer.start(1000)

        # Debounce restretch requests: many width_sample_changed
        # emissions can fire per event-loop iteration (one per
        # indicator at ROS callback rate).  A zero-interval
        # single-shot timer coalesces them into one restretch scan.
        self._restretch_timer = QTimer(self)
        self._restretch_timer.setSingleShot(True)
        self._restretch_timer.timeout.connect(self._restretch_columns)

    def load_config(self, config: AnnunciatorConfig):
        """Apply a new configuration, rebuilding all indicators and subscriptions."""
        self._teardown_subscriptions()
        self._clear_indicators()
        self._config = config

        for ind_config in config.indicators:
            name = ind_config.name
            if name in self._indicators:
                self._node.get_logger().warn(
                    f'Duplicate indicator name "{name}" — skipping')
                continue
            widget = IndicatorWidget(name, self)
            widget.width_sample_changed.connect(self._on_width_sample)
            self._indicators[name] = widget
            self._indicator_configs[name] = ind_config
            self._last_update[name] = time.monotonic()

        self._current_cols = 0  # force reflow
        self._rebuild_layout()
        self._setup_subscriptions()

    def get_config(self) -> AnnunciatorConfig:
        """Return the current configuration."""
        return self._config

    def shutdown(self):
        """Clean up subscriptions and timers."""
        self._stale_timer.stop()
        self._restretch_timer.stop()
        self._teardown_subscriptions()

    # -- Layout ----------------------------------------------------------------

    def minimumSizeHint(self):  # noqa: N802 (Qt API)
        return self._MIN_HINT

    def sizeHint(self):  # noqa: N802 (Qt API)
        return self._SIZE_HINT

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._rebuild_layout()

    def _rebuild_layout(self):
        """Reflow indicators into the grid and restretch columns.

        Column widths come from each indicator's EMA of rendered text width
        (see issue #23); fonts are sized per-cell by each IndicatorWidget
        from its own resizeEvent, so this method no longer touches fonts.
        """
        n = len(self._indicators)
        if n == 0:
            return

        w = self.width()
        h = self.height()
        aspect = w / max(h, 1)

        if aspect > 2.0:
            # Wide — single row.
            cols = n
        elif aspect < 0.5:
            # Tall — single column.
            cols = 1
        else:
            # Grid — aim for roughly square cells.
            cols = max(1, round(math.sqrt(n * aspect)))

        # Only reflow grid positions when column count changes.
        if cols != self._current_cols:
            # Remove all widgets from grid (without destroying them).
            for widget in self._indicators.values():
                self._layout.removeWidget(widget)

            for i, widget in enumerate(self._indicators.values()):
                self._layout.addWidget(widget, i // cols, i % cols)

            self._current_cols = cols

        self._restretch_columns()

    def _restretch_columns(self):
        """Set each column's stretch from the max EMA width of its indicators.

        Called on every ``width_sample_changed`` signal, which can fire
        several times per second across many indicators.  To avoid
        forcing a full layout recompute on every sample, the previously
        applied weights are cached and ``setColumnStretch`` is only
        called for columns whose weight actually changed.  Columns that
        were active under a wider reflow but aren't anymore are zeroed
        once, not on every call.
        """
        cols = self._current_cols
        if cols <= 0 or not self._indicators:
            return

        indicators = list(self._indicators.values())
        col_widths = [0] * cols
        for i, widget in enumerate(indicators):
            c = i % cols
            if widget.ema_width_px > col_widths[c]:
                col_widths[c] = widget.ema_width_px

        # Stretch weights are relative; pixel-width averages work directly.
        new_stretches = [max(1, w) for w in col_widths]
        previous = self._last_column_stretches

        # Zero any columns that were used previously but are no longer.
        for c in range(cols, len(previous)):
            self._layout.setColumnStretch(c, 0)

        # Update only columns whose weight changed.
        for c, weight in enumerate(new_stretches):
            prev = previous[c] if c < len(previous) else None
            if prev != weight:
                self._layout.setColumnStretch(c, weight)

        self._last_column_stretches = new_stretches

    def _on_width_sample(self, _name, _width):
        """Triggered by IndicatorWidget.width_sample_changed; schedule restretch.

        We don't scan immediately — multiple indicators commonly emit
        in the same event-loop iteration, and every scan touches all
        columns.  A zero-interval single-shot timer coalesces all
        signals between iterations into a single ``_restretch_columns``
        call.
        """
        if not self._restretch_timer.isActive():
            self._restretch_timer.start(0)

    # -- Subscriptions ---------------------------------------------------------

    def _setup_subscriptions(self):
        """Create ROS subscriptions based on the current config."""
        # Diagnostics subscription (shared by all diagnostics indicators).
        has_diag = any(
            c.source == 'diagnostics' for c in self._config.indicators
        )
        if has_diag:
            self._diag_sub = self._node.create_subscription(
                DiagnosticArray,
                self._config.diagnostics_topic,
                self._on_diagnostics,
                10,
            )

        # Per-indicator topic subscriptions.
        for ind_config in self._config.indicators:
            if ind_config.source != 'topic' or not ind_config.topic:
                continue
            try:
                msg_class = get_message(ind_config.msg_type)
            except Exception:
                self._node.get_logger().error(
                    f'Cannot load message type "{ind_config.msg_type}" '
                    f'for indicator "{ind_config.name}"'
                )
                continue
            name = ind_config.name
            sub = self._node.create_subscription(
                msg_class,
                ind_config.topic,
                lambda msg, n=name: self._on_topic_message(n, msg),
                10,
            )
            self._subscriptions[name] = sub

    def _teardown_subscriptions(self):
        """Destroy all ROS subscriptions."""
        if self._diag_sub is not None:
            self._node.destroy_subscription(self._diag_sub)
            self._diag_sub = None
        for sub in self._subscriptions.values():
            self._node.destroy_subscription(sub)
        self._subscriptions.clear()

    # -- ROS callbacks (run on executor thread) --------------------------------

    def _on_diagnostics(self, msg: DiagnosticArray):
        self._diagnostics_received.emit(msg)

    def _on_topic_message(self, indicator_name: str, msg):
        self._topic_received.emit(indicator_name, msg)

    # -- UI-thread handlers (via signals) --------------------------------------

    def _handle_diagnostics(self, msg: DiagnosticArray):
        for status in msg.status:
            for name, config in self._indicator_configs.items():
                if config.source != 'diagnostics':
                    continue
                if not config.matches_diagnostic(status.name):
                    continue
                widget = self._indicators.get(name)
                if widget is None:
                    continue

                level = _diagnostic_level_to_indicator(status.level)
                # Try to format a value from key-value pairs.
                value_text = ''
                if status.values:
                    # Use the first value by default.
                    try:
                        val = float(status.values[0].value)
                        value_text = config.format.format(val)
                    except (ValueError, IndexError, KeyError):
                        value_text = status.message or status.values[0].value

                if not value_text:
                    value_text = status.message or level.name

                widget.set_status(level, value_text)
                self._last_update[name] = time.monotonic()

    def _handle_topic_message(self, indicator_name: str, msg):
        config = self._indicator_configs.get(indicator_name)
        widget = self._indicators.get(indicator_name)
        if config is None or widget is None:
            return

        try:
            value = _extract_value(msg, config.value_field)
        except AttributeError:
            widget.set_status(IndicatorLevel.ERROR, 'BAD FIELD')
            self._last_update[indicator_name] = time.monotonic()
            return

        level = config.evaluate_level(value)
        try:
            value_text = config.format.format(value)
        except (ValueError, TypeError):
            value_text = str(value)

        widget.set_status(level, value_text)
        self._last_update[indicator_name] = time.monotonic()

    # -- Stale detection -------------------------------------------------------

    def _check_stale(self):
        now = time.monotonic()
        for name, config in self._indicator_configs.items():
            last = self._last_update.get(name, now)
            error_timeout = (config.stale_error_timeout
                             if config.stale_error_timeout > 0
                             else config.stale_timeout * 3)
            age = now - last
            widget = self._indicators[name]
            if age > error_timeout:
                if widget._level != IndicatorLevel.ERROR or widget._value_text != 'no data':
                    widget.set_status(IndicatorLevel.ERROR, 'no data')
            elif age > config.stale_timeout:
                if widget._level != IndicatorLevel.WARN or widget._value_text != 'no data':
                    widget.set_status(IndicatorLevel.WARN, 'no data')

    # -- Helpers ---------------------------------------------------------------

    def _clear_indicators(self):
        for widget in self._indicators.values():
            self._layout.removeWidget(widget)
            widget.setParent(None)
            widget.deleteLater()
        self._indicators.clear()
        self._indicator_configs.clear()
        self._last_update.clear()
        self._current_cols = 0
        # Zero every column stretch previously applied to the layout;
        # otherwise reloading with fewer columns would leave stale
        # weights on cols that are no longer in use, since
        # _restretch_columns only zeroes cols derived from the (now
        # reset) memo.  Take the max of Qt's current column count and
        # our own remembered count before resetting: QGridLayout's
        # columnCount() can drop once widgets are removed, so relying
        # on it alone could under-iterate and leave stretches behind.
        max_cols = max(
            self._layout.columnCount(),
            len(self._last_column_stretches),
        )
        for c in range(max_cols):
            self._layout.setColumnStretch(c, 0)
        self._last_column_stretches = []
