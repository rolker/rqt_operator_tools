"""Core annunciator widget — manages indicators, subscriptions, and layout."""

import time

from python_qt_binding.QtCore import QTimer, Signal
from python_qt_binding.QtGui import QColor
from python_qt_binding.QtWidgets import (
    QGridLayout,
    QHBoxLayout,
    QVBoxLayout,
    QWidget,
)

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
    return IndicatorLevel.ERROR


class AnnunciatorWidget(QWidget):
    """Container widget for a set of annunciator indicators.

    Handles adaptive layout (horizontal/vertical/grid based on aspect ratio),
    ROS subscriptions, and stale detection.
    """

    # Signals for thread-safe UI updates from ROS callbacks.
    _diagnostics_received = Signal(object)
    _topic_received = Signal(str, object)  # indicator name, message

    def __init__(self, node, parent=None):
        super().__init__(parent)
        self._node = node
        self._config = AnnunciatorConfig()
        self._indicators: dict[str, IndicatorWidget] = {}
        self._indicator_configs: dict[str, IndicatorConfig] = {}
        self._last_update: dict[str, float] = {}
        self._subscriptions: dict[str, object] = {}
        self._diag_sub = None
        self._layout = None

        self.setAutoFillBackground(True)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), QColor(30, 30, 30))
        self.setPalette(palette)

        self._layout = QHBoxLayout(self)
        self._layout.setContentsMargins(4, 4, 4, 4)
        self._layout.setSpacing(4)

        # Wire signals for thread-safe updates.
        self._diagnostics_received.connect(self._handle_diagnostics)
        self._topic_received.connect(self._handle_topic_message)

        # Stale detection timer (1 Hz).
        self._stale_timer = QTimer(self)
        self._stale_timer.timeout.connect(self._check_stale)
        self._stale_timer.start(1000)

    def load_config(self, config: AnnunciatorConfig):
        """Apply a new configuration, rebuilding all indicators and subscriptions."""
        self._teardown_subscriptions()
        self._clear_indicators()
        self._config = config

        for ind_config in config.indicators:
            widget = IndicatorWidget(ind_config.name, self)
            self._indicators[ind_config.name] = widget
            self._indicator_configs[ind_config.name] = ind_config
            self._last_update[ind_config.name] = 0.0

        self._rebuild_layout()
        self._setup_subscriptions()

    def get_config(self) -> AnnunciatorConfig:
        """Return the current configuration."""
        return self._config

    def shutdown(self):
        """Clean up subscriptions and timers."""
        self._stale_timer.stop()
        self._teardown_subscriptions()

    # -- Layout ----------------------------------------------------------------

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._rebuild_layout()

    def _rebuild_layout(self):
        """Recompute layout direction and font sizes based on current dimensions."""
        if not self._indicators:
            return

        # Remove all widgets from current layout.
        while self._layout.count():
            item = self._layout.takeAt(0)
            if item.widget():
                item.widget().setParent(None)

        # Delete old layout and create new one.
        QWidget().setLayout(self._layout)

        w = self.width()
        h = self.height()
        n = len(self._indicators)

        if n == 0:
            self._layout = QHBoxLayout(self)
            return

        aspect = w / max(h, 1)

        if aspect > 2.0:
            # Wide — horizontal strip.
            self._layout = QHBoxLayout(self)
            cell_h = h
            cell_w = w / n
        elif aspect < 0.5:
            # Tall — vertical column.
            self._layout = QVBoxLayout(self)
            cell_h = h / n
            cell_w = w
        else:
            # Grid.
            cols = max(1, int(n ** 0.5))
            rows = (n + cols - 1) // cols
            self._layout = QGridLayout(self)
            cell_h = h / max(rows, 1)
            cell_w = w / max(cols, 1)

        self._layout.setContentsMargins(4, 4, 4, 4)
        self._layout.setSpacing(4)

        # Scale fonts from cell dimensions.
        label_size = int(cell_h * 0.3)
        value_size = int(cell_h * 0.4)

        for i, widget in enumerate(self._indicators.values()):
            widget.update_font_size(label_size, value_size)
            if isinstance(self._layout, QGridLayout):
                cols = max(1, int(n ** 0.5))
                self._layout.addWidget(widget, i // cols, i % cols)
            else:
                self._layout.addWidget(widget)

        self.setLayout(self._layout)

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
            last = self._last_update.get(name, 0.0)
            if last == 0.0:
                # Never received — show stale.
                self._indicators[name].set_stale()
            elif (now - last) > config.stale_timeout:
                self._indicators[name].set_stale()

    # -- Helpers ---------------------------------------------------------------

    def _clear_indicators(self):
        for widget in self._indicators.values():
            widget.setParent(None)
            widget.deleteLater()
        self._indicators.clear()
        self._indicator_configs.clear()
        self._last_update.clear()
