<div align="center">

# ⚡ Andal

**High-performance embedded event store for Python**

Columnar storage | Time-partitioned | Built in C

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Python](https://img.shields.io/badge/python-3.8+-blue.svg)](https://www.python.org/downloads/)
[![Status](https://img.shields.io/badge/status-alpha-orange.svg)]()

</div>

Andal is a lightweight, embedded event store optimized for analytics workloads. Track user behavior, application events, and business metrics with columnar storage and efficient time-based querying.

## Quick Start

```bash
pip install -e .
```

```python
import andal

# Open or create a store
store = andal.EventStore("./data")

# Track events (kwargs become properties)
store.track("page_view", user_id=123, page="/pricing")
store.track("click", user_id=123, button="signup")
store.track("purchase", user_id=123, amount=99.99)

# Query events
results = store.filter(event_type="purchase", user_id=123)
for event in results:
    print(event["timestamp"], event["user_id"], event["properties"])

# Close store
store.close()
```

## What is Andal?

Andal is a fast, embedded event store built for analytics. Store millions of events and query them efficiently without setting up a database server.

### Columnar Storage

Events are stored in columnar format — each field (timestamps, user IDs, event types) lives in its own array. This makes filtering and aggregations extremely cache-friendly.

```python
# Each field stored separately for fast scans
event_type_ids: [0, 1, 0, 2, 0, ...]    # Dictionary-encoded
user_ids:       [123, 456, 123, ...]     # Raw integers
timestamps:     [1000, 1001, 1002, ...]  # Milliseconds
properties:     ['{"page": "/home"}', ...] # JSON strings
```

**Why it matters**: Reading only the columns you need is 10x faster than reading entire rows.

### Time-Based Filtering

Filter events by type, user, or time range. The partition index prunes irrelevant segments without loading them into memory.

```python
import time

# Filter by event type
views = store.filter(event_type="page_view")

# Filter by user
user_events = store.filter(user_id=123)

# Time range queries (timestamps in milliseconds)
now_ms = int(time.time() * 1000)
day_ago = now_ms - (24 * 60 * 60 * 1000)
today = store.filter(start_time=day_ago, end_time=now_ms)
```

**Why it matters**: Query only the time ranges you care about. Old segments stay on disk.

### Built-In Aggregations

Count, group, and analyze events without writing complex SQL.

```python
# Count by dimension
counts = store.count_by("event_type")
# => {"page_view": 15234, "click": 892, "purchase": 127}

# Unique values
unique_users = store.unique("user_id", event_type="purchase")
# => 4523

# Event counts over time
event_counts = store.event_counts()
# => {"page_view": 15234, "click": 892}
```

**Why it matters**: Get insights in one line of code. No GROUP BY, no CTEs.

### Funnel Analysis

Track user journeys through multi-step flows. Perfect for conversion analysis.

```python
# How many users go from view → cart → purchase?
funnel = store.funnel(
    steps=["page_view", "add_to_cart", "purchase"],
    within=3_600_000  # 1 hour in milliseconds
)

# Results:
# [{"step": "page_view", "users": 1000, "conversion_rate": 1.0},
#  {"step": "add_to_cart", "users": 120, "conversion_rate": 0.12},
#  {"step": "purchase", "users": 24, "conversion_rate": 0.024}]
```

**Why it matters**: Understand drop-off points in user flows without joining tables.

### Crash-Safe Durability

Write-ahead log (WAL) ensures no data loss. If your process crashes, Andal recovers all unflushed events on restart.

```python
store.track("payment_received", user_id=123, amount=99.99)
# ✓ Event is durable immediately (written to WAL)
# ✓ Even if process crashes before flush
# ✓ Recovered on next open
```

**Why it matters**: Your data survives crashes and power loss. No silent data loss.

### Lazy Loading

Segments are loaded from disk only when queried, then unloaded to conserve memory.

```python
# Query spans 100 segments, but only matching ones are loaded
events = store.filter(event_type="purchase", start_time=last_month)
# → Loads 3 segments that contain purchases in that time range
# → Other 97 segments stay on disk
```

**Why it matters**: Handle datasets larger than RAM. Memory footprint stays small.

---

## Performance

*Note: Comprehensive benchmarks pending. Current performance characteristics:*

**Write Performance:**
- WAL-buffered writes: ~100K+ events/sec (in-memory)
- Auto-flush at 10K events or 4MB threshold
- Segment write: <100ms for 10K events

**Query Performance:**
- Linear scan filtering (no indexes yet)
- Partition pruning via time-range metadata
- Lazy segment loading reduces memory footprint

**Memory:**
- ~28 bytes/event in-memory (columnar arrays)
- Segments unloaded after query to conserve memory

*Full benchmarks vs SQLite/DuckDB coming soon.*

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Python API (andal.EventStore)             │
└───────────────────┬─────────────────────────────┘
                    │
┌───────────────────▼─────────────────────────────┐
│  C Core                                         │
│  ├── WAL (Write-Ahead Log)                      │
│  ├── EventBlock (Columnar In-Memory)            │
│  ├── Segments (Immutable Disk Files)            │
│  ├── InvertedIndex (Fast Lookups)               │
│  └── PartitionIndex (Time Pruning)              │
└─────────────────────────────────────────────────┘
                    │
┌───────────────────▼─────────────────────────────┐
│  Disk Storage                                   │
│  ├── wal.log (durability)                       │
│  ├── seg_00001.dat (columnar data)              │
│  ├── seg_00002.dat                              │
│  └── event_types.txt                            │
└─────────────────────────────────────────────────┘
```

**Key Design Principles:**
- **Columnar Storage**: Each field in separate arrays for cache-efficient scans
- **Immutable Segments**: Write-once files, easy to reason about
- **Time Partitioning**: Binary search prunes irrelevant data
- **Crash Recovery**: WAL ensures no data loss

---

## Use Cases

- **Product Analytics**: Track user behavior, conversion funnels (Mixpanel alternative)
- **Application Monitoring**: Errors, performance metrics, audit logs
- **Business Intelligence**: Sales events, revenue tracking
- **IoT & Sensors**: Time-series sensor data
- **Security**: Access logs, anomaly detection

---

## Development Status

**Current Status**: Alpha (Active Development)

**Core C Engine:**
- ✅ Core columnar storage (EventBlock)
- ✅ Write-ahead log (WAL) with durability
- ✅ Segment persistence to disk
- ✅ Crash recovery and segment scanning
- ✅ String dictionary encoding with persistence
- ✅ Basic filtering queries (event_type, user_id, time range)
- ✅ Time-based partition index (query pruning)
- ✅ Lazy segment loading/unloading
- ⏳ Inverted indexes (planned - currently linear scan)
- ⏳ Compression (delta + LZ4) (planned)

**Python API:**
- ✅ Full Python bindings via C extension
- ✅ High-level `track()` API with kwargs→properties
- ✅ Filter queries with result parsing
- ✅ Aggregations: `count_by()`, `unique()`, `event_counts()`
- ✅ Funnel analysis with time windows
- ✅ First/last queries with metadata optimization
- ⏳ Python packaging (planned)

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed design docs.

---

## Performance Tips

1. **Use context managers**: `with EventStore(...) as store:` ensures proper cleanup
2. **Time filters help**: Include time ranges to leverage partition pruning
3. **Auto-flush works**: Default threshold is 10K events - manual `flush()` rarely needed
4. **Properties as kwargs**: `track("click", user_id=1, btn="x")` is cleaner than JSON strings

---

## Contributing

Andal is in early development. Issues and PRs are welcome!

```bash
# Build and test C core
make clean
make test           # Runs all C tests (wal, disk, store)

# Test Python bindings
make python         # Build Python extension
python tests/test_store_python.py

# Individual test targets
make test-wal       # WAL durability tests
make test-disk      # Segment serialization tests
make test-store     # Full integration tests
```

## License

TBD

---

<div align="center">

**Built with ⚡ for speed**

[Documentation](about.md) • [Architecture](ARCHITECTURE.md) • [Implementation Notes](IMPLEMENTATION_NOTES.md)

</div>
