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


def make_log_topic(namespace: str) -> str:
    """Build the full log topic name from a namespace."""
    if namespace:
        ns = namespace.strip('/')
        return f'/{ns}/log/text'
    return 'log/text'


#: Default filename for the per-day durable text sidecar.
JSONL_NAME = 'operator_log.jsonl'

#: Default seconds between periodic bag flushes (0 disables the timer).
#: 10 minutes keeps the segment-file count low while bounding how much
#: recorded-topic data a crash can lose (operator text is durable via the
#: sidecar regardless).
DEFAULT_FLUSH_INTERVAL_SEC = 600.0


class BagManager:
    """Write log entries to daily mcap bags and recover from existing ones.

    Each operator entry is also appended to a per-day JSON-lines sidecar
    (``operator_log.jsonl``) that is flushed and fsync'd on every write, so
    entries are durable and human-readable the moment they are logged. The
    mcap bag carries the entries alongside any recorded topics for synchronized
    playback; it is buffered and flushed periodically (see ``flush``) rather
    than per entry, to keep the file count bounded.
    """

    def __init__(
        self,
        node: Node,
        base_dir: str = '',
        log_namespace: str = '',
        flush_interval_sec: float = DEFAULT_FLUSH_INTERVAL_SEC,
    ):
        self._node = node
        self._base_dir = base_dir or os.path.expanduser('~/operator_logs')
        self._log_topic = make_log_topic(log_namespace)
        self._writer = None
        self._writer_lock = threading.Lock()
        self._current_day = None
        self._registered_topics: set[tuple[str, str]] = set()  # (topic_name, msg_type_str)
        # Durable text sidecar (per local day).
        self._jsonl_file = None
        self._jsonl_day = None
        # Set when the bag has buffered writes not yet flushed to disk.
        self._dirty = False
        # Periodic flush timer — bounds how much buffered bag data a crash can
        # lose. Fires on the node's executor; flush() guards with the lock.
        self._flush_timer = None
        if flush_interval_sec and flush_interval_sec > 0:
            self._flush_timer = node.create_timer(
                flush_interval_sec, self._on_flush_timer
            )

    @property
    def log_topic(self) -> str:
        return self._log_topic

    @staticmethod
    def _dt(timestamp_ns: int) -> datetime:
        """Convert an epoch-ns timestamp to an aware UTC datetime."""
        return datetime.fromtimestamp(timestamp_ns / 1e9, tz=timezone.utc)

    def _local_day(self, timestamp_ns: int):
        """Return the local calendar date for an epoch-ns timestamp."""
        return self._dt(timestamp_ns).astimezone().date()

    def _day_dir(self, dt: datetime) -> str:
        """Return the day directory for a datetime, using local time for naming."""
        local_dt = dt.astimezone()
        return os.path.join(self._base_dir, local_dt.strftime('%Y-%m-%d'))

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
        """Open a writer for the local day containing timestamp_ns, rotating if needed."""
        dt = datetime.fromtimestamp(timestamp_ns / 1e9, tz=timezone.utc)
        local_day = dt.astimezone().date()

        if self._writer is not None and self._current_day == local_day:
            return

        self._close_writer()
        day_dir = self._day_dir(dt)
        uri = self._next_segment_uri(day_dir)
        self._current_day = local_day

        writer = SequentialWriter()
        storage = StorageOptions(uri=uri, storage_id='mcap')
        converter = ConverterOptions('cdr', 'cdr')
        writer.open(storage, converter)

        topic = TopicMetadata(
            id=0,
            name=self._log_topic,
            type='std_msgs/msg/String',
            serialization_format='cdr',
        )
        writer.create_topic(topic)
        self._writer = writer

        # Re-register any extra topics from previous segment
        for topic_name, msg_type_str in list(self._registered_topics):
            self._writer.create_topic(TopicMetadata(
                id=0,
                name=topic_name,
                type=msg_type_str,
                serialization_format='cdr',
            ))

        self._node.get_logger().info(f'Opened bag: {uri}')

    def write_entry(self, entry: LogEntry):
        """Persist a log entry: durably to the sidecar, and to the mcap bag.

        The JSON-lines sidecar is flushed and fsync'd before this returns, so
        the entry survives an unclean shutdown immediately. The mcap copy is
        buffered and finalized by the periodic flush (see ``flush``).
        """
        with self._writer_lock:
            self._append_jsonl(entry)

            self._ensure_writer(entry.timestamp_ns)
            msg = String()
            msg.data = json.dumps({
                'type': entry.entry_type.value,
                'author': entry.author,
                'text': entry.text,
            })
            serialized = rclpy.serialization.serialize_message(msg)
            self._writer.write(self._log_topic, serialized, entry.timestamp_ns)
            self._dirty = True

    def _append_jsonl(self, entry: LogEntry):
        """Append an entry to the per-day sidecar, durably. Caller holds lock."""
        local_day = self._local_day(entry.timestamp_ns)
        if self._jsonl_file is None or self._jsonl_day != local_day:
            self._close_jsonl()
            day_dir = self._day_dir(self._dt(entry.timestamp_ns))
            os.makedirs(day_dir, exist_ok=True)
            self._jsonl_file = open(
                os.path.join(day_dir, JSONL_NAME), 'a', encoding='utf-8'
            )
            self._jsonl_day = local_day
        self._jsonl_file.write(json.dumps({
            'ts_ns': entry.timestamp_ns,
            'type': entry.entry_type.value,
            'author': entry.author,
            'text': entry.text,
        }) + '\n')
        self._jsonl_file.flush()
        os.fsync(self._jsonl_file.fileno())

    def flush(self):
        """Finalize the current mcap file so buffered bag data is durable.

        rosbag2/mcap buffer writes in memory; the segment file stays 0 bytes
        until the bag is closed. ``rosbag2_py`` exposes no per-message flush,
        but ``split_bagfile()`` finalizes the current ``.mcap`` (making every
        prior write durable and readable) and opens a fresh one. Operator text
        is already durable via the sidecar; this bounds how much *recorded
        topic* data a crash can lose to the flush interval. No-op when nothing
        new has been written, to avoid emitting empty files while idle.
        """
        with self._writer_lock:
            self._flush_locked()

    def _on_flush_timer(self):
        with self._writer_lock:
            self._flush_locked()

    def _flush_locked(self):
        """Finalize the current bag file if dirty. Caller must hold the lock."""
        if self._writer is not None and self._dirty:
            self._writer.split_bagfile()
            self._dirty = False

    def register_topic(self, topic_name: str, msg_type_str: str):
        """Register an additional topic for recording."""
        key = (topic_name, msg_type_str)
        with self._writer_lock:
            if key in self._registered_topics:
                return
            self._registered_topics.add(key)
            if self._writer is not None:
                self._writer.create_topic(TopicMetadata(
                    id=0,
                    name=topic_name,
                    type=msg_type_str,
                    serialization_format='cdr',
                ))

    def write_serialized(self, topic_name: str, serialized: bytes, timestamp_ns: int):
        """Write a pre-serialized message to the bag."""
        with self._writer_lock:
            self._ensure_writer(timestamp_ns)
            self._writer.write(topic_name, serialized, timestamp_ns)
            self._dirty = True

    def recover_entries(self) -> list[LogEntry]:
        """Read all log entries from all day directories."""
        base = Path(self._base_dir)
        if not base.is_dir():
            return []

        entries = []
        for day_dir in sorted(base.iterdir()):
            if not day_dir.is_dir():
                continue
            sidecar = day_dir / JSONL_NAME
            if sidecar.is_file():
                # The sidecar is the durable, authoritative text record; the
                # bag mirrors it for playback, so reading both would duplicate.
                entries.extend(self._read_jsonl(sidecar))
                continue
            # No sidecar (older bags, or topic-only recordings) — recover from
            # the mcap segments instead.
            for segment in sorted(day_dir.glob('operator_log_*')):
                try:
                    entries.extend(self._read_segment(str(segment)))
                except Exception as exc:
                    # The segment has no metadata.yaml (e.g. unclean shutdown:
                    # it is only written on close). Fall back to reading the
                    # self-describing .mcap files directly so flushed entries
                    # are still recovered after a crash.
                    entries.extend(self._recover_loose_files(segment, exc))
        return entries

    def _read_jsonl(self, path: Path) -> list[LogEntry]:
        """Read entries from a per-day JSON-lines sidecar."""
        entries: list[LogEntry] = []
        try:
            with open(path, encoding='utf-8') as fh:
                for line in fh:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        obj = json.loads(line)
                        entries.append(LogEntry(
                            timestamp_ns=int(obj['ts_ns']),
                            entry_type=EntryType(obj['type']),
                            author=obj.get('author', ''),
                            text=obj.get('text', ''),
                        ))
                    except (json.JSONDecodeError, KeyError, ValueError) as exc:
                        # A torn final line from a crash mid-write — skip it,
                        # keep every complete entry before it.
                        self._node.get_logger().warn(
                            f'Skipping malformed sidecar line in {path}: {exc}'
                        )
        except OSError as exc:
            self._node.get_logger().warn(f'Failed to read sidecar {path}: {exc}')
        return entries

    def _recover_loose_files(self, segment: Path, exc: Exception) -> list[LogEntry]:
        """Recover entries from a segment dir lacking metadata.yaml."""
        entries: list[LogEntry] = []
        recovered_any = False
        for mcap_file in sorted(segment.glob('*.mcap')):
            try:
                entries.extend(self._read_segment(str(mcap_file)))
                recovered_any = True
            except Exception as inner:
                # An empty trailing file (the open writer target at crash
                # time) or a torn write — skip it and keep the rest.
                self._node.get_logger().warn(
                    f'Failed to read bag file {mcap_file}: {inner}'
                )
        if not recovered_any:
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
            if topic.endswith('log/text'):
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
        self._dirty = False

    def _close_jsonl(self):
        if self._jsonl_file is not None:
            try:
                self._jsonl_file.close()
            finally:
                self._jsonl_file = None
                self._jsonl_day = None

    def close(self):
        if self._flush_timer is not None:
            self._node.destroy_timer(self._flush_timer)
            self._flush_timer = None
        with self._writer_lock:
            self._close_writer()
            self._close_jsonl()
