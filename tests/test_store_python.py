"""
Python tests for FastEvents EventStore.

Requires _fastevents.pyd to be built first:
    make python   (from repo root)

Run with:
    python -m pytest tests/test_store_python.py -v
"""

import sys
import os
import shutil
import tempfile
import time
from pathlib import Path

ROOT = str(Path(__file__).parent.parent)
sys.path.insert(0, ROOT)

import pytest
from andal.store import EventStore

# Fixed base timestamp so tests are deterministic
T = 1_735_689_600_000  # ms


@pytest.fixture
def db(tmp_path):
    """Open a fresh store, yield it, then close."""
    store = EventStore(str(tmp_path / "events"))
    yield store
    store.close()


@pytest.fixture
def db_path(tmp_path):
    """Yield a path string for stores that need open/close cycles."""
    return str(tmp_path / "events")


# ── Lifecycle ──────────────────────────────────────────────────────────────────

class TestLifecycle:
    def test_open_and_close(self, db_path):
        store = EventStore(db_path)
        store.close()

    def test_double_close_is_safe(self, db_path):
        store = EventStore(db_path)
        store.close()
        store.close()  # must not raise

    def test_context_manager(self, db_path):
        with EventStore(db_path) as store:
            assert store is not None
        # store.close() called automatically

    def test_size_empty(self, db):
        assert db.size() == 0


# ── track() ────────────────────────────────────────────────────────────────────

class TestTrack:
    def test_basic_track(self, db):
        db.track("page_view", user_id=1, timestamp=T)
        assert db.size() == 1

    def test_track_increments_size(self, db):
        db.track("click", user_id=1, timestamp=T)
        db.track("click", user_id=2, timestamp=T + 1)
        assert db.size() == 2

    def test_track_auto_timestamp(self, db):
        before = int(time.time() * 1000)
        db.track("ev", user_id=1)
        after = int(time.time() * 1000)
        events = db.filter()
        assert len(events) == 1
        assert before <= events[0]["timestamp"] <= after

    def test_track_with_properties(self, db):
        db.track("purchase", user_id=1, timestamp=T, amount=99.99, item="widget")
        events = db.filter(event_type="purchase")
        assert len(events) == 1
        assert events[0]["properties"] == {"amount": 99.99, "item": "widget"}

    def test_track_empty_type_raises(self, db):
        with pytest.raises(ValueError, match="event_type cannot be empty"):
            db.track("", user_id=1)

    def test_track_invalid_user_id_raises(self, db):
        with pytest.raises(ValueError):
            db.track("ev", user_id=-1)
        with pytest.raises(ValueError):
            db.track("ev", user_id=0)

    def test_track_negative_timestamp_raises(self, db):
        with pytest.raises(ValueError):
            db.track("ev", user_id=1, timestamp=-1)


# ── filter() ───────────────────────────────────────────────────────────────────

class TestFilter:
    @pytest.fixture(autouse=True)
    def seed(self, db):
        db.track("page_view", user_id=1, timestamp=T)
        db.track("click",     user_id=1, timestamp=T + 1000)
        db.track("page_view", user_id=2, timestamp=T + 2000)
        db.track("purchase",  user_id=2, timestamp=T + 3000)

    def test_no_filter_returns_all(self, db):
        assert len(db.filter()) == 4

    def test_filter_by_event_type(self, db):
        results = db.filter(event_type="page_view")
        assert len(results) == 2
        assert all(e["event_type"] == "page_view" for e in results)

    def test_filter_by_user_id(self, db):
        results = db.filter(user_id=1)
        assert len(results) == 2
        assert all(e["user_id"] == 1 for e in results)

    def test_filter_by_type_and_user(self, db):
        results = db.filter(event_type="page_view", user_id=2)
        assert len(results) == 1

    def test_filter_by_start_time(self, db):
        results = db.filter(start_time=T + 2000)
        assert len(results) == 2

    def test_filter_by_end_time(self, db):
        results = db.filter(end_time=T + 1000)
        assert len(results) == 2

    def test_filter_by_time_range(self, db):
        results = db.filter(start_time=T + 1000, end_time=T + 2000)
        assert len(results) == 2

    def test_filter_no_match(self, db):
        assert db.filter(event_type="nonexistent") == []

    def test_result_fields(self, db):
        events = db.filter(event_type="page_view", user_id=1)
        e = events[0]
        assert e["event_type"] == "page_view"
        assert e["user_id"] == 1
        assert e["timestamp"] == T
        assert e["properties"] is None


