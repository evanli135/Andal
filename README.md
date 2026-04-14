<div align="center">

# ⚡ Andal

**High-performance embedded event store for Python**

Columnar storage | Time-partitioned | Built in C

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Python](https://img.shields.io/badge/python-3.8+-blue.svg)](https://www.python.org/downloads/)
[![Status](https://img.shields.io/badge/status-alpha-orange.svg)]()

[Quick Start](#quick-start) • [Features](#features) • [Benchmarks](#benchmarks) • [Documentation](about.md)

</div>

---

## What is Andal?

Andal is a lightweight, embedded event store optimized for analytics workloads. Track user behavior, application events, and business metrics with columnar storage and efficient time-based querying.

```python
import andal

# Open or create a store
store = andal.EventStore("./data")

# Track events
store.track("page_view", user_id=123, page="/pricing")
store.track("click", user_id=123, button="signup")
store.track("purchase", user_id=123, amount=99.99)

# Query events
results = store.filter(event_type="purchase", user_id=123)
```

## Why Andal?

<table>
<tr>
<td width="33%">

### ⚡ **Blazing Fast**
Optimized for analytics workloads. Columnar storage and time-based partitioning for efficient scans.

</td>
<td width="33%">

### 📦 **Embedded**
No separate server process. No configuration. Just import and use. Perfect for applications, scripts, and notebooks.

</td>
<td width="33%">

### 🎯 **Analytics-First**
Built-in aggregations, time-series queries, and event sequencing. Stop fighting with SQL GROUP BY.

</td>
</tr>
</table>

## Quick Start

### Installation

```bash
# Install from source
pip install -e .
```

### Basic Usage

```python
import andal
from datetime import datetime

# Create store
store = andal.EventStore("./my_events")

# Track events (kwargs become properties)
store.track(
    event_type="user_signup",
    user_id=12345,
    timestamp=int(datetime.now().timestamp() * 1000),
    source="landing_page",
    plan="pro"
)

# Query with filters
recent_signups = store.filter(
    event_type="user_signup",
    start_time=int((datetime.now().timestamp() - 86400) * 1000)  # 24h ago
)

# Close store
store.close()
```

## Features

### 🔍 **Fast Filtering**
```python
# Filter by event type
views = store.filter(event_type="page_view")

# Filter by user
user_events = store.filter(user_id=123)

# Time range queries (timestamps in milliseconds)
import time
now_ms = int(time.time() * 1000)
day_ago = now_ms - (24 * 60 * 60 * 1000)
today = store.filter(start_time=day_ago, end_time=now_ms)
```

### 📊 **Aggregations**
```python
# Count by dimension
counts = store.count_by("event_type")
# => {"page_view": 15234, "click": 892, "purchase": 127}

# Unique values
unique_users = store.unique("user_id", event_type="purchase")
# => 4523

# Event counts
event_counts = store.event_counts()
# => {"page_view": 15234, "click": 892}
```

### 🔄 **Funnel Analysis**
```python
# Conversion funnels
funnel = store.funnel(
    steps=["page_view", "add_to_cart", "purchase"],
    within=3_600_000  # 1 hour in milliseconds
)
# => [{"step": "page_view", "users": 1000, "conversion_rate": 1.0},
#     {"step": "add_to_cart", "users": 120, "conversion_rate": 0.12},
#     {"step": "purchase", "users": 24, "conversion_rate": 0.024}]
```

### 🎯 **First/Last Queries**
```python
# Get first/last events efficiently
first_event = store.first()  # O(segments), not O(events)
last_event = store.last()
first_purchase = store.first(event_type="purchase", user_id=123)
```

### 💾 **Durable & Crash-Safe**
- Write-ahead log (WAL) for durability
- Automatic recovery on restart
- No data loss on crashes

## Benchmarks

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

## Use Cases

- **Product Analytics**: Track user behavior, conversion funnels (Mixpanel alternative)
- **Application Monitoring**: Errors, performance metrics, audit logs
- **Business Intelligence**: Sales events, revenue tracking
- **IoT & Sensors**: Time-series sensor data
- **Security**: Access logs, anomaly detection

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

## Performance Tips

1. **Use context managers**: `with EventStore(...) as store:` ensures proper cleanup
2. **Time filters help**: Include time ranges to leverage partition pruning
3. **Auto-flush works**: Default threshold is 10K events - manual `flush()` rarely needed
4. **Properties as kwargs**: `track("click", user_id=1, btn="x")` is cleaner than JSON strings

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
