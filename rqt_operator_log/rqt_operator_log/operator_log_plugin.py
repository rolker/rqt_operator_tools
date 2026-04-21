"""rqt plugin for the operator logbook."""

import time

from python_qt_binding.QtCore import QTimer
from rqt_gui_py.plugin import Plugin
from std_msgs.msg import String

from .bag_manager import BagManager
from .log_entry import EntryType, LogEntry
from .log_widget import LogWidget


class OperatorLogPlugin(Plugin):
    """rqt plugin that provides an operator logbook with bag recording."""

    def __init__(self, context):
        super().__init__(context)
        self.setObjectName('OperatorLogPlugin')

        self._node = context.node
        self._node.declare_parameter('log_directory', '')
        log_dir = self._node.get_parameter('log_directory').get_parameter_value().string_value
        self._bag_manager = BagManager(self._node, base_dir=log_dir)

        # Publisher for live log entries
        self._pub = self._node.create_publisher(String, 'log/text', 10)

        # Widget
        self._widget = LogWidget()
        self._widget.setWindowTitle('Operator Log')
        self._widget.entry_submitted.connect(self._on_entry_submitted)
        context.add_widget(self._widget)

        # Recover today's entries
        self._recover()

    def _recover(self):
        """Load entries from today's bag segments into the timeline."""
        entries = self._bag_manager.recover_entries()
        for entry in entries:
            self._widget.append_entry(entry)
        if entries:
            self._node.get_logger().info(
                f'Recovered {len(entries)} log entries from today'
            )

    def _on_entry_submitted(self, text: str):
        """Handle a new text entry from the widget."""
        entry = LogEntry(
            timestamp_ns=time.time_ns(),
            entry_type=EntryType.OPERATOR_TEXT,
            author=self._widget.author,
            text=text,
        )

        # Display in timeline
        self._widget.append_entry(entry)

        # Publish to ROS
        msg = String()
        msg.data = text
        self._pub.publish(msg)

        # Record to bag
        self._bag_manager.write_entry(entry)

    def shutdown_plugin(self):
        self._bag_manager.close()

    def save_settings(self, plugin_settings, instance_settings):
        instance_settings.set_value('author', self._widget.author)

    def restore_settings(self, plugin_settings, instance_settings):
        author = instance_settings.value('author', '')
        if author:
            self._widget.author = author
