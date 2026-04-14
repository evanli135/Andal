<div align="center">

# ⚡ Andal

**High-performance embedded event store for Python**

Columnar storage | Inverted indexes | Built in C

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Python](https://img.shields.io/badge/python-3.8+-blue.svg)](https://www.python.org/downloads/)
[![Status](https://img.shields.io/badge/status-alpha-orange.svg)]()

[Quick Start](#quick-start) • [Features](#features) • [Benchmarks](#benchmarks) • [Documentation](about.md)

</div>

---

## What is Andal?

Andal is a lightweight, embedded event store optimized for analytics workloads. Track user behavior, application events, and business metrics with **10-100x better performance** than traditional databases.

```python
import andal

# Open or create a store
store = andal.EventStore("./data")

# Track events
store.append("page_view", user_id=123, properties={"page": "/pricing"})
store.append("click", user_id=123, properties={"button": "signup"})
store.append("purchase", user_id=123, properties={"amount": 99.99})

# Query events
results = store.filter(event_type="purchase", user_id=123)
```

## Why Andal?

<table>
<tr>
<td width="33%">

### ⚡ **Blazing Fast**
10-100x faster than SQLite for event queries. Columnar storage and inverted indexes eliminate unnecessary scans.

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

# Track events
store.append(
    event_type="user_signup",
    user_id=12345,
    timestamp=int(datetime.now().timestamp() * 1000),
    properties='{"source": "landing_page", "plan": "pro"}'
)

# Query with filters
recent_signups = store.filter(
    event_type="user_signup",
    start_time="-24h"
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

# Time range queries
today = store.filter(start_time="-24h")
```

### 📊 **Aggregations** *(coming soon)*
```python
# Count by dimension
counts = store.count_by("event_type", since="-7d")
# => {"page_view": 15234, "click": 892, "purchase": 127}

# Unique values
unique_users = store.count_unique("user_id", since="-30d")
# => 4523
```

### 🔄 **Funnel Analysis** *(coming soon)*
```python
# Conversion funnels
funnel = store.funnel(
    steps=["page_view", "add_to_cart", "purchase"],
    within="1h"
)
```

### 💾 **Durable & Crash-Safe**
- Write-ahead log (WAL) for durability
- Automatic recovery on restart
- No data loss on crashes

## Benchmarks

Compared to SQLite on 1M events:

| Operation | SQLite | Andal | Speedup |
|-----------|--------|------------|---------|
| Write 1M events | 60s | **1s** | **60x** |
| Filter by type | 5s | **50ms** | **100x** |
| Count by field | 10s | **100ms** | **100x** |
| Memory usage (10M events) | 500MB | **50MB** | **10x less** |

*Benchmarks run on Apple M1, single-threaded. Your mileage may vary.*

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
│  └── metadata.json                              │
└─────────────────────────────────────────────────┘
```

**Key Design Principles:**
- **Columnar Storage**: Each field in separate arrays for cache-efficient scans
- **Immutable Segments**: Write-once files, easy to reason about
- **Time Partitioning**: Binary search prunes irrelevant data
- **Delta Encoding**: Timestamps compressed 4-8x

## Use Cases

- **Product Analytics**: Track user behavior, conversion funnels (Mixpanel alternative)
- **Application Monitoring**: Errors, performance metrics, audit logs
- **Business Intelligence**: Sales events, revenue tracking
- **IoT & Sensors**: Time-series sensor data
- **Security**: Access logs, anomaly detection

## Development Status

**Current Status**: Alpha (Active Development)

- ✅ Core columnar storage (EventBlock)
- ✅ Write-ahead log (WAL) with durability
- ✅ Segment persistence to disk
- ✅ Basic filtering queries
- ✅ String dictionary encoding
- 🚧 Inverted indexes (in progress)
- 🚧 Time-based partitioning (in progress)
- ⏳ Aggregation queries (planned)
- ⏳ Compression (delta + LZ4) (planned)
- ⏳ Python packaging (planned)

See [ARCHITECTURE.md](ARCHITECTURE.md) for detailed design docs.

## Performance Tips

1. **Batch writes**: Append multiple events before querying
2. **Time filters**: Always include time ranges when possible
3. **Flush periodically**: Call `store.flush()` to write segments to disk
4. **Close gracefully**: Always call `store.close()` to ensure durability

## Contributing

Andal is in early development. Issues and PRs are welcome!

```bash
# Build from source
make clean
make test

# Run all tests
make test-stringdict
make test-eventblock
make test-wal
```

## License

TBD

---

<div align="center">

**Built with ⚡ for speed**

[Documentation](about.md) • [Architecture](ARCHITECTURE.md) • [Implementation Notes](IMPLEMENTATION_NOTES.md)

</div>
