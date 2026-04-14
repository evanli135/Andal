# Andal

High-performance embedded event store for Python. Optimized for analytics workloads with columnar storage.

## What Is It

A database for storing and querying timestamped events (clicks, page views, purchases, errors). Built in C for speed, exposed as a Python library. No server process — just import and go.

**Event = something that happened + when + who**

## Why Use This

- **Embedded** — no separate server process, works like SQLite
- **Analytics-focused** — built-in aggregations, funnels, cohort queries
- **Fast** — columnar C core, WAL-backed durability

## Usage

### Tracking Events

```python
from fastevents.store import EventStore

db = EventStore("./events")

db.track("page_view", user_id=123, page="/pricing", timestamp=1735689600000)
db.track("click",     user_id=123, button="signup", timestamp=1735689605000)
db.track("purchase",  user_id=123, amount=99.99,    timestamp=1735689700000)
```

Properties are arbitrary keyword arguments, stored as JSON.  
`timestamp` defaults to now (milliseconds) if omitted.

### Context Manager

```python
with EventStore("./events") as db:
    db.track("click", user_id=1)
    # auto-closed on exit
```

### Filtering

```python
# By event type
views = db.filter(event_type="page_view")

# By user
user_events = db.filter(user_id=123)

# By time range (ms timestamps)
recent = db.filter(start_time=1735689600000, end_time=1735690000000)

# Combined
results = db.filter(event_type="purchase", user_id=123)
```

Each result is a dict: `{event_type, user_id, timestamp, properties}`.  
`properties` is parsed back to a dict (or `None`).

### Aggregations

```python
# Count events by type
db.event_counts()
# {"page_view": 15234, "click": 892, "purchase": 127}

# Count by field
db.count_by("event_type")
db.count_by("user_id", event_type="purchase")

# Unique user count
db.unique("user_id")
db.unique("user_id", event_type="purchase", start_time=..., end_time=...)
```

### First / Last

```python
# Earliest and most recent events (efficient — no full scan when unfiltered)
db.first()
db.last()

# With filters (full scan)
db.first(event_type="purchase")
db.last(user_id=123)
```

### Conversion Funnel

```python
result = db.funnel(
    steps=["page_view", "click", "purchase"]
)
# [{"step": "page_view", "users": 1000, "conversion_rate": 1.0},
#  {"step": "click",     "users":  450, "conversion_rate": 0.45},
#  {"step": "purchase",  "users":   89, "conversion_rate": 0.089}]
```

With a time window — users must complete the full funnel within `within` ms:

```python
db.funnel(
    steps=["page_view", "click", "purchase"],
    within=3_600_000  # 1 hour in ms
)
```

### Utilities

```python
db.size()    # total event count
db.flush()   # force active buffer → segment file on disk
db.close()   # flush + release resources
```

---

## Architecture

```
event_store_open()
        │
        ├── WAL (wal.log)         append-only durability log
        ├── active_block          in-memory columnar staging area
        ├── segments[]            immutable .dat files (flushed batches)
        ├── partition_idx         time range → segment ID (binary search)
        └── event_dict            event type string → uint32 ID
```

**Write path**: `track()` → WAL buffer → `wal.log` (fdatasync) + `active_block`  
**Flush path**: `active_block` → `seg_N.dat` → WAL truncated → fresh block  
**Crash recovery**: `wal_recover()` replays `wal.log` back into `active_block` on open  
**Query path**: partition prune → lazy load segments → scan → scan active block

---

## Complexity

| Method | Time | Notes |
|--------|------|-------|
| `track()` | O(1) amortized | WAL write + block append; occasional O(B) flush |
| `flush()` | O(B) | Serialize active block to segment file |
| `filter()` with time range | O(k×B) | k = segments overlapping the time window |
| `filter()` without time range | O(E) | Full scan all segments + active block |
| `count_by()` / `unique()` | O(E) | Calls `filter()` |
| `event_counts()` | O(E) | Calls `count_by()` |
| `first()` / `last()` (unfiltered) | O(S + B) | Segment metadata scan + one segment load |
| `first()` / `last()` (filtered) | O(E) | Full scan |
| `size()` | O(S) | Sums cached per-segment counts |
| `funnel(steps)` | O(n×E) | n = number of funnel steps |

**Symbols**: E = total events, S = number of segments, B = events per segment (~10 k), k = segments matching time range

---

## Building

```bash
# Run C tests
make test

# Build Python extension (.pyd)
make python
```

---

## Implementation Status

- [x] WAL with crash recovery
- [x] Columnar segment files
- [x] String dictionary persistence
- [x] Partition index (time-range pruning)
- [x] Full query path (`filter`, `count_by`, `unique`, `first`, `last`)
- [x] Conversion funnel analysis
- [x] Python C extension (`_fastevents`)
- [ ] Inverted index (currently linear scan)
- [ ] Python bindings: `delete_where`
- [ ] Compression (delta encoding, LZ4)
- [ ] Concurrent reads

---

**Language**: C11 core + Python 3.8+ bindings  
**Dependencies**: Standard library only
