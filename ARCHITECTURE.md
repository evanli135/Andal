# FastEvents Architecture

## Overview

Lightweight embedded event store with 5-layer architecture optimized for analytics workloads.

## Layer Architecture

```
┌──────────────────────────────────────────────────────────┐
│  Layer 5: PartitionIndex                                 │
│  Time range → Segment ID mapping (binary search)         │
└────────────────────────┬─────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────┐
│  Layer 4: InvertedIndex                                  │
│  Event type → Row IDs (per segment)                      │
└────────────────────────┬─────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────┐
│  Layer 2/3: Columnar Segments                            │
│  Immutable columnar files on disk                        │
└────────────────────────┬─────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────┐
│  Layer 1: WAL (Write-Ahead Log)                          │
│  Sequential append-only buffer → disk                    │
└──────────────────────────────────────────────────────────┘
```

## Component Status

### ✅ Implemented

1. **StringDict** (`src/stringdict.c`)
   - Hash table with djb2 hash
   - Open addressing, linear probing
   - Power-of-2 capacity
   - Event type → ID encoding

2. **EventBlock** (`src/eventstore.c`)
   - Columnar in-memory storage
   - Separate arrays per field
   - Dynamic growth
   - `append_to_block()` working

3. **WAL** (`src/wal.c`, `src/wal.h`)
   - 4MB buffer with auto-flush
   - Length-prefixed entries
   - `fdatasync()` for durability
   - Crash recovery via `wal_recover()`
   - Flush at 10K events

### 🔲 TODO

4. **Segment** (wrapper around EventBlock)
   - Write EventBlock to disk
   - Lazy load/unload
   - Metadata tracking

5. **InvertedIndex** (event type → rows)
   - Build from EventBlock
   - Write/load from .idx files
   - O(1) event type filtering

6. **PartitionIndex** (time → segments)
   - Binary search pruning
   - In-memory sorted array
   - Rebuild on startup

7. **EventStore** (coordinator)
   - Owns all layers
   - Write path: append → WAL → flush → Segment
   - Read path: query → prune → load → filter

## File Layout

```
store/
├── wal.log              ← Active write-ahead log
├── segments/
│   ├── seg_00001.dat    ← Columnar event data
│   ├── seg_00001.idx    ← Inverted index
│   ├── seg_00002.dat
│   └── seg_00002.idx
└── metadata.json        ← PartitionIndex + StringDict
```

## Data Structures

### StringDict
```c
typedef struct {
    char** strings;      // Hash table (NULL = empty)
    uint32_t* ids;       // IDs for each slot
    size_t count;
    size_t capacity;     // Power of 2
} StringDict;
```

### EventBlock
```c
typedef struct {
    size_t count;
    size_t capacity;
    uint32_t* event_type_ids;  // Dictionary-encoded
    uint64_t* user_ids;
    uint64_t* timestamps;
    char** properties;         // JSON strings
    uint64_t min_timestamp;
    uint64_t max_timestamp;
} EventBlock;
```

### WAL
```c
typedef struct {
    int fd;
    uint8_t* buffer;        // 4MB staging buffer
    size_t buf_len;         // Write cursor
    size_t buf_capacity;
    size_t event_count;     // Events since last flush
    size_t flush_threshold; // Flush at 10K events
    char* path;
} WAL;
```

### Segment
```c
typedef struct {
    uint64_t segment_id;
    EventBlock* block;      // NULL if unloaded
    char* file_path;
    bool is_loaded;
    uint64_t min_timestamp;
    uint64_t max_timestamp;
    size_t event_count;
} Segment;
```

### EventStore
```c
typedef struct {
    char* db_path;
    void* wal;              // WAL* (opaque)
    StringDict* event_dict;
    Segment** segments;
    size_t num_segments;
    PartitionIndex* partition_idx;
    EventBlock* active_block;
} EventStore;
```

## Write Path

```
event_store_append("page_view", user=123, ...)
    ↓
1. StringDict: "page_view" → ID 0
    ↓
2. Serialize to WAL buffer: [len|type_id|user|ts|props]
    ↓
3. Auto-flush at threshold (10K events)
    ↓
wal_flush_to_disk()
    ↓
4. write() + fdatasync() → durability
    ↓
5. Deserialize → EventBlock
    ↓
6. Write segment to disk:
   - seg_00001.dat (columnar arrays)
   - seg_00001.idx (inverted index)
    ↓
7. Update PartitionIndex
```

## Read Path

```
event_store_filter(event_type="page_view", start_time=...)
    ↓
1. PartitionIndex: binary search time range → [seg_1, seg_5, seg_9]
    ↓
2. For each segment:
   - Load from disk if needed
   - InvertedIndex: "page_view" → [row_2, row_5, row_10, ...]
   - Filter by other criteria (user_id, timestamp)
    ↓
3. Flatten results from all segments
    ↓
4. Return QueryResult
```

## Configuration

```c
#define WAL_DEFAULT_BUFFER_CAPACITY (4 * 1024 * 1024)  // 4MB
#define WAL_DEFAULT_FLUSH_THRESHOLD 10000               // 10K events
#define DEFAULT_BLOCK_CAPACITY 10000                    // 10K events/segment
```

## Testing

```bash
make test-stringdict   # StringDict tests
make test-eventblock   # EventBlock tests
make test-append       # append_to_block tests
make test-wal          # WAL tests
```

## Next Steps

### Priority 1: Segment Layer
- [ ] `segment_create()`
- [ ] `segment_write_to_disk()` - serialize EventBlock
- [ ] `segment_load_from_disk()` - deserialize to EventBlock
- [ ] File format: header + columnar arrays

### Priority 2: EventStore Coordinator
- [ ] `event_store_open()` - initialize all layers
- [ ] `event_store_append()` - write to WAL
- [ ] `event_store_flush()` - WAL → Segment
- [ ] `event_store_close()` - cleanup

### Priority 3: Query Path
- [ ] Linear scan filter (single segment)
- [ ] Multi-segment query
- [ ] PartitionIndex time pruning

### Priority 4: Performance
- [ ] InvertedIndex building
- [ ] Index-accelerated queries
- [ ] Lazy segment loading

## Performance Targets

- **Write**: 100K events/sec (WAL buffered)
- **Flush**: 10K events → segment in <100ms
- **Query**: Filter 1M events in <10ms (with indexes)
- **Memory**: <50 bytes/event in-memory
- **Disk**: <20 bytes/event compressed

## Design Principles

1. **Durability first**: WAL + fdatasync guarantees no data loss
2. **Immutable segments**: Write-once, never modify
3. **Columnar access**: Read only columns needed for query
4. **Lazy loading**: Load segments on demand
5. **Simple file format**: Easy to debug, no complex dependencies
