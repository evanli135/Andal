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

### Up and running in 30 seconds

No Postgres to spin up. No Docker container. No connection string to configure. Open a file path and start tracking — Andal creates everything it needs on first run.

Works in a Jupyter notebook, a Flask app, a CLI script, a Lambda function. Anywhere Python runs, Andal runs.

</td>
<td width="50%">

```python
from andal import EventStore

# This is the entire setup.
store = EventStore("./my_app")

store.track("signup", user_id=1)
store.track("purchase", user_id=1, amount=49.99)
store.track("churn", user_id=2)

# That's it. Data is on disk.
```

</td>
</tr>
</table>

<table>
<tr>
<td width="50%">

### Analytics without SQL

No GROUP BY. No CTEs. No JOINs. Answer the questions you actually care about — conversion rates, user counts, event breakdowns — in one line of Python.

Built-in funnel analysis, aggregations, and filtering. The queries you'd spend an afternoon writing in SQL are one method call.

</td>
<td width="50%">

```python
# Who's converting?
store.funnel(["page_view", "signup", "purchase"])
# page_view  → 10,000 users
# signup     → 1,200  (12%)
# purchase   → 240    (2.4%)

# What's happening right now?
store.count_by("event_type", since="24h")
# {"page_view": 8432, "signup": 91}

# How many unique users this week?
store.unique("user_id", since="7d")
# 4,891
```

</td>
</tr>
</table>

<table>
<tr>
<td width="50%">

### Built for events, not rows

Traditional databases store the current state of things. Andal stores what *happened* — and never throws it away. Every event is timestamped, immutable, and queryable forever.

Ask questions you can't ask a regular database: "What did this user do before churning?" "When did we first see this error?" "What's the pattern leading up to a purchase?"

</td>
<td width="50%">

```python
# Replay everything a user ever did
store.filter(user_id=123)

# Find all errors in the last hour
store.filter(event_type="error", since="1h")

# What happened before this crash?
store.filter(
    user_id=123,
    end_time=crash_timestamp
)
# Full history. Nothing deleted.
```

</td>
</tr>
</table>

<table>
<tr>
<td width="50%">

### Your data survives crashes

Andal writes events to a write-ahead log before anything else. If your process dies mid-write, the data isn't lost — it's recovered automatically the next time you open the store.

No silent data loss. No corrupt state. Just open the store and keep going.

</td>
<td width="50%">

```python
store.track("payment_confirmed", 
            order_id="ORD-123",
            amount=99.99)

# Process crashes here.

# On restart:
store = EventStore("./my_app")
# ✓ payment_confirmed recovered
# ✓ No data loss
# ✓ No manual recovery needed
```

</td>
</tr>
</table>

<table>
<tr>
<td width="50%">

### Scales with your data, not your ops budget

Query millions of events without loading them all into memory. Time-range queries skip over months of old data instantly. Results come back fast whether you have 10,000 events or 10,000,000.

No infrastructure to scale. No cluster to manage. Just a directory of files that grows as your data grows.

</td>
<td width="50%">

```python
# Fast even at scale
store.filter(
    event_type="purchase",
    since="30d"       # skips older data entirely
)

# Works on a laptop
# Works in a Lambda with 128MB RAM
# Works with years of event history

# No infra changes as you grow.
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

[Documentation](about.md) • [Architecture](ARCHITECTURE.md) • [Implementation Notes](IMPLEMENTATION_NOTES.md)

</div>
