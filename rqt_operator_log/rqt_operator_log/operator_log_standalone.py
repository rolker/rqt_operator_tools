"""Standalone entry point for the operator logbook."""

import argparse
import collections
import json
import os
import signal
import sys
import time

import rclpy
from python_qt_binding.QtCore import QTimer
from python_qt_binding.QtWidgets import QApplication
from std_msgs.msg import String

from .bag_manager import BagManager
from .log_entry import EntryType, LogEntry
from .log_widget import LogWidget
from .topic_recorder import TopicRecorder


DEFAULT_LOG_DIR = os.path.expanduser('~/operator_logs')
ENV_LOG_DIR = 'OPERATOR_LOG_DIR'
ENV_TOPICS = 'OPERATOR_LOG_TOPICS'
ENV_LOG_NS = 'OPERATOR_LOG_NS'


def main(argv=None):
    parser = argparse.ArgumentParser(description='Operator logbook')
    parser.add_argument(
        '--log-dir',
        default=os.environ.get(ENV_LOG_DIR, DEFAULT_LOG_DIR),
        help=f'Log directory (default: ${ENV_LOG_DIR} or ~/operator_logs)',
    )
    parser.add_argument(
        '--record-topics',
        default=os.environ.get(ENV_TOPICS, ''),
        help=f'Comma-separated topics to record (default: ${ENV_TOPICS})',
    )
    parser.add_argument(
        '--log-namespace',
        default=os.environ.get(ENV_LOG_NS, ''),
        help=f'Namespace for log topic (default: ${ENV_LOG_NS})',
    )
    args, ros_args = parser.parse_known_args(argv)

    app = QApplication.instance() or QApplication(sys.argv)

    rclpy.init(args=ros_args)
    node = rclpy.create_node('operator_log')

    bag_manager = BagManager(
        node, base_dir=args.log_dir, log_namespace=args.log_namespace
    )
    log_topic = bag_manager.log_topic
    pub = node.create_publisher(String, log_topic, 10)
    recent_publishes = collections.deque(maxlen=50)

    # Set up topic recording
    topic_names = [t.strip() for t in args.record_topics.split(',') if t.strip()]
    topic_recorder = TopicRecorder(node, bag_manager, topic_names) if topic_names else None

    widget = LogWidget()
    widget.setWindowTitle('Operator Log')
    widget.resize(600, 400)

    # Recover today's entries.
    entries = bag_manager.recover_entries()
    for entry in entries:
        widget.append_entry(entry)

    def on_submit(text):
        entry = LogEntry(
            timestamp_ns=time.time_ns(),
            entry_type=EntryType.OPERATOR_TEXT,
            author=widget.author,
            text=text,
        )
        widget.append_entry(entry)
        msg = String()
        msg.data = json.dumps({
            'type': entry.entry_type.value,
            'author': entry.author,
            'text': entry.text,
        })
        recent_publishes.append(msg.data)
        pub.publish(msg)
        bag_manager.write_entry(entry)

    def on_log_received(msg):
        if msg.data in recent_publishes:
            recent_publishes.remove(msg.data)
            return
        entry = BagManager._parse_entry(msg.data, time.time_ns())
        if entry:
            widget.append_entry(entry)
            bag_manager.write_entry(entry)

    node.create_subscription(String, log_topic, on_log_received, 10)

    widget.entry_submitted.connect(on_submit)
    widget.show()

    node.get_logger().info(f'Log directory: {args.log_dir}, topic: {log_topic}')

    spin_timer = QTimer()
    spin_timer.timeout.connect(lambda: rclpy.spin_once(node, timeout_sec=0))
    spin_timer.start(33)

    signal.signal(signal.SIGINT, signal.SIG_DFL)

    exit_code = app.exec_()
    spin_timer.stop()
    if topic_recorder:
        topic_recorder.shutdown()
    bag_manager.close()
    node.destroy_node()
    rclpy.shutdown()
    sys.exit(exit_code)
