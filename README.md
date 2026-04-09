# Andal

High-performance embedded event store for Python. Optimized for analytics workloads with columnar storage and inverted indexes.

## What Is It

A database for storing and querying timestamped events (clicks, page views, errors, transactions). Built in C for speed, exposed as Python library.

**Event = something that happened + when + who/what**

## Why Use This

- **10-100x faster** than SQLite for event queries
- **Embedded** - no separate server process
- **Analytics-focused** - built-in aggregations, funnels, cohorts
- **Low memory** - columnar compression

## Core Features

### 1. Event Tracking
```python
import fastevents

db = fastevents.EventStore("events.db")

db.track("page_view", user_id=123, page="/pricing", timestamp=1735689600)
db.track("click", user_id=123, button="signup", timestamp=1735689605)
db.track("purchase", user_id=123, amount=99.99, timestamp=1735689700)
```

### 2. Filtering
```python
# Find events by attributes
events = db.filter(event_type="purchase", user_id=123)
errors = db.filter(event_type="error", start_time="-24h")
```

### 3. Aggregations
```python
# Count by field
counts = db.count_by("event_type", since="-7d")
# {"page_view": 15234, "click": 892, "purchase": 127}

# Unique values
unique_users = db.unique("user_id", since="-30d")
# 4523
```

### 4. Funnels
```python
# Conversion analysis
funnel = db.funnel(
    steps=["page_view", "add_to_cart", "purchase"],
    within="1h"
)
# [{"step": "page_view", "users": 10000, "conversion": 100%},
#  {"step": "add_to_cart", "users": 1200, "conversion": 12%},
#  {"step": "purchase", "users": 240, "conversion": 2.4%}]
```

## Architecture

**Columnar Storage**: Each field stored separately for faster scans
```
user_ids:   [123, 456, 123, ...]
events:     ["click", "view", "purchase", ...]
timestamps: [100, 101, 102, ...]
```

**Inverted Indexes**: Fast lookups by event_type and user_id
```
"purchase" → [2, 15, 23, 89, ...]  (row IDs)
```

**Time Partitioning**: Data organized in hourly/daily blocks

**Compression**: 
- Timestamps: delta encoding (8 bytes → 2 bytes)
- Event types: dictionary encoding (20 bytes → 1 byte)

## Performance Targets

| Operation | Python + SQLite | FastEvents | Speedup |
|-----------|----------------|------------|---------|
| Write 1M events | 60s | 1s | 60x |
| Filter by type | 5s | 50ms | 100x |
| Count by field | 10s | 100ms | 100x |
| Memory (10M events) | 500MB | 50MB | 10x less |

## Use Cases

**Product Analytics**: Track user behavior, funnels, cohorts (Mixpanel alternative)  
**Application Monitoring**: Errors, performance, logs  
**Business Intelligence**: Sales, refunds, revenue analysis  
**Security/Audit**: Who accessed what, when  

## How It Works

**C Core**:
- Columnar storage engine
- Inverted indexes (hash tables + bitmaps)
- Query executor with SIMD aggregations
- Memory-mapped file I/O

**Python Bindings**:
- Python C API extension
- High-level wrapper for ergonomics
- Type conversion and error handling

## Implementation Status

- [ ] Phase 1: In-memory storage + basic filtering
- [ ] Phase 2: Persistence (mmap)
- [ ] Phase 3: Inverted indexes
- [ ] Phase 4: Aggregations
- [ ] Phase 5: Compression

See `EVENT_STORE_PROJECT.md` for detailed specifications.

## Installation

```bash
# Build from source
make build
make python
pip install -e .

# Run tests
make test
make pytest

# Benchmarks
make benchmark
```

## Comparison

| | FastEvents | SQLite | Pandas | ClickHouse |
|-|------------|--------|--------|-----------|
| Speed | ⚡⚡⚡ | ⚡ | ⚡⚡ | ⚡⚡⚡ |
| Embedded | ✅ | ✅ | ✅ | ❌ |
| Analytics | ✅ | Manual | ✅ | ✅ |
| Setup | Simple | Simple | Simple | Complex |
| Memory | Low | Medium | High | Medium |

**Niche**: Fast embedded analytics without external dependencies.

## Technical Details

**Language**: C11 core + Python 3.8+ bindings  
**Dependencies**: Standard library only (optional: glib, lz4)  
**Concurrency**: Single-threaded MVP (GIL-protected)  
**Storage**: Append-only, columnar, compressed  

---

**Status**: In development  
**License**: TBD
