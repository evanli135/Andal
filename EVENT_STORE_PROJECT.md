# Fast Event Store - Project Specification

## Project Overview

A high-performance embedded event store for Python applications, optimized for analytics workloads (product analytics, user behavior tracking, A/B testing data).

**Core Value Proposition:**
- 10-100x faster than SQLite for event queries
- Embedded (no separate server process)
- Columnar storage for fast filtering and aggregation
- Simple Python API

---

## Project Structure

```
Swiss/
├── src/
│   ├── fastevents.h        # Public C API
│   ├── internal.h          # Internal structs and APIs (testing only)
│   ├── wal.h               # WAL interface
│   ├── coordinator.c       # EventStore lifecycle and append logic
│   ├── disk.c              # Segment serialization / deserialization
│   ├── events.c            # EventBlock and Segment creation
│   ├── encoding.c          # WAL wire format (event_encode / event_decode)
│   ├── partition.c         # PartitionIndex (time range → segment ID)
│   ├── stringdict.c        # StringDict (event type string → uint32_t)
│   └── wal.c               # Write-ahead log
├── tests/
│   └── test_disk.c         # Disk layer tests
├── warnings.md             # Known issues and design gaps
└── Makefile
```

---

## Architecture

### Components

| Component | File | Role |
|---|---|---|
| `EventStore` | coordinator.c | Top-level coordinator; owns all subsystems |
| `WAL` | wal.c | Append-only durability log before segment flush |
| `StringDict` | stringdict.c | Hash table: event type string → compact uint32_t ID |
| `EventBlock` | events.c | In-memory columnar staging area for a batch of events |
| `Segment` | events.c / disk.c | Immutable on-disk snapshot of a flushed EventBlock |
| `PartitionIndex` | partition.c | In-memory sorted time range → segment ID index |
| Encoding | encoding.c | WAL wire format encode/decode |

### Data Lifecycle

This is the full path an event takes from `append` call to permanent storage:

```
event_store_append("click", user_id, ts, props)
        │
        ▼
  [WAL buffer]          in-memory, inside the WAL struct
  (wal->buf[])          not yet on disk — lost on crash
        │
        │  auto-flush every 10k events (or on close)
        ▼
  [wal.log]             on disk, survives crash
                        raw encoded bytes, not queryable
        │
        │  simultaneously, same event also goes to...
        ▼
  [active_block]        in-memory EventBlock
  (columnar arrays)     queryable, lost on crash (WAL recovers it)
        │
        │  event_store_flush() — when active_block is full
        ▼
  [seg_N.dat]           on disk, survives crash
                        columnar binary format, queryable
        │
        │  after successful write
        ▼
  [wal.log truncated]   WAL entries now redundant, reset to empty
                        (TODO — not yet implemented)
```

The WAL and active_block hold the **same events simultaneously** — the WAL is the durability copy, the active block is the queryable copy. On crash, `wal_recover` replays `wal.log` back into a fresh `active_block`.

### Segment Metadata vs Data

`segments[]` in the EventStore holds lightweight metadata structs — file path, time range, event count. The actual `EventBlock` data is lazy-loaded on demand via `segment_load_from_disk` and freed after the query via `segment_unload`. The `PartitionIndex` lets queries skip segments entirely via binary search on time range before any disk I/O happens.

---

## Python API (Target)

```python
import fastevents

db = fastevents.EventStore("events.db")

# Write
db.track("page_view", user_id=123, page="/pricing", timestamp=1735689600)
db.track("click",     user_id=123, button="signup", timestamp=1735689605)

# Query
events = db.filter(event_type="page_view", user_id=123, start_time="-24h")

# Aggregations
counts = db.count_by("event_type", since="-7d")
# {"page_view": 5234, "click": 892, ...}

unique_users = db.unique("user_id", event_type="purchase", since="-30d")
```

---

## C Public API

```c
// Lifecycle
EventStore* event_store_open(const char* path);
void        event_store_close(EventStore* store);

// Write
int event_store_append(EventStore* store,
                       const char* event_type,
                       uint64_t user_id,
                       uint64_t timestamp,
                       const char* properties_json);

// Flush active_block → segment file
int event_store_flush(EventStore* store);

// Query (not yet implemented)
// event_store_filter(...)
```

---

## Implementation Status

### Done
- `EventBlock` — columnar in-memory storage
- `StringDict` — string → ID hash table with save/load
- `WAL` — append, auto-flush, crash recovery replay
- `Segment` — write to disk, load from disk, atomic delete-where
- `PartitionIndex` — sorted insert, binary search query
- `event_store_open` — init all subsystems + WAL recovery
- `event_store_close` / `event_store_destroy`
- `event_store_append` — WAL + active_block write path
- `event_store_flush` — partial (writes segment, registers in index; missing WAL truncation + active_block reset)

### Not Yet Implemented
- WAL truncation after flush (`wal_truncate`)
- Active block reset after flush
- `FE_CAPACITY_EXCEEDED` retry loop in `event_store_append`
- `event_store_filter` — query path
- Python C extension bindings

---

## Performance Targets

| Metric | Target |
|---|---|
| Write throughput | 1M events/sec (single-threaded) |
| Linear scan | 100M events/sec |
| Index lookup | 1M matching events in <10ms |
| count_by on 10M events | <50ms |
| Memory per event | ~50 bytes in-memory, ~20 bytes on-disk |

---

## Dependencies

- **C**: Standard C11, POSIX (compiled with GCC 13.2 via MSYS2/ucrt64)
- **Python**: Python 3.8+, setuptools
- **Optional**: lz4 (compression, future)
