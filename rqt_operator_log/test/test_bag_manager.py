"""Tests for BagManager: write, recover, segment rotation, parse."""

import shutil
import tempfile
import time

import pytest
import rclpy

from rqt_operator_log.bag_manager import BagManager, make_log_topic
from rqt_operator_log.log_entry import EntryType, LogEntry


@pytest.fixture(scope='module')
def ros_node():
    rclpy.init()
    node = rclpy.create_node('test_bag_manager')
    yield node
    node.destroy_node()
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
    def test_json_operator_text(self):
        entry = BagManager._parse_entry(
            '{"type": "operator_text", "author": "alice", "text": "hello world"}',
            1000,
        )
        assert entry.entry_type == EntryType.OPERATOR_TEXT
        assert entry.author == 'alice'
        assert entry.text == 'hello world'
        assert entry.timestamp_ns == 1000

    def test_json_system_event(self):
        entry = BagManager._parse_entry(
            '{"type": "system_event", "author": "", "text": "mode changed"}',
            2000,
        )
        assert entry.entry_type == EntryType.SYSTEM_EVENT
        assert entry.author == ''
        assert entry.text == 'mode changed'

    def test_json_special_chars_in_author(self):
        entry = BagManager._parse_entry(
            '{"type": "operator_text", "author": "user>name<test", "text": "ok"}',
            3000,
        )
        assert entry.author == 'user>name<test'
        assert entry.text == 'ok'

    def test_plain_text_fallback(self):
        entry = BagManager._parse_entry('just some text', 3000)
        assert entry.entry_type == EntryType.OPERATOR_TEXT
        assert entry.text == 'just some text'
        assert entry.author == ''

    def test_invalid_json_fallback(self):
        entry = BagManager._parse_entry('{"type": "bogus"}', 4000)
        assert entry.entry_type == EntryType.OPERATOR_TEXT
        assert entry.text == '{"type": "bogus"}'


class TestMakeLogTopic:
    def test_empty_namespace(self):
        assert make_log_topic('') == 'log/text'

    def test_simple_namespace(self):
        assert make_log_topic('operator') == '/operator/log/text'

    def test_namespace_with_slash(self):
        assert make_log_topic('/bizzy/') == '/bizzy/log/text'

    def test_nested_namespace(self):
        assert make_log_topic('fleet/boat1') == '/fleet/boat1/log/text'


class TestNamespacedBag:
    def test_namespaced_write_and_recover(self, ros_node, tmp_dir):
        bm = BagManager(ros_node, base_dir=tmp_dir, log_namespace='operator')
        assert bm.log_topic == '/operator/log/text'
        bm.write_entry(LogEntry(
            timestamp_ns=time.time_ns(),
            entry_type=EntryType.OPERATOR_TEXT,
            author='op',
            text='namespaced entry',
        ))
        bm.close()

        # Recovery should find entries regardless of namespace
        bm2 = BagManager(ros_node, base_dir=tmp_dir, log_namespace='different')
        recovered = bm2.recover_entries()
        bm2.close()
        assert len(recovered) == 1
        assert recovered[0].text == 'namespaced entry'
