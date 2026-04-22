# Andal Architecture

## Overview

Andal is a layered embedded event store. Each layer has a single responsibility; the coordinator wires them together.

```
┌─────────────────────────────────────────┐
│  Python API  (andal.EventStore)         │
│  andal/store.py + src/fastevents_module │
└───────────────────┬─────────────────────┘
                    │
┌───────────────────▼─────────────────────┐
│  Coordinator  (src/coordinator.c)       │
│  EventStore — owns all layers below     │
└──┬──────────┬──────────┬───────────────┘
   │          │          │
┌──▼──┐  ┌───▼───┐  ┌───▼──────────────┐
│ WAL │  │ Disk  │  │ PartitionIndex   │
│     │  │Segments│  │ time → seg IDs   │
└──┬──┘  └───┬───┘  └──────────────────┘
   │          │
┌──▼──────────▼──────────────────────────┐
│  EventBlock  (columnar in-memory)      │
│  event_type_ids / user_ids /           │
│  timestamps / properties               │
└────────────────────────────────────────┘
```

## Components

### WAL — Write-Ahead Log (`src/wal.c`)

Every append lands here first. A 4 MB in-memory buffer flushed to `wal.log` via `write() + fdatasync()`. Length-prefixed entries (4-byte header + payload). On open, `wal_recover()` replays any entries that never made it into a segment before the last crash.

Flush triggers: 10,000 events, 4 MB buffer, or 1,000 ms timer — whichever comes first.

### EventBlock — Columnar In-Memory Storage (`src/events.c`)

The active write buffer and the in-memory representation of a segment. Each field is a separate heap array:

```c
uint32_t* event_type_ids   // dictionary-encoded (4 bytes → 1–2 bytes on disk)
uint64_t* user_ids
uint64_t* timestamps
char**    properties        // heap-allocated JSON strings
```

Tracks `min_timestamp`, `max_timestamp`, and `estimated_bytes` for flush decisions.

### StringDict — Event Type Encoding (`src/stringdict.c`)

Hash table mapping event type strings (e.g. `"page_view"`) to compact `uint32_t` IDs. djb2 hash, open addressing, linear probing. Persisted to `event_types.txt` and reloaded on open so IDs are stable across sessions.

### Segments — Immutable Disk Files (`src/disk.c`)

When the active EventBlock is flushed, it becomes an immutable segment file (`seg_00001.dat`, `seg_00002.dat`, …). The binary format:

```
SegmentHeader (fixed-size)
  magic[8]           EVTSEG\0\0
  version            uint32
  event_count        uint64
  min/max_timestamp  uint64
  column offsets     uint64 × 5

Column data (variable)
  event_type_ids     uint32[] raw
  user_ids           uint64[] raw
  timestamps         delta + LEB128 varint encoded
  properties         index (uint32 offsets) + heap (concatenated strings)
```

Segments are **lazy-loaded**: `is_loaded = false` until a query touches them, then unloaded again after the scan. This keeps memory flat regardless of how many segments exist on disk.

`segment_peek_metadata()` reads only the header to populate `min_timestamp`, `max_timestamp`, and `event_count` without loading event data — used during startup to rebuild the partition index cheaply.

### PartitionIndex — Time Pruning (`src/partition.c`)

Sorted array of `(min_ts, max_ts, segment_id)` entries. `partition_index_query(start, end)` returns only the segment IDs whose time range overlaps the query window. This lets time-range queries skip irrelevant segments entirely without loading them.

### Coordinator (`src/coordinator.c`)

Owns all components and implements the write and read paths.

**Write path:**
```
event_store_append(event_type, user_id, ts, props)
  1. StringDict: resolve/assign type_id, persist if new
  2. Encode event → binary blob
  3. wal_append()  ← durable before in-memory
  4. append_to_block(active_block)
  5. should_flush()? → event_store_flush()
```

**Flush path:**
```
event_store_flush()
  1. segment_write_to_disk()  ← write before registering
  2. grow_segments_array() if needed
  3. register_segment()       ← add to segments[] + partition_idx
  4. wal_truncate()           ← WAL entries now redundant
  5. fresh active_block
```

**Read path:**
```
event_store_filter(event_type, user_id, start_ts, end_ts)
  1. partition_index_query() → candidate segment IDs
  2. for each segment: lazy-load → scan → unload
  3. scan active_block
  4. return QueryResult
```

**Startup:**
```
event_store_open(path)
  1. mkdir (no-op if exists)
  2. open WAL, load StringDict
  3. scan_and_register_segments()  ← discover seg_*.dat files
  4. segment_peek_metadata() for each → rebuild PartitionIndex
  5. wal_recover() → replay events into active_block
```

## On-disk Layout

```
<db_path>/
  wal.log           write-ahead log (truncated after each flush)
  event_types.txt   string dictionary (one type per line)
  seg_00001.dat     columnar segment
  seg_00002.dat
  ...
```

## Python Binding (`src/fastevents_module.c`)

A CPython C extension (`andal._andal`). `PyEventStore` wraps a `EventStore*` behind `PyObject_HEAD`. Arguments are marshalled with `PyArg_ParseTupleAndKeywords`; results with `PyDict_New` / `PyList_Append`. `PyInit__andal` is the module entry point.

The high-level `andal.EventStore` Python class (`andal/store.py`) wraps the C extension with `track()`, `filter()`, `count_by()`, `unique()`, `funnel()`, and `first()`/`last()`.

## Design Decisions

| Decision | Rationale |
|---|---|
| WAL before in-memory | Durability — crash after WAL write loses nothing |
| Write disk before registering segment | Safe crash window: orphaned file is harmless, orphaned index entry is not |
| Immutable segments | Simple to reason about; no partial-write corruption risk |
| Lazy segment loading | Memory stays flat with large histories |
| Columnar arrays | Cache-efficient scans; only load the columns a query needs |
| Dictionary-encoded event types | Reduces per-event storage from ~20 bytes to 4 bytes |
| Delta + LEB128 timestamps | Sorted timestamps compress well; typical delta fits in 1–2 bytes |
