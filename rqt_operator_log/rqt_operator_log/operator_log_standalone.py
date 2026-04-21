"""Standalone entry point for the operator logbook."""

import argparse
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


DEFAULT_LOG_DIR = os.path.expanduser('~/operator_logs')
ENV_VAR = 'OPERATOR_LOG_DIR'


def main(argv=None):
    parser = argparse.ArgumentParser(description='Operator logbook')
    parser.add_argument(
        '--log-dir',
        default=os.environ.get(ENV_VAR, DEFAULT_LOG_DIR),
        help=f'Log directory (default: ${ENV_VAR} or ~/operator_logs)',
    )
    args, ros_args = parser.parse_known_args(argv)

    app = QApplication.instance() or QApplication(sys.argv)

    rclpy.init(args=ros_args)
    node = rclpy.create_node('operator_log')

    bag_manager = BagManager(node, base_dir=args.log_dir)
    pub = node.create_publisher(String, 'log/text', 10)

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
        msg.data = text
        pub.publish(msg)
        bag_manager.write_entry(entry)

    widget.entry_submitted.connect(on_submit)
    widget.show()

    spin_timer = QTimer()
    spin_timer.timeout.connect(lambda: rclpy.spin_once(node, timeout_sec=0))
    spin_timer.start(33)

    signal.signal(signal.SIGINT, signal.SIG_DFL)

    exit_code = app.exec_()
    spin_timer.stop()
    bag_manager.close()
    node.destroy_node()
    rclpy.shutdown()
    sys.exit(exit_code)
