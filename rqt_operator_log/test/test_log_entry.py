"""Tests for log_entry data model."""

from rqt_operator_log.log_entry import EntryType, LogEntry


class TestLogEntry:
    def test_defaults(self):
        entry = LogEntry(
            timestamp_ns=1000,
            entry_type=EntryType.OPERATOR_TEXT,
            text='hello',
        )
        assert entry.author == ''
        assert entry.source_topic == ''

    def test_all_fields(self):
        entry = LogEntry(
            timestamp_ns=123456789,
            entry_type=EntryType.SYSTEM_EVENT,
            text='mode changed',
            author='operator',
            source_topic='/helm/status',
        )
        assert entry.timestamp_ns == 123456789
        assert entry.entry_type == EntryType.SYSTEM_EVENT
        assert entry.text == 'mode changed'
        assert entry.author == 'operator'
        assert entry.source_topic == '/helm/status'

    def test_entry_type_values(self):
        assert EntryType.OPERATOR_TEXT.value == 'operator_text'
        assert EntryType.SYSTEM_EVENT.value == 'system_event'
