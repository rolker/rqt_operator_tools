"""Tests for TopicRecorder: subscription discovery and bag recording."""

import shutil
import tempfile
import time

import pytest
import rclpy
from rclpy.executors import SingleThreadedExecutor
from std_msgs.msg import String

from rqt_operator_log.bag_manager import BagManager
from rqt_operator_log.log_entry import EntryType, LogEntry
from rqt_operator_log.topic_recorder import TopicRecorder


@pytest.fixture(scope='module')
def ros_context():
    rclpy.init()
    yield
    rclpy.shutdown()


@pytest.fixture()
def nodes(ros_context):
    recorder_node = rclpy.create_node('test_recorder')
    pub_node = rclpy.create_node('test_publisher')
    yield recorder_node, pub_node
    recorder_node.destroy_node()
    pub_node.destroy_node()


@pytest.fixture()
def tmp_dir():
    d = tempfile.mkdtemp()
    yield d
    shutil.rmtree(d)


class TestTopicRecorder:
    def test_records_published_messages(self, nodes, tmp_dir):
        recorder_node, pub_node = nodes
        executor = SingleThreadedExecutor()
        executor.add_node(recorder_node)
        executor.add_node(pub_node)

        bag_manager = BagManager(recorder_node, base_dir=tmp_dir)
        # Force open the writer so register_topic has a writer to work with
        bag_manager.write_entry(LogEntry(
            timestamp_ns=time.time_ns(),
            entry_type=EntryType.OPERATOR_TEXT,
            author='setup',
            text='init',
        ))

        pub = pub_node.create_publisher(String, '/test/record_topic', 10)

        recorder = TopicRecorder(recorder_node, bag_manager, ['/test/record_topic'])

        # Spin to let discovery and subscriptions settle
        for _ in range(20):
            executor.spin_once(timeout_sec=0.05)

        # Publish messages
        for i in range(3):
            msg = String()
            msg.data = f'message {i}'
            pub.publish(msg)
            # Spin to deliver
            for _ in range(5):
                executor.spin_once(timeout_sec=0.05)

        recorder.shutdown()
        bag_manager.close()

        # Verify messages were recorded by reading the bag
        from rosbag2_py import SequentialReader, StorageOptions, ConverterOptions
        from pathlib import Path
        from datetime import datetime, timezone

        day = datetime.now(tz=timezone.utc).strftime('%Y%m%d')
        day_dir = f'{tmp_dir}/{day}'
        segments = sorted(Path(day_dir).glob('operator_log_*'))
        assert len(segments) >= 1

        recorded_topics = set()
        test_messages = []
        for segment in segments:
            reader = SequentialReader()
            reader.open(
                StorageOptions(uri=str(segment), storage_id='mcap'),
                ConverterOptions('cdr', 'cdr'),
            )
            while reader.has_next():
                topic, data, ts = reader.read_next()
                recorded_topics.add(topic)
                if topic == '/test/record_topic':
                    msg = rclpy.serialization.deserialize_message(data, String)
                    test_messages.append(msg.data)

        assert '/test/record_topic' in recorded_topics
        assert len(test_messages) == 3
        assert test_messages == ['message 0', 'message 1', 'message 2']

    def test_ignores_missing_topics(self, nodes, tmp_dir):
        recorder_node, _ = nodes
        bag_manager = BagManager(recorder_node, base_dir=tmp_dir)
        recorder = TopicRecorder(
            recorder_node, bag_manager, ['/nonexistent/topic']
        )
        # Should not crash, just stay pending
        assert '/nonexistent/topic' in recorder._pending
        recorder.shutdown()
        bag_manager.close()
