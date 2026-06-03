"""Main widget for the operator logbook — timeline display and text entry."""

from datetime import datetime, timezone

from python_qt_binding.QtCore import Signal
from python_qt_binding.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QTextBrowser,
    QVBoxLayout,
    QWidget,
)

from .log_entry import EntryType, LogEntry


class LogWidget(QWidget):
    """Operator log timeline with text entry."""

    entry_submitted = Signal(str)  # emits the text

    def __init__(self, author: str = '', parent=None):
        super().__init__(parent)
        self._author = author
        self._setup_ui()

    def _setup_ui(self):
        layout = QVBoxLayout(self)

        # Author bar
        author_layout = QHBoxLayout()
        author_layout.addWidget(QLabel('Author:'))
        self._author_edit = QLineEdit(self._author)
        self._author_edit.setMaximumWidth(200)
        self._author_edit.textChanged.connect(self._on_author_changed)
        author_layout.addWidget(self._author_edit)
        author_layout.addStretch()
        layout.addLayout(author_layout)

        # Timeline
        self._timeline = QTextBrowser()
        self._timeline.setOpenExternalLinks(False)
        self._timeline.setReadOnly(True)
        layout.addWidget(self._timeline, stretch=1)

        # Entry bar
        entry_layout = QHBoxLayout()
        self._entry_edit = QLineEdit()
        self._entry_edit.setPlaceholderText('Type a log entry...')
        self._entry_edit.returnPressed.connect(self._on_submit)
        entry_layout.addWidget(self._entry_edit, stretch=1)

        self._submit_btn = QPushButton('Submit')
        self._submit_btn.clicked.connect(self._on_submit)
        entry_layout.addWidget(self._submit_btn)
        layout.addLayout(entry_layout)

        # Keep keyboard focus on the entry box so the operator can type a log
        # entry without first clicking it: route the widget's focus to the
        # entry field, and re-grab focus on show / after each submit.
        self.setFocusProxy(self._entry_edit)

    def showEvent(self, event):
        super().showEvent(event)
        self._entry_edit.setFocus()

    def _on_author_changed(self, text):
        self._author = text

    def _on_submit(self):
        text = self._entry_edit.text().strip()
        if not text:
            return
        self._entry_edit.clear()
        self.entry_submitted.emit(text)
        self._entry_edit.setFocus()

    @property
    def author(self) -> str:
        return self._author

    @author.setter
    def author(self, value: str):
        self._author = value
        self._author_edit.setText(value)

    def append_entry(self, entry: LogEntry):
        """Add an entry to the timeline display."""
        dt = datetime.fromtimestamp(entry.timestamp_ns / 1e9, tz=timezone.utc)
        local_dt = dt.astimezone()
        time_str = local_dt.strftime('%H:%M:%S')

        if entry.entry_type == EntryType.OPERATOR_TEXT:
            author_html = (
                f' <b>{_escape(entry.author)}</b>' if entry.author else ''
            )
            html = (
                f'<span style="color: gray;">[{time_str}]</span>'
                f'{author_html} {_escape(entry.text)}'
            )
        else:
            html = (
                f'<span style="color: gray;">[{time_str}]</span> '
                f'<i style="color: #666;">{_escape(entry.text)}</i>'
            )

        self._timeline.append(html)

    def clear_timeline(self):
        self._timeline.clear()


def _escape(text: str) -> str:
    """Escape HTML special characters."""
    return (
        text.replace('&', '&amp;')
        .replace('<', '&lt;')
        .replace('>', '&gt;')
    )
