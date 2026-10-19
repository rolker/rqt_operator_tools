"""Standalone entry point: the ping profile in its own window (wall display)."""

import argparse
import signal
import sys
import threading

import rclpy
import yaml
from rclpy.executors import SingleThreadedExecutor
from python_qt_binding.QtCore import Qt
from python_qt_binding.QtWidgets import QApplication

from .ping_profile_widget import PingProfileWidget
from .platform_lookup import normalize_hex_color
from .profile_model import DEPTH_MODES, ProfileConfig


def main(argv=None):
    """Run the view until the window is closed."""
    parser = argparse.ArgumentParser(description='Multibeam ping profile')
    parser.add_argument('--config', '-c', help='Path to a YAML settings file')
    parser.add_argument('--topic', help='Override the SonarDetections topic')
    parser.add_argument('--title', help='Override the title')
    parser.add_argument('--color', help='Override the colour, #rrggbb (else the '
                        'platform colour from /marine/platforms)')
    parser.add_argument('--depth-mode', choices=DEPTH_MODES,
                        help='Override the depth axis mode')
    parser.add_argument('--always-on-top', action='store_true',
                        help='Keep window above other windows')
    args, ros_args = parser.parse_known_args(argv)

    app = QApplication.instance() or QApplication(sys.argv)
    rclpy.init(args=sys.argv[:1] + ros_args)
    node = rclpy.create_node('ping_profile')

    config = ProfileConfig()
    if args.config:
        try:
            with open(args.config, encoding='utf-8') as handle:
                text = handle.read()
            yaml.safe_load(text)   # from_yaml is lenient; say so when it cannot parse
            config = ProfileConfig.from_yaml(text)
        except (OSError, UnicodeError, yaml.YAMLError) as exc:
            node.get_logger().error(
                f'Cannot use {args.config}, starting with defaults: {exc}')
    if args.topic:
        config.topic = args.topic
    if args.title:
        config.title = args.title
    if args.color:
        config.color = args.color
        if not normalize_hex_color(args.color):
            node.get_logger().error(
                f'--color {args.color!r} is not #rrggbb; using the platform colour')
    if args.depth_mode:
        config.depth_mode = args.depth_mode

    # Spin ROS on its own thread, as rqt_gui_py does.  The subscription
    # callback only emits a queued Qt signal, so widgets stay on the Qt thread.
    executor = SingleThreadedExecutor()
    executor.add_node(node)
    spin_thread = threading.Thread(target=executor.spin, name='ping_profile_spin',
                                   daemon=True)
    widget = None
    exit_code = 1
    try:
        widget = PingProfileWidget(node, config=config)
        if args.always_on_top:
            widget.setWindowFlags(widget.windowFlags() | Qt.WindowStaysOnTopHint)
        widget.setWindowTitle('Ping Profile')
        widget.resize(600, 420)
        widget.show()
        spin_thread.start()
        signal.signal(signal.SIGINT, signal.SIG_DFL)
        exit_code = app.exec_()
    finally:
        # Stop the executor before destroying the subscription it may be waiting on.
        executor.shutdown()
        if spin_thread.is_alive():
            spin_thread.join(timeout=2.0)
        if widget is not None:
            widget.shutdown()
        node.destroy_node()
        rclpy.shutdown()
    sys.exit(exit_code)


if __name__ == '__main__':
    main()