# ── flush() + cross-segment queries ────────────────────────────────────────────

class TestFlush:
    def test_flush_empty_is_noop(self, db):
        db.flush()  # must not raise
        assert db.size() == 0

    def test_flush_creates_segment(self, db):
        db.track("ev", user_id=1, timestamp=T)
        db.flush()
        assert db.size() == 1

    def test_query_spans_segment_and_active_block(self, db):
        db.track("ev", user_id=1, timestamp=T)
        db.flush()
        db.track("ev", user_id=2, timestamp=T + 1)
        results = db.filter(event_type="ev")
        assert len(results) == 2


# ── count_by() / event_counts() ────────────────────────────────────────────────

class TestCountBy:
    def test_count_by_event_type(self, db):
        db.track("view", user_id=1, timestamp=T)
        db.track("view", user_id=2, timestamp=T + 1)
        db.track("click", user_id=1, timestamp=T + 2)
        counts = db.count_by("event_type")
        assert counts == {"view": 2, "click": 1}

    def test_count_by_user_id(self, db):
        db.track("ev", user_id=1, timestamp=T)
        db.track("ev", user_id=1, timestamp=T + 1)
        db.track("ev", user_id=2, timestamp=T + 2)
        counts = db.count_by("user_id")
        assert counts == {1: 2, 2: 1}

    def test_count_by_with_event_type_filter(self, db):
        db.track("view", user_id=1, timestamp=T)
        db.track("click", user_id=1, timestamp=T + 1)
        counts = db.count_by("event_type", event_type="view")
        assert counts == {"view": 1}

    def test_count_by_invalid_field_raises(self, db):
        with pytest.raises(ValueError):
            db.count_by("properties")

    def test_event_counts(self, db):
        db.track("a", user_id=1, timestamp=T)
        db.track("a", user_id=2, timestamp=T + 1)
        db.track("b", user_id=1, timestamp=T + 2)
        counts = db.event_counts()
        assert counts == {"a": 2, "b": 1}

    def test_event_counts_empty(self, db):
        assert db.event_counts() == {}


# ── unique() ───────────────────────────────────────────────────────────────────

class TestUnique:
    def test_unique_users(self, db):
        db.track("view", user_id=1, timestamp=T)
        db.track("view", user_id=1, timestamp=T + 1)
        db.track("view", user_id=2, timestamp=T + 2)
        assert db.unique("user_id") == 2

    def test_unique_with_event_type_filter(self, db):
        db.track("view",  user_id=1, timestamp=T)
        db.track("click", user_id=2, timestamp=T + 1)
        assert db.unique("user_id", event_type="view") == 1


# ── first() / last() ───────────────────────────────────────────────────────────

class TestFirstLast:
    def test_first_and_last_empty(self, db):
        assert db.first() is None
        assert db.last() is None

    def test_first_returns_earliest(self, db):
        db.track("ev", user_id=2, timestamp=T + 1000)
        db.track("ev", user_id=1, timestamp=T)
        assert db.first()["timestamp"] == T

    def test_last_returns_latest(self, db):
        db.track("ev", user_id=1, timestamp=T)
        db.track("ev", user_id=2, timestamp=T + 1000)
        assert db.last()["timestamp"] == T + 1000

    def test_first_with_filter(self, db):
        db.track("view",  user_id=1, timestamp=T)
        db.track("click", user_id=1, timestamp=T + 1)
        assert db.first(event_type="click")["timestamp"] == T + 1

    def test_last_with_filter(self, db):
        db.track("view",  user_id=1, timestamp=T)
        db.track("click", user_id=1, timestamp=T + 1)
        assert db.last(event_type="view")["timestamp"] == T

    def test_first_unfiltered_uses_metadata_path(self, db_path):
        """Unfiltered first()/last() should work correctly across segment + active block."""
        with EventStore(db_path) as store:
            store.track("ev", user_id=1, timestamp=T + 5000)
            store.flush()
            store.track("ev", user_id=2, timestamp=T)  # earlier, in active block
        with EventStore(db_path) as store:
            assert store.first()["timestamp"] == T
            assert store.last()["timestamp"] == T + 5000


