"""rqt plugin for the operator logbook."""

import os
import time

from rqt_gui_py.plugin import Plugin
from std_msgs.msg import String

from .bag_manager import BagManager
from .log_entry import EntryType, LogEntry
from .log_widget import LogWidget
from .topic_recorder import TopicRecorder

DEFAULT_LOG_DIR = os.path.expanduser('~/operator_logs')
ENV_LOG_DIR = 'OPERATOR_LOG_DIR'
ENV_TOPICS = 'OPERATOR_LOG_TOPICS'


def _resolve_log_dir(settings_dir: str) -> str:
    """Resolve log directory: rqt settings > env var > default."""
    if settings_dir:
        return settings_dir
    return os.environ.get(ENV_LOG_DIR, DEFAULT_LOG_DIR)


def _resolve_record_topics() -> list[str]:
    """Resolve topics to record from env var."""
    raw = os.environ.get(ENV_TOPICS, '')
    if not raw:
        return []
    return [t.strip() for t in raw.split(',') if t.strip()]


class OperatorLogPlugin(Plugin):
    """rqt plugin that provides an operator logbook with bag recording."""

    def __init__(self, context):
        super().__init__(context)
        self.setObjectName('OperatorLogPlugin')

        self._node = context.node
        self._log_dir = ''  # populated by restore_settings before _recover
        self._bag_manager = None
        self._topic_recorder = None

        # Publisher for live log entries
        self._pub = self._node.create_publisher(String, 'log/text', 10)

        # Widget
        self._widget = LogWidget()
        self._widget.setWindowTitle('Operator Log')
        self._widget.entry_submitted.connect(self._on_entry_submitted)
        context.add_widget(self._widget)

    def _ensure_bag_manager(self):
        """Create the bag manager and topic recorder if not yet created."""
        if self._bag_manager is not None:
            return
        log_dir = _resolve_log_dir(self._log_dir)
        self._bag_manager = BagManager(self._node, base_dir=log_dir)
        self._node.get_logger().info(f'Log directory: {log_dir}')

        record_topics = _resolve_record_topics()
        if record_topics:
            self._topic_recorder = TopicRecorder(
                self._node, self._bag_manager, record_topics
            )

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
        self._ensure_bag_manager()
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
        if self._topic_recorder:
            self._topic_recorder.shutdown()
        if self._bag_manager:
            self._bag_manager.close()

    def save_settings(self, plugin_settings, instance_settings):
        instance_settings.set_value('author', self._widget.author)
        if self._log_dir:
            instance_settings.set_value('log_directory', self._log_dir)

    def restore_settings(self, plugin_settings, instance_settings):
        author = instance_settings.value('author', '')
        if author:
            self._widget.author = author
        self._log_dir = instance_settings.value('log_directory', '')
        self._ensure_bag_manager()

    def trigger_configuration(self):
        """Called when the user clicks the wrench icon in rqt."""
        from python_qt_binding.QtWidgets import QFileDialog
        current = _resolve_log_dir(self._log_dir)
        chosen = QFileDialog.getExistingDirectory(
            self._widget, 'Select Log Directory', current,
        )
        if chosen:
            self._log_dir = chosen
            self._node.get_logger().info(f'Log directory changed to: {chosen}')
            if self._topic_recorder:
                self._topic_recorder.shutdown()
                self._topic_recorder = None
            if self._bag_manager:
                self._bag_manager.close()
            self._bag_manager = BagManager(self._node, base_dir=chosen)

            record_topics = _resolve_record_topics()
            if record_topics:
                self._topic_recorder = TopicRecorder(
                    self._node, self._bag_manager, record_topics
                )

            self._widget.clear_timeline()
            self._recover()
