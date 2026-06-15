"""Main widget for the operator logbook — timeline display and text entry."""

from datetime import datetime, timezone

from python_qt_binding.QtCore import Qt, Signal
from python_qt_binding.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPlainTextEdit,
    QPushButton,
    QSplitter,
    QTextBrowser,
    QVBoxLayout,
    QWidget,
)

from .log_entry import EntryType, LogEntry


class _EntryTextEdit(QPlainTextEdit):
    """Multi-line log entry box that submits on Ctrl-Enter.

    A bare Enter inserts a newline (default ``QPlainTextEdit`` behavior); only
    Ctrl+Return / Ctrl+Enter (including the keypad Enter) emits
    ``submit_requested``.
    """

    submit_requested = Signal()

    def keyPressEvent(self, event):
        if event.key() in (Qt.Key_Return, Qt.Key_Enter) and (
            event.modifiers() & Qt.ControlModifier
        ):
            self.submit_requested.emit()
            event.accept()
            return
        super().keyPressEvent(event)


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

        # Splitter: timeline on top, entry box below, with a draggable divider
        # so the operator can trade timeline height for a taller entry box.
        splitter = QSplitter(Qt.Vertical)

        # Timeline
        self._timeline = QTextBrowser()
        self._timeline.setOpenExternalLinks(False)
        self._timeline.setReadOnly(True)
        # Keep a few lines of history visible: the splitter is non-collapsible,
        # but without a floor the divider could still be dragged down until the
        # timeline is only a sliver. A modest minimum honors "always see
        # history" without fighting small windows.
        self._timeline.setMinimumHeight(60)
        splitter.addWidget(self._timeline)

        # Entry pane: multi-line box with the Submit button below, bottom-right.
        entry_pane = QWidget()
        entry_pane_layout = QVBoxLayout(entry_pane)
        entry_pane_layout.setContentsMargins(0, 0, 0, 0)

        self._entry_edit = _EntryTextEdit()
        self._entry_edit.setPlaceholderText(
            'Type a log entry... (Ctrl-Enter to submit, Enter for newline)'
        )
        self._entry_edit.submit_requested.connect(self._on_submit)
        # Default to ~3 lines tall so multi-line notes feel invited; the
        # splitter lets the operator grow it further.
        line_h = self._entry_edit.fontMetrics().lineSpacing()
        self._entry_edit.setMinimumHeight(3 * line_h + 12)
        entry_pane_layout.addWidget(self._entry_edit)

        button_layout = QHBoxLayout()
        button_layout.addStretch()
        self._submit_btn = QPushButton('Submit')
        self._submit_btn.setToolTip('Submit log entry (Ctrl-Enter)')
        self._submit_btn.clicked.connect(self._on_submit)
        button_layout.addWidget(self._submit_btn)
        entry_pane_layout.addLayout(button_layout)

        splitter.addWidget(entry_pane)

        # Timeline takes the stretch; neither pane may collapse to zero so the
        # operator can always see history and reach the entry box.
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 0)
        splitter.setCollapsible(0, False)
        splitter.setCollapsible(1, False)
        splitter.setSizes([400, 120])
        layout.addWidget(splitter, stretch=1)

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
        text = self._entry_edit.toPlainText().strip()
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
                f'{author_html} {_escape_multiline(entry.text)}'
            )
        else:
            html = (
                f'<span style="color: gray;">[{time_str}]</span> '
                f'<i style="color: #666;">{_escape_multiline(entry.text)}</i>'
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


def _escape_multiline(text: str) -> str:
    """Escape HTML and render newlines as ``<br>`` line breaks.

    The timeline is an HTML view, which collapses literal newlines to a
    single space. Multi-line operator notes (issue #66) must keep their
    line breaks on screen, so convert newlines after escaping.
    """
    return _escape(text).replace('\n', '<br>')
