"""Standalone entry point: the scoreboard in its own window (wall display)."""

import argparse
import signal
import sys

import rclpy
from python_qt_binding.QtCore import Qt, QTimer
from python_qt_binding.QtWidgets import QApplication

from .coverage_widget import CoverageWidget
from .score_model import CoverageConfig, VIEWS


def main(argv=None):
    """Run the scoreboard until the window is closed."""
    parser = argparse.ArgumentParser(description='Survey coverage scoreboard')
    parser.add_argument('--config', '-c', help='Path to a YAML settings file')
    parser.add_argument('--view', choices=VIEWS, help='Override the view')
    parser.add_argument('--topic', help='Override the coverage_assessment topic')
    parser.add_argument('--always-on-top', action='store_true',
                        help='Keep window above other windows')
    args, ros_args = parser.parse_known_args(argv)

    app = QApplication.instance() or QApplication(sys.argv)
    rclpy.init(args=ros_args)
    node = rclpy.create_node('survey_coverage')

    config = CoverageConfig()
    if args.config:
        try:
            with open(args.config) as handle:
                config = CoverageConfig.from_yaml(handle.read())
        except OSError as exc:
            node.get_logger().error(f'Cannot read {args.config}: {exc}')
    if args.view:
        config.view = args.view
    if args.topic:
        config.topic = args.topic

    widget = CoverageWidget(node)
    widget.load_config(config)
    if args.always_on_top:
        widget.setWindowFlags(widget.windowFlags() | Qt.WindowStaysOnTopHint)
    widget.setWindowTitle('Survey Coverage')
    widget.resize(900, 360)
    widget.show()

    # Spin ROS from the Qt thread so callbacks and widgets share one thread.
    spin_timer = QTimer()
    spin_timer.timeout.connect(lambda: rclpy.spin_once(node, timeout_sec=0))
    spin_timer.start(33)
    signal.signal(signal.SIGINT, signal.SIG_DFL)

    exit_code = app.exec_()
    spin_timer.stop()
    widget.shutdown()
    node.destroy_node()
    rclpy.shutdown()
    sys.exit(exit_code)


if __name__ == '__main__':
    main()
