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

from ..config_model import IndicatorLevel
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

    def __init__(self, warn_v=12.5, critical_v=11.5, parent=None):
        super().__init__(parent)
        self._warn_v = warn_v
        self._critical_v = critical_v
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

        readouts = QGridLayout()
        self._volt_label = QLabel('-- V')
        self._pct_label = QLabel('-- %')
        self._amp_label = QLabel('-- A')
        for lab in (self._volt_label, self._pct_label, self._amp_label):
            lab.setStyleSheet('color: #f0f0f0; font-size: 16px;')
        readouts.addWidget(self._volt_label, 0, 0)
        readouts.addWidget(self._pct_label, 0, 1)
        readouts.addWidget(self._amp_label, 0, 2)
        layout.addLayout(readouts)

        self._trend = TrendPlot()
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

    def set_thresholds(self, warn_v, critical_v):
        self._warn_v = warn_v
        self._critical_v = critical_v

    def set_battery(self, voltage=None, percentage=None, current=None,
                    stale=False):
        """Update the readouts and push voltage onto the trend.

        *percentage* is the mavros 0..1 fraction; *current* is amps.
        """
        if stale or voltage is None:
            self._volt_label.setText('-- V')
            self._set_level(IndicatorLevel.STALE)
        else:
            self._volt_label.setText(f'{voltage:.1f} V')
            self._trend.add_sample(voltage)
            self._set_level(self._level_for(voltage))

        if percentage is None:
            self._pct_label.setText('-- %')
        else:
            self._pct_label.setText(f'{percentage * 100.0:.0f} %')

        if current is None:
            self._amp_label.setText('-- A')
        else:
            self._amp_label.setText(f'{current:.1f} A')

    def _set_level(self, level):
        color = _LAMP_COLORS[level]
        self._lamp.setStyleSheet(
            f'background-color: {color.name()}; border-radius: 7px;')
