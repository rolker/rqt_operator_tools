"""Standalone entry point for the annunciator panel."""

import argparse
import os
import signal
import sys

import rclpy
from python_qt_binding.QtCore import Qt, QTimer
from python_qt_binding.QtWidgets import QApplication

from .annunciator_widget import AnnunciatorWidget
from .config_model import AnnunciatorConfig


def main(argv=None):
    parser = argparse.ArgumentParser(description='Annunciator panel')
    parser.add_argument(
        '--config', '-c',
        help='Path to YAML config file',
    )
    parser.add_argument(
        '--always-on-top',
        action='store_true',
        help='Keep window above other windows',
    )
    args, ros_args = parser.parse_known_args(argv)

    app = QApplication.instance() or QApplication(sys.argv)

    rclpy.init(args=ros_args)
    node = rclpy.create_node('annunciator')

    widget = AnnunciatorWidget(node)
    if args.always_on_top:
        widget.setWindowFlags(widget.windowFlags() | Qt.WindowStaysOnTopHint)
    widget.setWindowTitle('Annunciator')

    # Load config.
    config_path = args.config
    if config_path is None:
        # Try default from package share.
        try:
            from ament_index_python.packages import get_package_share_directory
            share_dir = get_package_share_directory('rqt_annunciator')
            default_path = os.path.join(share_dir, 'config', 'default_annunciator.yaml')
            if os.path.exists(default_path):
                config_path = default_path
        except Exception:
            pass

    if config_path and os.path.exists(config_path):
        try:
            config = AnnunciatorConfig.from_file(config_path)
            widget.load_config(config)
            node.get_logger().info(f'Loaded config from {config_path}')
        except Exception as exc:
            node.get_logger().error(f'Failed to load config: {exc}')
    else:
        node.get_logger().warn('No config file found — starting with empty panel')

    widget.resize(800, 60)
    widget.show()

    # Spin ROS via QTimer to keep everything on the Qt thread.
    spin_timer = QTimer()
    spin_timer.timeout.connect(lambda: rclpy.spin_once(node, timeout_sec=0))
    spin_timer.start(33)  # ~30 Hz

    # Allow Ctrl+C to work.
    signal.signal(signal.SIGINT, signal.SIG_DFL)

    exit_code = app.exec_()
    spin_timer.stop()
    widget.shutdown()
    rclpy.shutdown()
    sys.exit(exit_code)
