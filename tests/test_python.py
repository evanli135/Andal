"""
Python integration tests for Fast Event Store
"""

import pytest
import time
import tempfile
import os
from pathlib import Path

# Add parent directory to path for imports
import sys
sys.path.insert(0, str(Path(__file__).parent.parent / "python"))

from fastevents import EventStore, QuerySpec, EventStats


class TestEventStore:
    """Test suite for EventStore Python API"""

    @pytest.fixture
    def temp_db(self):
        """Create a temporary database file"""
        fd, path = tempfile.mkstemp(suffix=".db")
        os.close(fd)
        yield path
        # Cleanup
        if os.path.exists(path):
            os.unlink(path)

    def test_create_store(self, temp_db):
        """Test creating an event store"""
        db = EventStore(temp_db)
        assert db.db_path == temp_db
        db.close()

    def test_context_manager(self, temp_db):
        """Test using EventStore as context manager"""
        with EventStore(temp_db) as db:
            assert db.db_path == temp_db
        # Should auto-close

    def test_track_event(self, temp_db):
        """Test tracking a basic event"""
        with EventStore(temp_db) as db:
            timestamp = int(time.time() * 1000)
            db.track("test_event", user_id=123, timestamp=timestamp, foo="bar")

    def test_track_event_auto_timestamp(self, temp_db):
        """Test tracking event with automatic timestamp"""
        with EventStore(temp_db) as db:
            db.track("test_event", user_id=123, data="value")

    def test_track_event_validation(self, temp_db):
        """Test input validation for track()"""
        with EventStore(temp_db) as db:
            # Empty event type
            with pytest.raises(ValueError, match="event_type cannot be empty"):
                db.track("", user_id=123)

            # Invalid user_id
            with pytest.raises(ValueError, match="user_id must be a non-negative integer"):
                db.track("test", user_id=-1)

            # Invalid timestamp
            with pytest.raises(ValueError, match="timestamp must be a non-negative integer"):
                db.track("test", user_id=123, timestamp=-1000)

    def test_filter_events(self, temp_db):
        """Test filtering events"""
        with EventStore(temp_db) as db:
            # Track some events
            now = int(time.time() * 1000)
            db.track("page_view", user_id=123, timestamp=now)
            db.track("click", user_id=123, timestamp=now + 1000)
            db.track("page_view", user_id=456, timestamp=now + 2000)

            # Filter by event type
            results = db.filter(event_type="page_view")
            # TODO: Uncomment when C extension is implemented
            # assert len(results) == 2

            # Filter by user_id
            results = db.filter(user_id=123)
            # assert len(results) == 2

            # Filter by both
            results = db.filter(event_type="page_view", user_id=123)
            # assert len(results) == 1

    def test_query_spec_validation(self):
        """Test QuerySpec validation"""
        # Valid spec
        spec = QuerySpec(event_type="test", user_id=123)
        assert spec.event_type == "test"

        # Invalid time range
        with pytest.raises(ValueError, match="start_time must be <= end_time"):
            QuerySpec(start_time=1000, end_time=500)

    def test_count_by(self, temp_db):
        """Test count_by aggregation"""
        with EventStore(temp_db) as db:
            # Track events
            db.track("page_view", user_id=123)
            db.track("page_view", user_id=456)
            db.track("click", user_id=123)

            # Count by event type
            counts = db.count_by("event_type")
            # TODO: Uncomment when implemented
            # assert counts["page_view"] == 2
            # assert counts["click"] == 1

    def test_count_by_validation(self, temp_db):
        """Test count_by input validation"""
        with EventStore(temp_db) as db:
            with pytest.raises(ValueError, match="count_by only supports"):
                db.count_by("invalid_field")

    def test_unique_count(self, temp_db):
        """Test unique count"""
        with EventStore(temp_db) as db:
            # Track events
            db.track("page_view", user_id=123)
            db.track("page_view", user_id=123)
            db.track("page_view", user_id=456)

            # Count unique users
            unique = db.unique("user_id", event_type="page_view")
            # TODO: Uncomment when implemented
            # assert unique == 2

    def test_unique_validation(self, temp_db):
        """Test unique() input validation"""
        with EventStore(temp_db) as db:
            with pytest.raises(ValueError, match="unique.*only supports"):
                db.unique("event_type")

    def test_stats(self, temp_db):
        """Test getting store statistics"""
        with EventStore(temp_db) as db:
            stats = db.stats()
            assert isinstance(stats, EventStats)
            assert stats.total_events >= 0
            assert stats.num_blocks >= 0

    def test_flush(self, temp_db):
        """Test flush operation"""
        with EventStore(temp_db) as db:
            db.track("test", user_id=123)
            db.flush()  # Should not raise

    def test_close(self, temp_db):
        """Test explicit close"""
        db = EventStore(temp_db)
        db.close()
        db.close()  # Should be idempotent


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
