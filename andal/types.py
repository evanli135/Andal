"""
Type definitions for the Fast Event Store Python API
"""

from typing import TypedDict, Optional, Any, Dict
from dataclasses import dataclass


class Event(TypedDict, total=False):
    """Represents a single event"""
    event_type: str
    user_id: int
    timestamp: int  # Unix timestamp in milliseconds
    properties: Optional[Dict[str, Any]]


@dataclass
class QuerySpec:
    """Specification for filtering/querying events"""
    event_type: Optional[str] = None
    user_id: Optional[int] = None
    start_time: Optional[int] = None  # Unix timestamp in ms
    end_time: Optional[int] = None    # Unix timestamp in ms

    def __post_init__(self):
        """Validate query spec"""
        if self.start_time and self.end_time:
            if self.start_time > self.end_time:
                raise ValueError("start_time must be <= end_time")


@dataclass
class EventStats:
    """Statistics about the event store"""
    total_events: int
    num_blocks: int
    memory_usage_bytes: int
    disk_usage_bytes: int
    unique_event_types: int
    unique_users: int
    oldest_timestamp: Optional[int] = None
    newest_timestamp: Optional[int] = None
