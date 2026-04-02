"""
High-level Python API for Fast Event Store

This module provides the user-facing EventStore class that wraps
the C extension module.
"""

from typing import List, Dict, Optional, Any
import json
from .types import Event, QuerySpec, EventStats

# This will be the C extension module (not implemented yet)
# import _fastevents


class EventStore:
    """
    High-performance embedded event store.

    Example:
        >>> db = EventStore("events.db")
        >>> db.track("page_view", user_id=123, page="/pricing")
        >>> events = db.filter(event_type="page_view", user_id=123)
    """

    def __init__(self, db_path: str):
        """
        Open or create an event store at the given path.

        Args:
            db_path: Path to the database file
        """
        self.db_path = db_path
        # TODO: Initialize C extension
        # self._handle = _fastevents.open(db_path)
        self._handle = None

    def __enter__(self):
        """Context manager support"""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Close the store on context exit"""
        self.close()
        return False

    def track(self,
              event_type: str,
              user_id: int,
              timestamp: Optional[int] = None,
              **properties) -> None:
        """
        Track a new event.

        Args:
            event_type: Type of event (e.g., "page_view", "click")
            user_id: User identifier
            timestamp: Unix timestamp in milliseconds (defaults to now)
            **properties: Additional event properties

        Example:
            >>> db.track("click", user_id=123, button="signup", page="/pricing")
        """
        if timestamp is None:
            import time
            timestamp = int(time.time() * 1000)

        # Validate inputs
        if not event_type:
            raise ValueError("event_type cannot be empty")
        if not isinstance(user_id, int) or user_id < 0:
            raise ValueError("user_id must be a non-negative integer")
        if not isinstance(timestamp, int) or timestamp < 0:
            raise ValueError("timestamp must be a non-negative integer")

        # Convert properties to JSON
        properties_json = json.dumps(properties) if properties else "{}"

        # TODO: Call C extension
        # _fastevents.append(self._handle, event_type, user_id, timestamp, properties_json)
        print(f"[TODO] Track: {event_type}, user={user_id}, ts={timestamp}, props={properties_json}")

    def filter(self,
               event_type: Optional[str] = None,
               user_id: Optional[int] = None,
               start_time: Optional[int] = None,
               end_time: Optional[int] = None) -> List[Event]:
        """
        Filter events by criteria.

        Args:
            event_type: Filter by event type
            user_id: Filter by user ID
            start_time: Minimum timestamp (inclusive)
            end_time: Maximum timestamp (exclusive)

        Returns:
            List of matching events

        Example:
            >>> events = db.filter(event_type="page_view", user_id=123)
            >>> recent = db.filter(start_time=int(time.time() * 1000) - 86400000)  # Last 24h
        """
        spec = QuerySpec(
            event_type=event_type,
            user_id=user_id,
            start_time=start_time,
            end_time=end_time
        )

        # TODO: Call C extension
        # results = _fastevents.filter(self._handle, spec)
        # return results

        print(f"[TODO] Filter: {spec}")
        return []

    def count_by(self,
                 field: str,
                 event_type: Optional[str] = None,
                 start_time: Optional[int] = None,
                 end_time: Optional[int] = None) -> Dict[Any, int]:
        """
        Count events grouped by a field.

        Args:
            field: Field to group by ("event_type" or "user_id")
            event_type: Optional filter by event type
            start_time: Optional minimum timestamp
            end_time: Optional maximum timestamp

        Returns:
            Dictionary mapping field values to counts

        Example:
            >>> counts = db.count_by("event_type")
            >>> # {"page_view": 5234, "click": 892, ...}
        """
        if field not in ("event_type", "user_id"):
            raise ValueError(f"count_by only supports 'event_type' or 'user_id', got '{field}'")

        # TODO: Call C extension
        # results = _fastevents.count_by(self._handle, field, event_type, start_time, end_time)
        # return results

        print(f"[TODO] Count by {field}")
        return {}

    def unique(self,
               field: str,
               event_type: Optional[str] = None,
               start_time: Optional[int] = None,
               end_time: Optional[int] = None) -> int:
        """
        Count unique values for a field.

        Args:
            field: Field to count unique values for ("user_id")
            event_type: Optional filter by event type
            start_time: Optional minimum timestamp
            end_time: Optional maximum timestamp

        Returns:
            Number of unique values

        Example:
            >>> unique_users = db.unique("user_id", event_type="purchase")
        """
        if field != "user_id":
            raise ValueError(f"unique() currently only supports 'user_id', got '{field}'")

        # TODO: Call C extension
        # result = _fastevents.unique(self._handle, field, event_type, start_time, end_time)
        # return result

        print(f"[TODO] Unique {field}")
        return 0

    def stats(self) -> EventStats:
        """
        Get statistics about the event store.

        Returns:
            EventStats object with store metadata
        """
        # TODO: Call C extension
        # raw_stats = _fastevents.stats(self._handle)
        # return EventStats(**raw_stats)

        return EventStats(
            total_events=0,
            num_blocks=0,
            memory_usage_bytes=0,
            disk_usage_bytes=0,
            unique_event_types=0,
            unique_users=0
        )

    def flush(self) -> None:
        """
        Flush pending writes to disk (when persistence is implemented).
        """
        # TODO: Call C extension
        # _fastevents.flush(self._handle)
        pass

    def close(self) -> None:
        """
        Close the event store and release resources.
        """
        if self._handle is not None:
            # TODO: Call C extension
            # _fastevents.close(self._handle)
            self._handle = None

    def __del__(self):
        """Ensure resources are cleaned up"""
        self.close()
