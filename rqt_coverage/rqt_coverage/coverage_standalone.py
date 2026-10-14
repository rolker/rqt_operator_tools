"""Standalone entry point: the scoreboard in its own window (wall display)."""

import argparse
import signal
import sys
import threading

import rclpy
import yaml
from rclpy.executors import SingleThreadedExecutor
from python_qt_binding.QtCore import Qt
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
    rclpy.init(args=sys.argv[:1] + ros_args)
    node = rclpy.create_node('survey_coverage')

    config = CoverageConfig()
    if args.config:
        try:
            with open(args.config, encoding='utf-8') as handle:
                text = handle.read()
            yaml.safe_load(text)   # from_yaml is lenient; say so when it cannot parse
            config = CoverageConfig.from_yaml(text)
        except (OSError, UnicodeError, yaml.YAMLError) as exc:
            node.get_logger().error(
                f'Cannot use {args.config}, starting with defaults: {exc}')
    if args.view:
        config.view = args.view
    if args.topic:
        config.topic = args.topic

    widget = CoverageWidget(node, config=config)
    if args.always_on_top:
        widget.setWindowFlags(widget.windowFlags() | Qt.WindowStaysOnTopHint)
    widget.setWindowTitle('Survey Coverage')
    widget.resize(900, 360)
    widget.show()

    # Spin ROS on its own thread, as rqt_gui_py does.  The subscription
    # callback only emits a queued Qt signal, so widgets stay on the Qt thread.
    executor = SingleThreadedExecutor()
    executor.add_node(node)
    spin_thread = threading.Thread(target=executor.spin, name='survey_coverage_spin',
                                   daemon=True)
    spin_thread.start()
    signal.signal(signal.SIGINT, signal.SIG_DFL)

    exit_code = app.exec_()
    widget.shutdown()
    executor.shutdown()
    spin_thread.join(timeout=2.0)
    node.destroy_node()
    rclpy.shutdown()
    sys.exit(exit_code)


if __name__ == '__main__':
    main()
