"""Settings dialog for the annunciator panel."""

from python_qt_binding.QtCore import Qt
from python_qt_binding.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QDoubleSpinBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from .config_model import (
    AnnunciatorConfig,
    IndicatorConfig,
    MatchMode,
    validate_expression,
)


class _IndicatorEditor(QWidget):
    """Editor for a single indicator's configuration."""

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QFormLayout(self)

        self._name_edit = QLineEdit()
        layout.addRow('Name:', self._name_edit)

        self._source_combo = QComboBox()
        self._source_combo.addItems(['topic', 'diagnostics'])
        self._source_combo.currentTextChanged.connect(self._on_source_changed)
        layout.addRow('Source:', self._source_combo)

        # -- Topic fields --
        self._topic_group = QGroupBox('Topic Settings')
        topic_layout = QFormLayout(self._topic_group)

        self._topic_edit = QLineEdit()
        topic_layout.addRow('Topic:', self._topic_edit)

        self._msg_type_edit = QLineEdit()
        self._msg_type_edit.setPlaceholderText('e.g. std_msgs/msg/Float64')
        topic_layout.addRow('Message type:', self._msg_type_edit)

        self._value_field_edit = QLineEdit()
        self._value_field_edit.setPlaceholderText('e.g. data')
        self._value_field_edit.setText('data')
        topic_layout.addRow('Value field:', self._value_field_edit)

        self._format_edit = QLineEdit()
        self._format_edit.setPlaceholderText('e.g. {:.1f}V')
        self._format_edit.setText('{}')
        topic_layout.addRow('Format:', self._format_edit)

        # Threshold expressions with validation.
        self._thresh_warn_edit = QLineEdit()
        self._thresh_warn_edit.setPlaceholderText('e.g. value < 12.5')
        self._thresh_warn_edit.textChanged.connect(
            lambda text: self._validate_threshold(self._thresh_warn_edit, self._warn_status))
        self._warn_status = QLabel()
        warn_row = QHBoxLayout()
        warn_row.addWidget(self._thresh_warn_edit)
        warn_row.addWidget(self._warn_status)
        topic_layout.addRow('Warn if:', warn_row)

        self._thresh_error_edit = QLineEdit()
        self._thresh_error_edit.setPlaceholderText('e.g. value < 11.5')
        self._thresh_error_edit.textChanged.connect(
            lambda text: self._validate_threshold(self._thresh_error_edit, self._error_status))
        self._error_status = QLabel()
        error_row = QHBoxLayout()
        error_row.addWidget(self._thresh_error_edit)
        error_row.addWidget(self._error_status)
        topic_layout.addRow('Error if:', error_row)

        layout.addRow(self._topic_group)

        # -- Diagnostics fields --
        self._diag_group = QGroupBox('Diagnostics Settings')
        diag_layout = QFormLayout(self._diag_group)

        self._diag_name_edit = QLineEdit()
        self._diag_name_edit.setPlaceholderText('e.g. NTP offset')
        diag_layout.addRow('Diagnostic name:', self._diag_name_edit)

        self._match_mode_combo = QComboBox()
        self._match_mode_combo.addItems(['substring', 'exact', 'regex'])
        diag_layout.addRow('Match mode:', self._match_mode_combo)

        self._diag_format_edit = QLineEdit()
        self._diag_format_edit.setPlaceholderText('e.g. {:.0f}ms')
        self._diag_format_edit.setText('{}')
        diag_layout.addRow('Format:', self._diag_format_edit)

        # Optional value thresholds on a selected KeyValue. When set, the row
        # colors by the threshold result combined with the diagnostic's own
        # level instead of by the level alone.
        self._diag_value_key_edit = QLineEdit()
        self._diag_value_key_edit.setPlaceholderText('e.g. Voltage (KeyValue key)')
        diag_layout.addRow('Value key:', self._diag_value_key_edit)

        self._diag_thresh_warn_edit = QLineEdit()
        self._diag_thresh_warn_edit.setPlaceholderText('e.g. value < 23.0')
        self._diag_thresh_warn_edit.textChanged.connect(
            lambda text: self._validate_threshold(
                self._diag_thresh_warn_edit, self._diag_warn_status))
        self._diag_warn_status = QLabel()
        diag_warn_row = QHBoxLayout()
        diag_warn_row.addWidget(self._diag_thresh_warn_edit)
        diag_warn_row.addWidget(self._diag_warn_status)
        diag_layout.addRow('Warn if:', diag_warn_row)

        self._diag_thresh_error_edit = QLineEdit()
        self._diag_thresh_error_edit.setPlaceholderText('e.g. value < 21.5')
        self._diag_thresh_error_edit.textChanged.connect(
            lambda text: self._validate_threshold(
                self._diag_thresh_error_edit, self._diag_error_status))
        self._diag_error_status = QLabel()
        diag_error_row = QHBoxLayout()
        diag_error_row.addWidget(self._diag_thresh_error_edit)
        diag_error_row.addWidget(self._diag_error_status)
        diag_layout.addRow('Error if:', diag_error_row)

        layout.addRow(self._diag_group)

        # -- Common fields --
        self._stale_timeout_spin = QDoubleSpinBox()
        self._stale_timeout_spin.setRange(0.5, 300.0)
        self._stale_timeout_spin.setValue(5.0)
        self._stale_timeout_spin.setSuffix(' s')
        layout.addRow('Stale timeout:', self._stale_timeout_spin)

        self._on_source_changed(self._source_combo.currentText())

    def _on_source_changed(self, source: str):
        self._topic_group.setVisible(source == 'topic')
        self._diag_group.setVisible(source == 'diagnostics')

    def _validate_threshold(self, edit: QLineEdit, status_label: QLabel):
        text = edit.text().strip()
        if not text:
            status_label.setText('')
            return
        err = validate_expression(text)
        if err:
            status_label.setText(f'\u274c {err}')
            status_label.setStyleSheet('color: red; font-size: 10px;')
        else:
            status_label.setText('\u2705')
            status_label.setStyleSheet('color: green;')

    def load_config(self, config: IndicatorConfig):
        self._name_edit.setText(config.name)
        self._source_combo.setCurrentText(config.source)

        self._topic_edit.setText(config.topic)
        self._msg_type_edit.setText(config.msg_type)
        self._value_field_edit.setText(config.value_field)
        self._thresh_warn_edit.setText(config.threshold_warn)
        self._thresh_error_edit.setText(config.threshold_error)

        self._diag_name_edit.setText(config.diagnostic_name)
        self._match_mode_combo.setCurrentText(config.match_mode.value)
        self._diag_value_key_edit.setText(config.value_key)
        self._diag_thresh_warn_edit.setText(config.threshold_warn)
        self._diag_thresh_error_edit.setText(config.threshold_error)

        fmt = config.format
        if config.source == 'diagnostics':
            self._diag_format_edit.setText(fmt)
            self._format_edit.setText('{}')
        else:
            self._format_edit.setText(fmt)
            self._diag_format_edit.setText('{}')

        self._stale_timeout_spin.setValue(config.stale_timeout)

    def get_config(self) -> IndicatorConfig:
        source = self._source_combo.currentText()
        if source == 'diagnostics':
            fmt = self._diag_format_edit.text()
            threshold_warn = self._diag_thresh_warn_edit.text()
            threshold_error = self._diag_thresh_error_edit.text()
            value_key = self._diag_value_key_edit.text()
        else:
            fmt = self._format_edit.text()
            threshold_warn = self._thresh_warn_edit.text()
            threshold_error = self._thresh_error_edit.text()
            value_key = ''
        return IndicatorConfig(
            name=self._name_edit.text() or 'Unnamed',
            source=source,
            topic=self._topic_edit.text(),
            msg_type=self._msg_type_edit.text(),
            value_field=self._value_field_edit.text() or 'data',
            format=fmt or '{}',
            threshold_warn=threshold_warn,
            threshold_error=threshold_error,
            diagnostic_name=self._diag_name_edit.text(),
            match_mode=MatchMode(self._match_mode_combo.currentText()),
            value_key=value_key,
            stale_timeout=self._stale_timeout_spin.value(),
        )


