"""Data model for operator log entries."""

from dataclasses import dataclass
from enum import Enum


class EntryType(Enum):
    OPERATOR_TEXT = 'operator_text'
    SYSTEM_EVENT = 'system_event'


@dataclass
class LogEntry:
    """A single log entry."""

    timestamp_ns: int
    entry_type: EntryType
    text: str
    author: str = ''
    source_topic: str = ''
