"""Manage daily rosbag2 recording for operator log entries."""

import json
import os
import re
import threading
from datetime import datetime, timezone
from pathlib import Path

import rclpy.serialization
from rclpy.node import Node
from rosbag2_py import (
    ConverterOptions,
    SequentialReader,
    SequentialWriter,
    StorageOptions,
    TopicMetadata,
)
from std_msgs.msg import String

from .log_entry import EntryType, LogEntry


class BagManager:
    """Write log entries to daily mcap bags and recover from existing ones."""

    TEXT_TOPIC = 'log/text'

    def __init__(self, node: Node, base_dir: str = ''):
        self._node = node
        self._base_dir = base_dir or os.path.expanduser('~/operator_logs')
        self._writer = None
        self._writer_lock = threading.Lock()
        self._current_day = None
        self._current_uri = None
        self._topic_registered = False

    def _day_dir(self, day: datetime) -> str:
        return os.path.join(self._base_dir, day.strftime('%Y%m%d'))

    def _next_segment_uri(self, day_dir: str) -> str:
        """Find the next available segment number in a day directory."""
        os.makedirs(day_dir, exist_ok=True)
        max_num = -1
        for path in Path(day_dir).glob('operator_log_*'):
            m = re.search(r'operator_log_(\d+)', path.name)
            if m:
                max_num = max(max_num, int(m.group(1)))
        segment = max_num + 1
        return os.path.join(day_dir, f'operator_log_{segment:03d}')

    def _ensure_writer(self, timestamp_ns: int):
        """Open a writer for the day containing timestamp_ns, rotating if needed."""
        dt = datetime.fromtimestamp(timestamp_ns / 1e9, tz=timezone.utc)
        day = dt.date()

        if self._writer is not None and self._current_day == day:
            return

        self._close_writer()
        day_dir = self._day_dir(dt)
        uri = self._next_segment_uri(day_dir)
        self._current_uri = uri
        self._current_day = day

        writer = SequentialWriter()
        storage = StorageOptions(uri=uri, storage_id='mcap')
        converter = ConverterOptions('cdr', 'cdr')
        writer.open(storage, converter)

        topic = TopicMetadata(
            id=0,
            name=self.TEXT_TOPIC,
            type='std_msgs/msg/String',
            serialization_format='cdr',
        )
        writer.create_topic(topic)
        self._writer = writer
        self._topic_registered = True
        self._node.get_logger().info(f'Opened bag: {uri}')

    def write_entry(self, entry: LogEntry):
        """Serialize and write a log entry to the bag."""
        with self._writer_lock:
            self._ensure_writer(entry.timestamp_ns)

            msg = String()
            msg.data = json.dumps({
                'type': entry.entry_type.value,
                'author': entry.author,
                'text': entry.text,
            })
            serialized = rclpy.serialization.serialize_message(msg)
            self._writer.write(self.TEXT_TOPIC, serialized, entry.timestamp_ns)

    def recover_entries(self) -> list[LogEntry]:
        """Read all entries from today's bag segments."""
        now = datetime.now(tz=timezone.utc)
        day_dir = self._day_dir(now)
        if not os.path.isdir(day_dir):
            return []

        entries = []
        segments = sorted(Path(day_dir).glob('operator_log_*'))
        for segment in segments:
            try:
                entries.extend(self._read_segment(str(segment)))
            except Exception as exc:
                self._node.get_logger().warn(
                    f'Failed to read bag segment {segment}: {exc}'
                )
        return entries

    def _read_segment(self, uri: str) -> list[LogEntry]:
        """Read log entries from a single bag segment."""
        reader = SequentialReader()
        storage = StorageOptions(uri=uri, storage_id='mcap')
        converter = ConverterOptions('cdr', 'cdr')
        reader.open(storage, converter)

        entries = []
        while reader.has_next():
            topic, data, timestamp_ns = reader.read_next()
            if topic == self.TEXT_TOPIC:
                msg = rclpy.serialization.deserialize_message(data, String)
                entry = self._parse_entry(msg.data, timestamp_ns)
                if entry:
                    entries.append(entry)
        return entries

    @staticmethod
    def _parse_entry(text: str, timestamp_ns: int) -> LogEntry | None:
        """Parse a serialized log string back into a LogEntry."""
        try:
            obj = json.loads(text)
            return LogEntry(
                timestamp_ns=timestamp_ns,
                entry_type=EntryType(obj['type']),
                author=obj.get('author', ''),
                text=obj.get('text', ''),
            )
        except (json.JSONDecodeError, KeyError, ValueError):
            # Plain text fallback for non-JSON entries
            return LogEntry(
                timestamp_ns=timestamp_ns,
                entry_type=EntryType.OPERATOR_TEXT,
                text=text,
            )

    def _close_writer(self):
        if self._writer is not None:
            self._writer.close()
            self._writer = None
            self._topic_registered = False

    def close(self):
        with self._writer_lock:
            self._close_writer()
