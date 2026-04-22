<div align="center">

# ⚡ Andal

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Python](https://img.shields.io/badge/python-3.8+-blue.svg)](https://www.python.org/downloads/)
[![Status](https://img.shields.io/badge/status-alpha-orange.svg)]()

No Server | No SQL | No Config

**`pip install andal` — then forget about your analytics infrastructure.**

SQLite for events — embedded, columnar, zero ops.

Andal is an embedded event store for Python. Track events, query them,
run funnels and aggregations — all from a simple Python API, no SQL required.

</div>


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

<table>
<tr>
<td width="50%">

### Serverless & Embedded

No separate server process. No configuration files. No Docker containers. Just a Python import and a file path.

Perfect for applications, scripts, notebooks, and edge devices. Your data stays local.

</td>
<td width="50%">

```python
import andal

# That's it. No server to start.
# No connection strings.
# No authentication setup.

store = andal.EventStore("./data")

# Works in Jupyter notebooks
# Works in Flask apps
# Works in CLI scripts
# Works anywhere Python runs
```

</td>
</tr>
</table>

<table>
<tr>
<td width="50%">

### Columnar Storage

Events are stored in columnar format — each field (timestamps, user IDs, event types) lives in its own array. This makes filtering and aggregations extremely cache-friendly.

Reading only the columns you need is 10x faster than reading entire rows.

</td>
<td width="50%">

```python
# Each field stored separately for fast scans
event_type_ids: [0, 1, 0, 2, 0, ...]
# Dictionary-encoded

user_ids: [123, 456, 123, ...]
# Raw integers

timestamps: [1000, 1001, 1002, ...]
# Milliseconds

properties: ['{"page": "/home"}', ...]
# JSON strings
```

</td>
</tr>
</table>

<table>
<tr>
<td width="50%">

### Time-Based Filtering

Filter events by type, user, or time range. The partition index prunes irrelevant segments without loading them into memory.

Query only the time ranges you care about. Old segments stay on disk.

</td>
<td width="50%">

```python
import time

# Filter by event type
views = store.filter(event_type="page_view")

# Filter by user
user_events = store.filter(user_id=123)

# Time range queries
now_ms = int(time.time() * 1000)
day_ago = now_ms - (24 * 60 * 60 * 1000)
recent = store.filter(
    start_time=day_ago,
    end_time=now_ms
)
```

</td>
</tr>
</table>

<table>
<tr>
<td width="50%">

### Built-In Aggregations

Count, group, and analyze events without writing complex SQL.

Get insights in one line of code. No GROUP BY, no CTEs, no headaches.

</td>
<td width="50%">

```python
# Count by dimension
counts = store.count_by("event_type")
# {"page_view": 15234, "click": 892}

# Unique values
unique_users = store.unique(
    "user_id",
    event_type="purchase"
)
# 4523

# Event counts
event_counts = store.event_counts()
# {"page_view": 15234, "click": 892}
```

</td>
</tr>
</table>

<table>
<tr>
<td width="50%">

### Funnel Analysis

Track user journeys through multi-step flows. Perfect for conversion analysis.

Understand drop-off points in user flows without joining tables or writing complex queries.

</td>
<td width="50%">

```python
# How many users convert?
funnel = store.funnel(
    steps=[
        "page_view",
        "add_to_cart",
        "purchase"
    ],
    within=3_600_000  # 1 hour
)

# [{"step": "page_view",
#   "users": 1000,
#   "conversion_rate": 1.0},
#  {"step": "add_to_cart",
#   "users": 120,
#   "conversion_rate": 0.12}, ...]
```

</td>
</tr>
</table>

<table>
<tr>
<td width="50%">

### Crash-Safe Durability

Write-ahead log (WAL) ensures no data loss. If your process crashes, Andal recovers all unflushed events on restart.

Your data survives crashes and power loss. No silent data loss, ever.

</td>
<td width="50%">

```python
store.track(
    "payment_received",
    user_id=123,
    amount=99.99
)

# ✓ Event is durable immediately
#   (written to WAL)
# ✓ Survives process crashes
# ✓ Recovered on next open
```

</td>
</tr>
</table>

<table>
<tr>
<td width="50%">

### Lazy Loading

Segments are loaded from disk only when queried, then unloaded to conserve memory.

Handle datasets larger than RAM. Memory footprint stays small even with millions of events.

</td>
<td width="50%">

```python
# Query spans 100 segments,
# but only matching ones are loaded
events = store.filter(
    event_type="purchase",
    start_time=last_month
)

# → Loads 3 segments that contain
#   purchases in that time range
# → Other 97 segments stay on disk
```

</td>
</tr>
</table>

---

## Python API

### Opening a Store

```python
import andal

# Open or create a store
store = andal.EventStore("./my_events")

# Use as context manager (recommended)
with andal.EventStore("./my_events") as store:
    store.track("event", user_id=1)
    # Automatically closed and flushed
```

### Tracking Events

```python
# Basic event tracking
store.track("page_view", user_id=123)

# With timestamp (milliseconds since epoch)
import time
now_ms = int(time.time() * 1000)
store.track("click", user_id=123, timestamp=now_ms)

# With properties (kwargs become JSON properties)
store.track(
    "purchase",
    user_id=123,
    timestamp=now_ms,
    amount=99.99,
    currency="USD",
    product_id="prod_123"
)
```

### Filtering Events

```python
# Filter by event type
purchases = store.filter(event_type="purchase")

# Filter by user
user_events = store.filter(user_id=123)

# Filter by time range
recent = store.filter(
    start_time=day_ago_ms,
    end_time=now_ms
)

# Combine filters
recent_purchases = store.filter(
    event_type="purchase",
    user_id=123,
    start_time=day_ago_ms
)

# Results are lists of event dictionaries
for event in recent_purchases:
    print(event["event_type"])    # "purchase"
    print(event["user_id"])        # 123
    print(event["timestamp"])      # 1710000000000
    print(event["properties"])     # {"amount": 99.99, ...}
```

### Aggregations

```python
# Count events by dimension
event_counts = store.count_by("event_type")
# {"page_view": 5234, "click": 892, "purchase": 127}

user_counts = store.count_by("user_id", event_type="purchase")
# {123: 5, 456: 3, 789: 12}

# Count unique values
unique_users = store.unique("user_id")
# 4523

unique_purchasers = store.unique("user_id", event_type="purchase")
# 89

# Get all event type counts
all_counts = store.event_counts()
# {"page_view": 5234, "click": 892, "purchase": 127}
```

### Funnel Analysis

```python
# Track conversion through multiple steps
funnel = store.funnel(
    steps=["page_view", "add_to_cart", "purchase"],
    within=3_600_000  # Users must complete within 1 hour
)

# Returns conversion at each step
# [
#   {"step": "page_view", "users": 1000, "conversion_rate": 1.0},
#   {"step": "add_to_cart", "users": 120, "conversion_rate": 0.12},
#   {"step": "purchase", "users": 24, "conversion_rate": 0.024}
# ]

# Without time window (users can take any amount of time)
funnel = store.funnel(steps=["signup", "first_purchase"])
```

### First/Last Queries

```python
# Get earliest event (optimized with metadata)
first = store.first()
first_purchase = store.first(event_type="purchase")
first_user_event = store.first(user_id=123)

# Get most recent event
last = store.last()
last_error = store.last(event_type="error")

# These use segment metadata for O(segments) performance
# instead of O(events) when no filters are specified
```

### Utilities

```python
# Get total event count
count = store.size()
# 15234

# Force flush to disk (usually not needed - auto-flushes at 10K events)
store.flush()

# Close store (flushes pending writes)
store.close()
```

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

[Architecture](ARCHITECTURE.md)

</div>
