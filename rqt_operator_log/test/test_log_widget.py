"""Tests for the operator log entry box (issue #66).

Covers the multi-line ``QPlainTextEdit`` entry box and its Ctrl-Enter submit
behavior: bare Enter must insert a newline, Ctrl-Enter must submit, and the
submit path must strip whitespace, ignore empty entries, and clear the box.

These exercise Qt widgets, so they need a ``QApplication``; the offscreen QPA
platform (also forced by CI) lets them run without a display.
"""

import os

import pytest

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')

from python_qt_binding.QtCore import Qt
from python_qt_binding.QtGui import QTextCursor
from python_qt_binding.QtTest import QTest
from python_qt_binding.QtWidgets import QApplication

from rqt_operator_log.log_entry import EntryType, LogEntry
from rqt_operator_log.log_widget import _EntryTextEdit, _escape_multiline, LogWidget


@pytest.fixture(scope='module')
def qapp():
    app = QApplication.instance() or QApplication([])
    yield app


class TestEntryTextEdit:
    def test_ctrl_enter_submits(self, qapp):
        edit = _EntryTextEdit()
        received = []
        edit.submit_requested.connect(lambda: received.append(True))

        edit.setPlainText('hello')
        QTest.keyClick(edit, Qt.Key_Return, Qt.ControlModifier)

        assert received == [True]
        # Submit does not itself mutate the text.
        assert edit.toPlainText() == 'hello'

    def test_ctrl_keypad_enter_submits(self, qapp):
        edit = _EntryTextEdit()
        received = []
        edit.submit_requested.connect(lambda: received.append(True))

        edit.setPlainText('hello')
        QTest.keyClick(edit, Qt.Key_Enter, Qt.ControlModifier)

        assert received == [True]

    def test_bare_enter_inserts_newline_without_submitting(self, qapp):
        edit = _EntryTextEdit()
        received = []
        edit.submit_requested.connect(lambda: received.append(True))

        edit.setPlainText('line1')
        edit.moveCursor(QTextCursor.End)
        QTest.keyClick(edit, Qt.Key_Return, Qt.NoModifier)

        assert received == []
        assert edit.toPlainText() == 'line1\n'


class TestLogWidgetSubmit:
    def test_submit_emits_stripped_text_and_clears(self, qapp):
        widget = LogWidget(author='Operator')
        emitted = []
        widget.entry_submitted.connect(emitted.append)

        widget._entry_edit.setPlainText('  a note  ')
        widget._on_submit()

        assert emitted == ['a note']
        assert widget._entry_edit.toPlainText() == ''

    def test_submit_preserves_multiline(self, qapp):
        widget = LogWidget()
        emitted = []
        widget.entry_submitted.connect(emitted.append)

        widget._entry_edit.setPlainText('line one\nline two')
        widget._on_submit()

        assert emitted == ['line one\nline two']

    def test_whitespace_only_is_ignored(self, qapp):
        widget = LogWidget()
        emitted = []
        widget.entry_submitted.connect(emitted.append)

        widget._entry_edit.setPlainText('   \n\t ')
        widget._on_submit()

        assert emitted == []
        # Nothing emitted, but the box is left as-is for the operator to edit.
        assert widget._entry_edit.toPlainText() == '   \n\t '


class TestMultilineDisplay:
    """Multi-line notes must keep their line breaks in the timeline view.

    The timeline is an HTML widget; without converting newlines to ``<br>``
    a multi-line note collapses onto one line on screen (issue #66).
    """

    def test_escape_multiline_converts_newlines(self):
        assert _escape_multiline('a\nb') == 'a<br>b'

    def test_escape_multiline_still_escapes_html(self):
        assert _escape_multiline('x & <y>\n<z>') == 'x &amp; &lt;y&gt;<br>&lt;z&gt;'

    def test_timeline_has_minimum_height(self, qapp):
        # A floor keeps history visible even when the splitter is dragged down.
        widget = LogWidget()
        assert widget._timeline.minimumHeight() > 0

    def test_timeline_preserves_multiline_note(self, qapp):
        widget = LogWidget()
        entry = LogEntry(
            timestamp_ns=1_000_000_000,
            entry_type=EntryType.OPERATOR_TEXT,
            text='first line\nsecond line',
            author='Operator',
        )
        widget.append_entry(entry)

        plain = widget._timeline.toPlainText()
        assert 'first line' in plain
        assert 'second line' in plain
        # The two lines must not be flattened together with a space.
        assert 'first line second line' not in plain
