"""Settings dialog for the survey-coverage scoreboard."""

from python_qt_binding.QtCore import Qt
from python_qt_binding.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QDoubleSpinBox,
    QFormLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QVBoxLayout,
)

from .score_model import (
    FONT_SCALE_MAX,
    FONT_SCALE_MIN,
    VIEW_DETAIL,
    VIEW_GLANCE,
    CoverageConfig,
)


class ConfigDialog(QDialog):
    """Dialog for the view, areas, font scale, combined score and aging window."""

    def __init__(self, config: CoverageConfig, seen_area_ids, parent=None):
        """Fill the form from ``config``; ``seen_area_ids`` feed the area list."""
        super().__init__(parent)
        self.setWindowTitle('Survey Coverage Settings')
        self._base = config
        layout = QVBoxLayout(self)
        form = QFormLayout()
        layout.addLayout(form)

        self._view = QComboBox()
        self._view.addItem('Glance (two big numbers)', VIEW_GLANCE)
        self._view.addItem('Detail (per-source credit)', VIEW_DETAIL)
        self._view.setCurrentIndex(self._view.findData(config.view))
        form.addRow('View', self._view)

        self._topic = QLineEdit(config.topic)
        form.addRow('Topic', self._topic)

        self._areas = QListWidget()
        self._areas.setMaximumHeight(110)
        ids = sorted(set(seen_area_ids) | set(config.selected_areas))
        for area_id in ids:
            self._add_area_item(area_id, area_id in config.selected_areas)
        form.addRow('Areas (none ticked = all)', self._areas)
        self._extra_area = QLineEdit()
        self._extra_area.setPlaceholderText(
            'add an area id not received yet, then OK')
        form.addRow('Add area id', self._extra_area)

        self._font = QDoubleSpinBox()
        self._font.setRange(FONT_SCALE_MIN, FONT_SCALE_MAX)
        self._font.setSingleStep(0.25)
        self._font.setValue(config.font_scale)
        form.addRow('Font scale', self._font)

        self._combined = QCheckBox('Show a combined score')
        self._combined.setChecked(config.show_combined)
        form.addRow(self._combined)
        self._w_spec = self._weight(config.weight_spec)
        form.addRow('Weight: covered to standard', self._w_spec)
        self._w_cov = self._weight(config.weight_covered)
        form.addRow('Weight: covered below standard', self._w_cov)

        self._aging = QDoubleSpinBox()
        self._aging.setRange(0.0, 86400.0)
        self._aging.setDecimals(1)
        self._aging.setSuffix(' s')
        self._aging.setValue(config.aging_timeout)
        form.addRow('Mark "no new assessment" after (0 = never)', self._aging)
        note = QLabel('The scorer publishes only when something changed, so a quiet '
                      'interval is normal for an idle or finished survey.')
        note.setWordWrap(True)
        layout.addWidget(note)

        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    @staticmethod
    def _weight(value):
        box = QDoubleSpinBox()
        box.setRange(0.0, 1.0)
        box.setSingleStep(0.05)
        box.setDecimals(2)
        box.setValue(value)
        return box

    def _add_area_item(self, area_id, checked):
        item = QListWidgetItem(area_id)
        item.setFlags(item.flags() | Qt.ItemIsUserCheckable)
        item.setCheckState(Qt.Checked if checked else Qt.Unchecked)
        self._areas.addItem(item)

    def get_config(self) -> CoverageConfig:
        """Return the edited settings as a normalised configuration."""
        selected = []
        for i in range(self._areas.count()):
            item = self._areas.item(i)
            if item.checkState() == Qt.Checked:
                selected.append(item.text())
        extra = self._extra_area.text().strip()
        if extra and extra not in selected:
            selected.append(extra)
        return CoverageConfig(
            topic=self._topic.text(),
            view=self._view.currentData(),
            selected_areas=selected,
            font_scale=self._font.value(),
            show_combined=self._combined.isChecked(),
            weight_spec=self._w_spec.value(),
            weight_covered=self._w_cov.value(),
            aging_timeout=self._aging.value(),
        ).normalized()
