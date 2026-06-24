"""Battery readout: voltage / percentage / current with a warn/err lamp and
an embedded voltage TrendPlot.

Levels come from the configured warn/critical voltages; the lamp turns amber
below warn and red below critical (or when the data is stale).
"""

from python_qt_binding.QtCore import Qt
from python_qt_binding.QtWidgets import (
    QGridLayout,
    QLabel,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

from ..config_model import IndicatorLevel, is_valid_measurement
from ..trend_plot import TrendPlot
from . import ERROR_COLOR, OK_COLOR, WARN_COLOR

_LAMP_COLORS = {
    IndicatorLevel.OK: OK_COLOR,
    IndicatorLevel.WARN: WARN_COLOR,
    IndicatorLevel.ERROR: ERROR_COLOR,
    IndicatorLevel.STALE: ERROR_COLOR,
}


class BatteryGauge(QWidget):
    """Numeric battery readout with a status lamp and a voltage trend."""

    # Fixed trend scale; zones within it track the configured thresholds.
    _SCALE_MIN = 21.0
    _SCALE_MAX = 29.0

    # EMA smoothing for the zone-split marker so it does not jump with the
    # instantaneous voltage sag under load.
    _MARKER_EMA_ALPHA = 0.08

    def __init__(self, warn_v=23.5, critical_v=22.0, parent=None):
        super().__init__(parent)
        self._warn_v = warn_v
        self._critical_v = critical_v
        self._marker_ema = None
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(6, 4, 6, 4)
        layout.setSpacing(4)

        header = QGridLayout()
        self._lamp = QLabel()
        self._lamp.setFixedSize(14, 14)
        self._title = QLabel('Battery')
        self._title.setStyleSheet('color: #c8c8c8; font-weight: bold;')
        header.addWidget(self._lamp, 0, 0)
        header.addWidget(self._title, 0, 1, Qt.AlignLeft)
        header.setColumnStretch(2, 1)
        layout.addLayout(header)

        # Voltage only — percentage and current carry no useful data on this
        # platform's BMS, so they are intentionally omitted.
        self._volt_label = QLabel('-- V')
        self._volt_label.setStyleSheet(
            'color: #f0f0f0; font-size: 20px; font-weight: bold;')
        self._volt_label.setAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        header.addWidget(self._volt_label, 0, 2, Qt.AlignRight)

        # Battery voltage sags under load and recovers at rest; bin to 30 s and
        # plot the per-bin MAX (resting voltage) so the line tracks pack health,
        # while the min/max band still shows the sag.  240 bins * 30 s = 2 h.
        # Fixed 21..29 V scale with red/yellow/green zones derived from the
        # configured critical/warn thresholds (no numeric axis labels).
        self._trend = TrendPlot(capacity=240, bin_seconds=30.0, agg='max',
                                y_min=self._SCALE_MIN, y_max=self._SCALE_MAX,
                                zones=self._zones())
        layout.addWidget(self._trend, 1)

        self._set_level(IndicatorLevel.STALE)

    def _level_for(self, voltage):
        if voltage is None:
            return IndicatorLevel.STALE
        if voltage < self._critical_v:
            return IndicatorLevel.ERROR
        if voltage < self._warn_v:
            return IndicatorLevel.WARN
        return IndicatorLevel.OK

    def _zones(self):
        """Red/yellow/green bands derived from the critical/warn thresholds:
        red below critical, yellow critical..warn, green above warn — clamped
        to the fixed trend scale."""
        crit = max(self._SCALE_MIN, min(self._critical_v, self._SCALE_MAX))
        warn = max(crit, min(self._warn_v, self._SCALE_MAX))
        return [
            (self._SCALE_MIN, crit, ERROR_COLOR),   # red:    < critical
            (crit, warn, WARN_COLOR),               # yellow: critical..warn
            (warn, self._SCALE_MAX, OK_COLOR),       # green:  > warn
        ]

    def set_thresholds(self, warn_v, critical_v):
        self._warn_v = warn_v
        self._critical_v = critical_v
        # Keep the trend's health zones in sync with the new thresholds.
        self._trend.set_zones(self._zones())

    def set_battery(self, voltage=None, percentage=None, current=None,
                    stale=False):
        """Update the voltage readout and trend.

        *percentage* and *current* are accepted for call-site compatibility but
        not displayed (no useful data from this platform's BMS).
        """
        if stale or not is_valid_measurement(voltage):
            self._volt_label.setText('-- V')
            self._trend.set_marker(None)
            self._marker_ema = None
            self._set_level(IndicatorLevel.STALE)
        else:
            self._volt_label.setText(f'{voltage:.1f} V')
            self._trend.add_sample(voltage)
            # The current voltage splits the trend's zone brightness (bright
            # below, dim above).  Smooth it with an EMA so the split line does
            # not jump with the instantaneous load sag.
            a = self._MARKER_EMA_ALPHA
            self._marker_ema = (voltage if self._marker_ema is None
                                else a * voltage + (1.0 - a) * self._marker_ema)
            self._trend.set_marker(self._marker_ema)
            self._set_level(self._level_for(voltage))

    def _set_level(self, level):
        color = _LAMP_COLORS[level]
        self._lamp.setStyleSheet(
            f'background-color: {color.name()}; border-radius: 7px;')