# ── funnel() ───────────────────────────────────────────────────────────────────

class TestFunnel:
    def test_basic_funnel(self, db):
        # user 1 completes all 3 steps
        db.track("view",     user_id=1, timestamp=T)
        db.track("click",    user_id=1, timestamp=T + 100)
        db.track("purchase", user_id=1, timestamp=T + 200)
        # user 2 only completes first 2
        db.track("view",  user_id=2, timestamp=T)
        db.track("click", user_id=2, timestamp=T + 100)

        result = db.funnel(["view", "click", "purchase"])
        assert result[0] == {"step": "view",     "users": 2, "conversion_rate": 1.0}
        assert result[1] == {"step": "click",    "users": 2, "conversion_rate": 1.0}
        assert result[2] == {"step": "purchase", "users": 1, "conversion_rate": 0.5}

    def test_funnel_empty_steps(self, db):
        assert db.funnel([]) == []

    def test_funnel_with_within(self, db):
        # user 1 completes within 1 hour
        db.track("view",     user_id=1, timestamp=T)
        db.track("purchase", user_id=1, timestamp=T + 1_000)
        # user 2 takes too long
        db.track("view",     user_id=2, timestamp=T)
        db.track("purchase", user_id=2, timestamp=T + 4_000_000)  # > 1 hour

        result = db.funnel(["view", "purchase"], within=3_600_000)
        assert result[1]["users"] == 1


# ── Regression / edge cases ────────────────────────────────────────────────────

class TestEdgeCases:
    def test_more_than_16_segments(self, db_path):
        """partition_index was allocated for 16 but declared capacity 64 — heap overflow at 17."""
        with EventStore(db_path) as store:
            for i in range(20):
                store.track('ev', user_id=1, timestamp=T + i)
                store.flush()
            assert store.size() == 20
            assert len(store.filter()) == 20

    def test_more_than_16_segments_reopen(self, db_path):
        with EventStore(db_path) as store:
            for i in range(20):
                store.track('ev', user_id=1, timestamp=T + i)
                store.flush()
        with EventStore(db_path) as store:
            assert store.size() == 20
            assert len(store.filter()) == 20

    def test_timestamp_with_0x0a_byte_wal_recovery(self, db_path):
        """WAL was opened without O_BINARY — timestamps containing 0x0A corrupted binary data."""
        # T+10 = 1_000_000_000_010 has 0x0A as the first byte of its little-endian encoding
        with EventStore(db_path) as store:
            for i in range(20):
                store.track('ev', user_id=1, timestamp=T + i)
        with EventStore(db_path) as store:
            assert store.size() == 20

    def test_timestamp_zero(self, db_path):
        with EventStore(db_path) as store:
            store.track('ev', user_id=1, timestamp=0)
            results = store.filter()
            assert len(results) == 1
            assert results[0]['timestamp'] == 0

    def test_filter_zero_bounds_returns_all(self, db_path):
        """filter(start_time=0, end_time=0) should mean 'no bounds'."""
        with EventStore(db_path) as store:
            store.track('ev', user_id=1, timestamp=T)
            store.flush()
            store.track('ev', user_id=2, timestamp=T + 1)
            results = store.filter(start_time=0, end_time=0)
            assert len(results) == 2

    def test_partition_exact_boundaries(self, db_path):
        """Partition pruning must include events exactly at min/max timestamps."""
        with EventStore(db_path) as store:
            store.track('a', user_id=1, timestamp=T)
            store.track('b', user_id=1, timestamp=T + 1000)
            store.flush()
            assert len(store.filter(start_time=T,      end_time=T))      == 1
            assert len(store.filter(start_time=T+1000, end_time=T+1000)) == 1
            assert len(store.filter(end_time=T-1))      == 0
            assert len(store.filter(start_time=T+1001)) == 0

    def test_duplicate_timestamps(self, db_path):
        with EventStore(db_path) as store:
            for i in range(100):
                store.track('ev', user_id=i+1, timestamp=T)
            results = store.filter(start_time=T, end_time=T)
            assert len(results) == 100

    def test_special_chars_in_properties(self, db_path):
        with EventStore(db_path) as store:
            store.track('ev', user_id=1, timestamp=T,
                        msg='hello "world"', path='/a/b?x=1&y=2',
                        uni='café 日本語')
            store.flush()
        with EventStore(db_path) as store:
            p = store.filter()[0]['properties']
            assert p['msg']  == 'hello "world"'
            assert p['path'] == '/a/b?x=1&y=2'
            assert p['uni']  == 'café 日本語'

    def test_large_properties_segment_roundtrip(self, db_path):
        big = 'x' * 10_000
        with EventStore(db_path) as store:
            store.track('ev', user_id=1, timestamp=T, payload=big)
            store.flush()
        with EventStore(db_path) as store:
            assert store.filter()[0]['properties']['payload'] == big

    def test_truncated_wal_recovers_intact_entries(self, db_path):
        """A crash mid-write leaves a truncated entry; earlier entries must survive."""
        import os
        with EventStore(db_path) as store:
            store.track('ev', user_id=1, timestamp=T)
            store.track('ev', user_id=2, timestamp=T + 1)
        wal_path = os.path.join(db_path, 'wal.log')
        size = os.path.getsize(wal_path)
        with open(wal_path, 'r+b') as f:
            f.truncate(size - 5)
        with EventStore(db_path) as store:
            assert store.size() >= 1

    def test_wal_recovery_500_events(self, db_path):
        """Regression: WAL with many events including 0x0A bytes must fully recover."""
        with EventStore(db_path) as store:
            for i in range(500):
                store.track('ev', user_id=1, timestamp=T + i)
        with EventStore(db_path) as store:
            assert store.size() == 500


