"""
High-level Python API for FastEvents.

Wraps the _fastevents C extension with a Pythonic interface:
- track() instead of append() to match analytics terminology
- **properties kwargs instead of a raw JSON string
- filter() returns proper Event dicts with parsed properties
- count_by() and unique() implemented in Python on top of filter()
"""

import json
import time
from typing import Any, Dict, List, Optional

import _fastevents
from .types import Event


class EventStore:
    """
    High-performance embedded event store.

    Example:
        with EventStore("./data") as db:
            db.track("page_view", user_id=123, page="/pricing")
            events = db.filter(event_type="page_view", user_id=123)
    """

    def __init__(self, db_path: str):
        self._store = _fastevents.EventStore(db_path)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
        return False

    def __del__(self):
        self.close()

    # ── Write ──────────────────────────────────────────────────────────────────

    def track(self,
              event_type: str,
              user_id: int,
              timestamp: Optional[int] = None,
              **properties) -> None:
        """
        Track a new event.

        Args:
            event_type: Event type string (e.g. "page_view", "click")
            user_id:    User identifier (must be > 0)
            timestamp:  Unix timestamp in milliseconds (defaults to now)
            **properties: Any additional key/value properties

        Example:
            db.track("click", user_id=123, button="signup", page="/pricing")
        """
        if not event_type:
            raise ValueError("event_type cannot be empty")
        if not isinstance(user_id, int) or user_id <= 0:
            raise ValueError("user_id must be a positive integer")

        if timestamp is None:
            timestamp = int(time.time() * 1000)
        elif not isinstance(timestamp, int) or timestamp < 0:
            raise ValueError("timestamp must be a non-negative integer")

        props_json = json.dumps(properties) if properties else None
        self._store.append(event_type, user_id=user_id,
                           timestamp=timestamp, properties=props_json)

    def flush(self) -> None:
        """Flush the active event buffer to a segment file on disk."""
        self._store.flush()

    # ── Query ──────────────────────────────────────────────────────────────────

    def filter(self,
               event_type: Optional[str] = None,
               user_id: Optional[int] = None,
               start_time: Optional[int] = None,
               end_time: Optional[int] = None) -> List[Event]:
        """
        Filter events by criteria. All parameters are optional.

        Args:
            event_type: Match only this event type (None = any)
            user_id:    Match only this user (None = any)
            start_time: Minimum timestamp inclusive in ms (None = no bound)
            end_time:   Maximum timestamp inclusive in ms (None = no bound)

        Returns:
            List of Event dicts with keys: event_type, user_id, timestamp, properties
        """
        rows = self._store.filter(
            event_type,
            user_id    or 0,
            start_time or 0,
            end_time   or 0,
        )
        return [_parse_row(r) for r in rows]
    


    def count_by(self,
                 field: str,
                 event_type: Optional[str] = None,
                 start_time: Optional[int] = None,
                 end_time: Optional[int] = None) -> Dict[Any, int]:
        """
        Count events grouped by a field.

        Args:
            field:      "event_type" or "user_id"
            event_type: Optional pre-filter by event type
            start_time: Optional minimum timestamp
            end_time:   Optional maximum timestamp

        Returns:
            Dict mapping field value → count

        Example:
            db.count_by("event_type")  # {"page_view": 5234, "click": 892}
        """
        if field not in ("event_type", "user_id"):
            raise ValueError(f"count_by only supports 'event_type' or 'user_id', got '{field}'")

        events = self.filter(event_type=event_type,
                             start_time=start_time, end_time=end_time)
        counts: Dict[Any, int] = {}
        for e in events:
            key = e[field]
            counts[key] = counts.get(key, 0) + 1
        return counts

    def unique(self,
               field: str,
               event_type: Optional[str] = None,
               start_time: Optional[int] = None,
               end_time: Optional[int] = None) -> int:
        """
        Count distinct values for a field across matching events.

        Args:
            field:      Field to count unique values for ("user_id")
            event_type: Optional pre-filter by event type
            start_time: Optional minimum timestamp
            end_time:   Optional maximum timestamp

        Returns:
            Number of unique values

        Example:
            db.unique("user_id", event_type="purchase")
        """
        events = self.filter(event_type=event_type,
                             start_time=start_time, end_time=end_time)
        return len({e[field] for e in events})

    def first(self,
              event_type: Optional[str] = None,
              user_id: Optional[int] = None) -> Optional[Event]:
        """
        Return the earliest matching event by timestamp, or None if no match.

        When called with no filters, uses segment metadata to find the min
        timestamp without loading any segment data, then fetches only that
        moment — O(segments) instead of O(events).

        Example:
            db.first(event_type="purchase", user_id=123)
        """
        if event_type is None and user_id is None:
            ts = self._store.min_timestamp()
            if ts is None:
                return None
            events = self.filter(start_time=ts, end_time=ts)
            return events[0] if events else None

        events = self.filter(event_type=event_type, user_id=user_id)
        return min(events, key=lambda e: e["timestamp"], default=None)

    def last(self,
             event_type: Optional[str] = None,
             user_id: Optional[int] = None) -> Optional[Event]:
        """
        Return the most recent matching event by timestamp, or None if no match.

        When called with no filters, uses segment metadata to find the max
        timestamp without loading any segment data, then fetches only that
        moment — O(segments) instead of O(events).

        Example:
            db.last(event_type="page_view")
        """
        if event_type is None and user_id is None:
            ts = self._store.max_timestamp()
            if ts is None:
                return None
            events = self.filter(start_time=ts, end_time=ts)
            return events[0] if events else None

        events = self.filter(event_type=event_type, user_id=user_id)
        return max(events, key=lambda e: e["timestamp"], default=None)

    def funnel(self,
               steps: List[str],
               within: Optional[int] = None) -> List[Dict[str, Any]]:
        """
        Conversion funnel analysis.

        For each step, counts how many users who completed step N also went on
        to complete step N+1 (in order). Produces a drop-off table.

        Args:
            steps:  Ordered list of event types, e.g. ["page_view", "click", "purchase"]
            within: Optional time window in ms. If set, a user must complete
                    the entire funnel within this duration of their first step.

        Returns:
            List of dicts, one per step:
                step            — event type name
                users           — number of users who reached this step
                conversion_rate — fraction of step-1 users who reached this step

        Example:
            db.funnel(["page_view", "click", "purchase"], within=3_600_000)
            # [{"step": "page_view", "users": 1000, "conversion_rate": 1.0},
            #  {"step": "click",     "users":  450, "conversion_rate": 0.45},
            #  {"step": "purchase",  "users":   89, "conversion_rate": 0.089}]
        """
        if not steps:
            return []

        # Collect per-user timestamps for each step: uid → {step: sorted [ts]}
        user_times: Dict[int, Dict[str, List[int]]] = {}
        for step in steps:
            for e in self.filter(event_type=step):
                uid = e["user_id"]
                user_times.setdefault(uid, {}).setdefault(step, []).append(e["timestamp"])
        for uid in user_times:
            for step in user_times[uid]:
                user_times[uid][step].sort()

        # entry_times[uid] = earliest timestamp at which the user entered the funnel
        entry_times: Dict[int, int] = {}
        for uid, steps_map in user_times.items():
            if steps[0] in steps_map:
                entry_times[uid] = min(steps_map[steps[0]])

        first_count = len(entry_times)
        results: List[Dict[str, Any]] = [
            {"step": steps[0], "users": first_count, "conversion_rate": 1.0}
        ]

        for i in range(1, len(steps)):
            curr_step = steps[i]
            next_entry: Dict[int, int] = {}

            for uid, prev_time in entry_times.items():
                curr_times = user_times.get(uid, {}).get(curr_step, [])
                valid = [t for t in curr_times if t >= prev_time]
                if not valid:
                    continue
                curr_time = min(valid)
                if within is not None:
                    start_time = min(user_times[uid][steps[0]])
                    if curr_time - start_time > within:
                        continue
                next_entry[uid] = curr_time

            entry_times = next_entry
            users = len(entry_times)
            results.append({
                "step": curr_step,
                "users": users,
                "conversion_rate": round(users / first_count, 4) if first_count else 0.0,
            })

        return results

    def event_counts(self,
                     start_time: Optional[int] = None,
                     end_time: Optional[int] = None) -> Dict[str, int]:
        """
        Return the number of times each event type occurred.

        Args:
            start_time: Optional minimum timestamp
            end_time:   Optional maximum timestamp

        Returns:
            Dict mapping event_type → count

        Example:
            db.event_counts()  # {"page_view": 5234, "click": 892}
        """
        return self.count_by("event_type",
                             start_time=start_time, end_time=end_time)

    # ── Utilities ──────────────────────────────────────────────────────────────

    def size(self) -> int:
        """Total number of events in the store."""
        return self._store.size()

    def close(self) -> None:
        """Close the store and flush pending writes."""
        if self._store is not None:
            self._store.close()
            self._store = None

# ── Helpers ───────────────────────────────────────────────────────────────────

def _parse_row(row: dict) -> Event:
    """Convert a raw C extension row dict into a typed Event dict."""
    props = row["properties"]
    return Event(
        event_type=row["event_type"],
        user_id=row["user_id"],
        timestamp=row["timestamp"],
        properties=json.loads(props) if props else None,
    )
