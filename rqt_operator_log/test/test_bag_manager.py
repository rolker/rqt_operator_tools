"""Tests for BagManager: write, recover, segment rotation, parse."""

import shutil
import tempfile
import time

import pytest
import rclpy

from rqt_operator_log.bag_manager import BagManager
from rqt_operator_log.log_entry import EntryType, LogEntry


@pytest.fixture(scope='module')
def ros_node():
    rclpy.init()
    node = rclpy.create_node('test_bag_manager')
    yield node
    rclpy.shutdown()


@pytest.fixture()
def tmp_dir():
    d = tempfile.mkdtemp()
    yield d
    shutil.rmtree(d)


class TestBagManager:
    def test_write_and_recover(self, ros_node, tmp_dir):
        bm = BagManager(ros_node, base_dir=tmp_dir)
        entry = LogEntry(
            timestamp_ns=time.time_ns(),
            entry_type=EntryType.OPERATOR_TEXT,
            author='tester',
            text='hello world',
        )
        bm.write_entry(entry)
        bm.close()

        bm2 = BagManager(ros_node, base_dir=tmp_dir)
        recovered = bm2.recover_entries()
        bm2.close()

        assert len(recovered) == 1
        assert recovered[0].text == 'hello world'
        assert recovered[0].author == 'tester'
        assert recovered[0].entry_type == EntryType.OPERATOR_TEXT

    def test_multiple_entries(self, ros_node, tmp_dir):
        bm = BagManager(ros_node, base_dir=tmp_dir)
        for i in range(5):
            bm.write_entry(LogEntry(
                timestamp_ns=time.time_ns(),
                entry_type=EntryType.OPERATOR_TEXT,
                author='op',
                text=f'entry {i}',
            ))
        bm.close()

        bm2 = BagManager(ros_node, base_dir=tmp_dir)
        recovered = bm2.recover_entries()
        bm2.close()

        assert len(recovered) == 5
        assert [e.text for e in recovered] == [f'entry {i}' for i in range(5)]

    def test_segment_rotation(self, ros_node, tmp_dir):
        # Session 1
        bm1 = BagManager(ros_node, base_dir=tmp_dir)
        bm1.write_entry(LogEntry(
            timestamp_ns=time.time_ns(),
            entry_type=EntryType.OPERATOR_TEXT,
            author='op',
            text='session 1',
        ))
        bm1.close()

        # Session 2 — new segment
        bm2 = BagManager(ros_node, base_dir=tmp_dir)
        bm2.write_entry(LogEntry(
            timestamp_ns=time.time_ns(),
            entry_type=EntryType.OPERATOR_TEXT,
            author='op',
            text='session 2',
        ))
        bm2.close()

        # Recovery should find both
        bm3 = BagManager(ros_node, base_dir=tmp_dir)
        recovered = bm3.recover_entries()
        bm3.close()

        assert len(recovered) == 2
        assert recovered[0].text == 'session 1'
        assert recovered[1].text == 'session 2'

    def test_system_event_roundtrip(self, ros_node, tmp_dir):
        bm = BagManager(ros_node, base_dir=tmp_dir)
        bm.write_entry(LogEntry(
            timestamp_ns=time.time_ns(),
            entry_type=EntryType.SYSTEM_EVENT,
            author='',
            text='helm mode changed to autonomous',
        ))
        bm.close()

        bm2 = BagManager(ros_node, base_dir=tmp_dir)
        recovered = bm2.recover_entries()
        bm2.close()

        assert len(recovered) == 1
        assert recovered[0].entry_type == EntryType.SYSTEM_EVENT
        assert recovered[0].text == 'helm mode changed to autonomous'

    def test_recover_empty_dir(self, ros_node, tmp_dir):
        bm = BagManager(ros_node, base_dir=tmp_dir)
        recovered = bm.recover_entries()
        bm.close()
        assert recovered == []


class TestParseEntry:
    def test_operator_text(self):
        entry = BagManager._parse_entry(
            '[operator_text] <alice> hello world', 1000
        )
        assert entry.entry_type == EntryType.OPERATOR_TEXT
        assert entry.author == 'alice'
        assert entry.text == 'hello world'
        assert entry.timestamp_ns == 1000

    def test_system_event(self):
        entry = BagManager._parse_entry(
            '[system_event] <> mode changed', 2000
        )
        assert entry.entry_type == EntryType.SYSTEM_EVENT
        assert entry.author == ''
        assert entry.text == 'mode changed'

    def test_plain_text_fallback(self):
        entry = BagManager._parse_entry('just some text', 3000)
        assert entry.entry_type == EntryType.OPERATOR_TEXT
        assert entry.text == 'just some text'
        assert entry.author == ''

    def test_malformed_brackets(self):
        entry = BagManager._parse_entry('[invalid_type] <op> text', 4000)
        # invalid_type is not a valid EntryType — should fall back
        assert entry.entry_type == EntryType.OPERATOR_TEXT
        assert entry.text == '[invalid_type] <op> text'