# ── Persistence (segment rebuild on reopen) ────────────────────────────────────

class TestPersistence:
    def test_flushed_events_survive_reopen(self, db_path):
        with EventStore(db_path) as store:
            store.track("view", user_id=1, timestamp=T)
            store.track("click", user_id=2, timestamp=T + 1)
            store.flush()

        with EventStore(db_path) as store:
            assert store.size() == 2
            results = store.filter(event_type="view")
            assert len(results) == 1
            assert results[0]["user_id"] == 1

    def test_wal_events_survive_reopen(self, db_path):
        """Events in active_block (not yet flushed) are recovered via WAL."""
        with EventStore(db_path) as store:
            store.track("ev", user_id=1, timestamp=T)
            # do NOT flush — WAL must carry this

        with EventStore(db_path) as store:
            assert store.size() == 1

    def test_mixed_segment_and_wal_recovery(self, db_path):
        with EventStore(db_path) as store:
            store.track("ev", user_id=1, timestamp=T)
            store.flush()
            store.track("ev", user_id=2, timestamp=T + 1)  # in WAL only

        with EventStore(db_path) as store:
            assert store.size() == 2

    def test_properties_survive_reopen(self, db_path):
        with EventStore(db_path) as store:
            store.track("buy", user_id=1, timestamp=T, price=9.99, item="hat")
            store.flush()

        with EventStore(db_path) as store:
            events = store.filter(event_type="buy")
            assert events[0]["properties"] == {"price": 9.99, "item": "hat"}

    def test_segment_query_after_reopen(self, db_path):
        """Partition pruning must work on reopened segments."""
        with EventStore(db_path) as store:
            store.track("early", user_id=1, timestamp=T)
            store.track("late",  user_id=1, timestamp=T + 100_000)
            store.flush()

        with EventStore(db_path) as store:
            early = store.filter(event_type="early", start_time=T, end_time=T)
            assert len(early) == 1
            late = store.filter(event_type="late", start_time=T + 100_000)
            assert len(late) == 1
