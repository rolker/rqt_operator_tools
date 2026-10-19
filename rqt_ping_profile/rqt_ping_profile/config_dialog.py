"""Settings dialog for the ping-profile view."""

from python_qt_binding.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QDoubleSpinBox,
    QFormLayout,
    QLabel,
    QLineEdit,
    QSpinBox,
    QVBoxLayout,
)

from .profile_model import (
    DEPTH_AUTO,
    DEPTH_FIXED,
    HISTORY_MAX,
    ProfileConfig,
)
from .platform_lookup import normalize_hex_color


class ConfigDialog(QDialog):
    """Dialog for the topic, title, colour override, depth axis, staleness, history and flagged beams."""

    def __init__(self, config: ProfileConfig, parent=None):
        """Fill the form from ``config``."""
        super().__init__(parent)
        self.setWindowTitle('Ping Profile Settings')
        layout = QVBoxLayout(self)
        form = QFormLayout()
        layout.addLayout(form)

        self._topic = QLineEdit(config.topic)
        form.addRow('Topic', self._topic)
        self._title = QLineEdit(config.title)
        self._title.setPlaceholderText('empty = show the topic')
        form.addRow('Title', self._title)

        self._color = QLineEdit(config.color)
        self._color.setPlaceholderText('empty = the platform colour from /marine/platforms')
        form.addRow('Colour override (#rrggbb)', self._color)

        self._mode = QComboBox()
        self._mode.addItem('Fixed range', DEPTH_FIXED)
        self._mode.addItem('Auto-scale to the ping', DEPTH_AUTO)
        self._mode.setCurrentIndex(self._mode.findData(config.depth_mode))
        self._mode.currentIndexChanged.connect(self._mode_changed)
        form.addRow('Depth axis', self._mode)
        self._dmin = self._depth_box(config.depth_min)
        form.addRow('Fixed range: top depth', self._dmin)
        self._dmax = self._depth_box(config.depth_max)
        form.addRow('Fixed range: bottom depth', self._dmax)

        self._stale = QDoubleSpinBox()
        self._stale.setRange(0.0, 86400.0)
        self._stale.setDecimals(1)
        self._stale.setSuffix(' s')
        self._stale.setValue(config.stale_timeout)
        form.addRow('Mark "no ping" after (0 = never)', self._stale)

        self._history = QSpinBox()
        self._history.setRange(0, HISTORY_MAX)
        self._history.setValue(config.history)
        form.addRow('Earlier pings faded behind', self._history)

        self._flagged = QCheckBox('Show flagged (not OK) beams as dim dots')
        self._flagged.setChecked(config.show_flagged)
        form.addRow(self._flagged)

        note = QLabel('Depths are metres below the transducer. In auto-scale the '
                      'fixed range is ignored.')
        note.setWordWrap(True)
        layout.addWidget(note)

        self._error = QLabel('')
        self._error.setStyleSheet('color: #c03030')
        layout.addWidget(self._error)

        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)
        self._mode_changed()

    @staticmethod
    def _depth_box(value):
        box = QDoubleSpinBox()
        box.setRange(-1000.0, 20000.0)
        box.setDecimals(1)
        box.setSuffix(' m')
        box.setValue(value)
        return box

    def _mode_changed(self, _index=None):
        fixed = self._mode.currentData() == DEPTH_FIXED
        self._dmin.setEnabled(fixed)
        self._dmax.setEnabled(fixed)

    def accept(self):
        """Refuse a colour override that is not ``#rrggbb`` instead of silently dropping it."""
        text = self._color.text().strip()
        if text and not normalize_hex_color(text):
            self._error.setText(f'"{text}" is not a colour: use #rrggbb, or leave it empty')
            return
        super().accept()

    def get_config(self) -> ProfileConfig:
        """Return the edited settings as a normalised configuration."""
        return ProfileConfig(
            topic=self._topic.text(),
            title=self._title.text(),
            depth_mode=self._mode.currentData(),
            depth_min=self._dmin.value(),
            depth_max=self._dmax.value(),
            stale_timeout=self._stale.value(),
            history=self._history.value(),
            show_flagged=self._flagged.isChecked(),
            color=self._color.text(),
        ).normalized()