class ConfigDialog(QDialog):
    """Dialog for configuring the annunciator panel."""

    def __init__(self, config: AnnunciatorConfig, parent=None):
        super().__init__(parent)
        self.setWindowTitle('Annunciator Settings')
        self.setMinimumSize(500, 400)

        main_layout = QVBoxLayout(self)

        # Diagnostics topic.
        topic_layout = QHBoxLayout()
        topic_layout.addWidget(QLabel('Diagnostics topic:'))
        self._diag_topic_edit = QLineEdit(config.diagnostics_topic)
        topic_layout.addWidget(self._diag_topic_edit)
        main_layout.addLayout(topic_layout)

        # Indicator list + editor.
        body_layout = QHBoxLayout()

        # Left: indicator list + add/remove buttons.
        left_layout = QVBoxLayout()
        self._list_widget = QListWidget()
        self._list_widget.currentRowChanged.connect(self._on_selection_changed)
        left_layout.addWidget(self._list_widget)

        btn_layout = QHBoxLayout()
        add_btn = QPushButton('+')
        add_btn.setFixedWidth(30)
        add_btn.clicked.connect(self._add_indicator)
        remove_btn = QPushButton('-')
        remove_btn.setFixedWidth(30)
        remove_btn.clicked.connect(self._remove_indicator)
        btn_layout.addWidget(add_btn)
        btn_layout.addWidget(remove_btn)
        btn_layout.addStretch()
        left_layout.addLayout(btn_layout)

        body_layout.addLayout(left_layout, 1)

        # Right: editor.
        self._editor = _IndicatorEditor()
        body_layout.addWidget(self._editor, 2)

        main_layout.addLayout(body_layout)

        # Import/Export buttons.
        io_layout = QHBoxLayout()
        import_btn = QPushButton('Import YAML...')
        import_btn.clicked.connect(self._import_yaml)
        export_btn = QPushButton('Export YAML...')
        export_btn.clicked.connect(self._export_yaml)
        io_layout.addWidget(import_btn)
        io_layout.addWidget(export_btn)
        io_layout.addStretch()
        main_layout.addLayout(io_layout)

        # OK / Cancel.
        button_box = QDialogButtonBox(
            QDialogButtonBox.Ok | QDialogButtonBox.Cancel
        )
        button_box.accepted.connect(self.accept)
        button_box.rejected.connect(self.reject)
        main_layout.addWidget(button_box)

        # Populate from config.
        self._configs: list[IndicatorConfig] = list(config.indicators)
        self._current_row = -1
        for c in self._configs:
            self._list_widget.addItem(c.name)
        if self._configs:
            self._list_widget.setCurrentRow(0)

    def _on_selection_changed(self, row: int):
        # Save current editor state before switching.
        if 0 <= self._current_row < len(self._configs):
            self._configs[self._current_row] = self._editor.get_config()
            # Update list item text in case name changed.
            item = self._list_widget.item(self._current_row)
            if item:
                item.setText(self._configs[self._current_row].name)
        self._current_row = row
        if 0 <= row < len(self._configs):
            self._editor.load_config(self._configs[row])

    def _add_indicator(self):
        base_name = 'New Indicator'
        existing = {c.name for c in self._configs}
        name = base_name
        counter = 2
        while name in existing:
            name = f'{base_name} {counter}'
            counter += 1
        new_config = IndicatorConfig(name=name)
        self._configs.append(new_config)
        self._list_widget.addItem(new_config.name)
        self._list_widget.setCurrentRow(len(self._configs) - 1)

    def _remove_indicator(self):
        row = self._list_widget.currentRow()
        if 0 <= row < len(self._configs):
            self._current_row = -1  # Prevent saving to deleted config.
            self._configs.pop(row)
            self._list_widget.takeItem(row)
            if self._configs:
                new_row = min(row, len(self._configs) - 1)
                self._list_widget.setCurrentRow(new_row)

    def _import_yaml(self):
        path, _ = QFileDialog.getOpenFileName(
            self, 'Import YAML', '', 'YAML files (*.yaml *.yml)'
        )
        if path:
            try:
                config = AnnunciatorConfig.from_file(path)
                self._diag_topic_edit.setText(config.diagnostics_topic)
                self._configs = list(config.indicators)
                self._current_row = -1
                self._list_widget.clear()
                for c in self._configs:
                    self._list_widget.addItem(c.name)
                if self._configs:
                    self._list_widget.setCurrentRow(0)
            except Exception as exc:
                from python_qt_binding.QtWidgets import QMessageBox
                QMessageBox.warning(self, 'Import Error', str(exc))

    def _export_yaml(self):
        path, _ = QFileDialog.getSaveFileName(
            self, 'Export YAML', '', 'YAML files (*.yaml *.yml)'
        )
        if path:
            config = self.get_config()
            config.to_file(path)

    def get_config(self) -> AnnunciatorConfig:
        """Return the configured AnnunciatorConfig."""
        # Save current editor state.
        if 0 <= self._current_row < len(self._configs):
            self._configs[self._current_row] = self._editor.get_config()
        return AnnunciatorConfig(
            diagnostics_topic=self._diag_topic_edit.text(),
            indicators=list(self._configs),
        )
