"""Command-authority banner.

A single colored bar with the resolved authority label, driven by
``resolve_authority()``.  Makes the active driver visible at all times
(human-control transparency); the panel greys commanded markers separately
when ``rc/in`` is stale under RC/MANUAL.
"""

from python_qt_binding.QtCore import QSize, Qt
from python_qt_binding.QtGui import QColor
from python_qt_binding.QtWidgets import QLabel, QSizePolicy

from .config_model import AuthorityState, AuthoritySource, resolve_authority

# Banner background per color token.
_TOKEN_COLORS = {
    'autonomy': QColor(40, 120, 60),    # green — under autonomy
    'joystick': QColor(40, 90, 150),    # blue — operator joystick
    'standby': QColor(70, 70, 70),      # grey — idle/standby
    'rc': QColor(200, 130, 0),          # amber — direct RC/transmitter
    'hold': QColor(150, 110, 0),        # dark amber — hold
    'disarmed': QColor(120, 40, 40),    # red — disarmed
    'unknown': QColor(90, 90, 110),     # blue-grey — FCU known, ROS layer unseen
}
_DEFAULT_COLOR = QColor(70, 70, 70)


class AuthorityBanner(QLabel):
    """Colored bar showing who is currently commanding the vessel."""

    _MIN_HINT = QSize(120, 24)
    _SIZE_HINT = QSize(320, 30)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAlignment(Qt.AlignCenter)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.setAutoFillBackground(True)
        self._state = AuthorityState(
            AuthoritySource.DISARMED, 'DISARMED', 'disarmed')
        self._stale = True
        self._apply()

    def minimumSizeHint(self):  # noqa: N802 (Qt API)
        return self._MIN_HINT

    def sizeHint(self):  # noqa: N802 (Qt API)
        return self._SIZE_HINT

    def set_modes(self, fcu_mode, piloting_mode, stale=False):
        """Resolve and display authority from the FCU and helm-manager modes."""
        self._state = resolve_authority(fcu_mode, piloting_mode)
        self._stale = stale
        self._apply()

    @property
    def state(self) -> AuthorityState:
        return self._state

    def _apply(self):
        color = _TOKEN_COLORS.get(self._state.color_token, _DEFAULT_COLOR)
        if self._stale:
            color = color.darker(160)
            text = f'{self._state.label}  (stale)'
        else:
            text = self._state.label
        self.setText(text)
        self.setStyleSheet(
            f'background-color: {color.name()}; color: white; '
            'font-weight: bold; font-size: 14px; padding: 3px;')
