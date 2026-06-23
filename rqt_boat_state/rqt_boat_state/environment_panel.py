"""Environment readout row: sound speed and water temperature.

Non-gauge numeric readouts (each with a TrendPlot) for the control/power/
environment scope.  Sound speed comes from ``marine_interfaces/SoundSpeed``
(NaN → "—"); water temperature from ``sensor_msgs/Temperature``.  No alarm
thresholds — the trends auto-scale — but both are stale-gated by the panel.
"""

from python_qt_binding.QtWidgets import (
    QGridLayout,
    QLabel,
    QSizePolicy,
    QWidget,
)

from .config_model import format_measurement
from .trend_plot import TrendPlot

_VALUE_STYLE = 'color: #f0f0f0; font-size: 18px; font-weight: bold;'
_TITLE_STYLE = 'color: #a0a0a0; font-size: 11px;'


class _Readout(QWidget):
    """A titled numeric readout with a trend sparkline."""

    def __init__(self, title, unit, fmt, parent=None):
        super().__init__(parent)
        self._unit = unit
        self._fmt = fmt
        layout = QGridLayout(self)
        layout.setContentsMargins(6, 2, 6, 2)
        layout.setSpacing(2)

        self._title = QLabel(title)
        self._title.setStyleSheet(_TITLE_STYLE)
        self._value = QLabel('—')
        self._value.setStyleSheet(_VALUE_STYLE)
        self._trend = TrendPlot()
        self._trend.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        layout.addWidget(self._title, 0, 0)
        layout.addWidget(self._value, 0, 1)
        layout.addWidget(self._trend, 1, 0, 1, 2)
        layout.setColumnStretch(1, 1)
        layout.setRowStretch(1, 1)

    def set_value(self, value, stale=False):
        """Update the readout; NaN/None/stale shows a dash and skips the trend."""
        if stale:
            self._value.setText('—')
            return
        self._value.setText(format_measurement(value, self._fmt + ' ' + self._unit))
        self._trend.add_sample(value)


class EnvironmentPanel(QWidget):
    """Row of environment readouts (sound speed, water temperature)."""

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QGridLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.setSpacing(6)

        self._sound_speed = _Readout('Sound speed', 'm/s', '{:.1f}')
        self._water_temp = _Readout('Water temp', '°C', '{:.1f}')
        layout.addWidget(self._sound_speed, 0, 0)
        layout.addWidget(self._water_temp, 0, 1)
        layout.setColumnStretch(0, 1)
        layout.setColumnStretch(1, 1)

    def set_sound_speed(self, value, stale=False):
        self._sound_speed.set_value(value, stale)

    def set_water_temp(self, value, stale=False):
        self._water_temp.set_value(value, stale)
