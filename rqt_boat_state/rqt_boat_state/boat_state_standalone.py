"""Standalone entry point for the boat-state panel (mirrors the annunciator)."""

import argparse
import os
import signal
import sys

import rclpy
from python_qt_binding.QtCore import Qt, QTimer
from python_qt_binding.QtWidgets import QApplication

from .boat_state_widget import BoatStateWidget
from .config_model import BoatStateConfig


def main(argv=None):
    parser = argparse.ArgumentParser(description='Boat state panel')
    parser.add_argument('--config', '-c', help='Path to YAML config file')
    parser.add_argument('--always-on-top', action='store_true',
                        help='Keep window above other windows')
    args, ros_args = parser.parse_known_args(argv)

    app = QApplication.instance() or QApplication(sys.argv)

    rclpy.init(args=ros_args)
    node = rclpy.create_node('boat_state')

    widget = BoatStateWidget(node)
    if args.always_on_top:
        widget.setWindowFlags(widget.windowFlags() | Qt.WindowStaysOnTopHint)
    widget.setWindowTitle('Boat State')

    if args.config and os.path.exists(args.config):
        try:
            widget.load_config(BoatStateConfig.from_file(args.config))
            node.get_logger().info(f'Loaded config from {args.config}')
        except Exception as exc:
            node.get_logger().error(f'Failed to load config: {exc}')

    widget.resize(800, 480)
    widget.show()

    spin_timer = QTimer()
    spin_timer.timeout.connect(lambda: rclpy.spin_once(node, timeout_sec=0))
    spin_timer.start(33)  # ~30 Hz

    signal.signal(signal.SIGINT, signal.SIG_DFL)

    exit_code = app.exec_()
    spin_timer.stop()
    widget.shutdown()
    rclpy.shutdown()
    sys.exit(exit_code)
