# Fast Event Store - Project Specification

## Project Overview

A high-performance embedded event store for Python applications, optimized for analytics workloads (product analytics, user behavior tracking, A/B testing data).

**Core Value Proposition:**
- 10-100x faster than SQLite for event queries
- Embedded (no separate server process)
- Columnar storage for fast filtering and aggregation
- Simple Python API

---

## Architecture

### Storage Model
- **Columnar layout**: Each field stored in separate arrays
- **Append-only**: Events never updated/deleted (immutable log)
- **Partitioned by time**: Data organized in hourly/daily blocks
- **Indexed**: Inverted indexes for event_type and user_id

### Components

```
┌─────────────────────────────────────┐
│         Python API Layer            │
│  (event tracking, queries, filters) │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│      Python C Extension             │
│   (bindings, type conversion)       │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│       C Core Engine                 │
│  - Columnar storage                 │
│  - Inverted indexes                 │
│  - Query execution                  │
│  - Compression                      │
└─────────────────────────────────────┘
```

---

## Project Structure

```
fastevents/
├── README.md
├── PROJECT.md                 # This file
├── Makefile                   # Build automation
├── setup.py                   # Python package setup
│
├── src/                       # C implementation
│   ├── core.h                 # Main header
│   ├── core.c                 # Core event store logic
│   ├── storage.h              # Columnar storage
│   ├── storage.c
│   ├── index.h                # Inverted indexes
│   ├── index.c
│   ├── query.h                # Query execution
│   ├── query.c
│   └── compress.h             # Compression (future)
│       compress.c
│
├── python/                    # Python bindings
│   ├── fastevents_module.c    # Python C API extension
│   └── fastevents/            # Pure Python wrapper
│       ├── __init__.py
│       ├── store.py           # High-level API
│       └── types.py           # Type definitions
│
├── tests/                     # Test suite
│   ├── test_core.c            # C unit tests
│   ├── test_python.py         # Python integration tests
│   └── benchmark.py           # Performance benchmarks
│
└── examples/                  # Usage examples
    ├── basic.py
    └── analytics.py
```

---

## API Design

### Python API (Target)

```python
import fastevents

# Create/open store
db = fastevents.EventStore("events.db")

# Track events
db.track("page_view", 
         user_id=123, 
         page="/pricing",
         timestamp=1735689600)

db.track("click",
         user_id=123,
         button="signup",
         timestamp=1735689605)

# Queries
events = db.filter(event_type="page_view", 
                   user_id=123,
                   start_time="-24h")

# Aggregations
counts = db.count_by("event_type", since="-7d")
# Returns: {"page_view": 5234, "click": 892, ...}

unique_users = db.unique("user_id", 
                         event_type="purchase",
                         since="-30d")

# Funnel analysis (Phase 2)
funnel = db.funnel(
    steps=["page_view", "click", "purchase"],
    within="1h"
)
# Returns: [{"step": "page_view", "count": 1000}, 
#           {"step": "click", "count": 450}, 
#           {"step": "purchase", "count": 89}]
```

---

## C Core Design

### Data Structures

```c
// Event store handle
typedef struct {
    char* db_path;
    int fd;                     // File descriptor
    void* mmap_ptr;             // Memory-mapped region
    size_t total_events;
    EventBlock* current_block;
    IndexSet* indexes;
} EventStore;

// Single time-partitioned block
typedef struct {
    uint64_t block_id;          // Timestamp-based ID
    size_t num_events;
    size_t capacity;
    
    // Columnar arrays
    uint32_t* event_types;      // Dictionary-encoded
    uint64_t* user_ids;         // Raw uint64
    uint64_t* timestamps;       // Unix timestamps (ms)
    uint8_t** properties;       // JSON blobs (variable length)
    
    // Metadata
    uint64_t min_timestamp;
    uint64_t max_timestamp;
} EventBlock;

// Inverted index (event_type -> row IDs)
typedef struct {
    HashTable* event_type_index;  // event_type -> Bitmap
    HashTable* user_id_index;     // user_id -> Bitmap
    StringDict* event_type_dict;  // String -> ID mapping
} IndexSet;

// Bitmap for fast filtering
typedef struct {
    uint64_t* bits;
    size_t num_bits;
} Bitmap;
```

### Core Functions (C API)

```c
// Lifecycle
EventStore* event_store_open(const char* path);
void event_store_close(EventStore* store);

// Write
int event_store_append(EventStore* store,
                       const char* event_type,
                       uint64_t user_id,
                       uint64_t timestamp,
                       const char* properties_json);

// Query
QueryResult* event_store_filter(EventStore* store,
                                const QuerySpec* spec);

// Aggregation
CountMap* event_store_count_by(EventStore* store,
                               const char* field,
                               const QuerySpec* spec);

// Utilities
void event_store_flush(EventStore* store);
EventStats event_store_stats(EventStore* store);
```

---

## Implementation Phases

### Phase 1: MVP (Days 1-3)
**Goal**: Basic append + filter queries

- [ ] Event block structure (in-memory arrays)
- [ ] Append events (no persistence yet)
- [ ] Simple linear scan filter (event_type + user_id)
- [ ] Python C extension skeleton
- [ ] Basic Python wrapper
- [ ] Simple tests

**Deliverable**: Can append events and filter by event_type/user_id in-memory

---

### Phase 2: Persistence (Days 4-5)
**Goal**: Save/load from disk

- [ ] File format design (header + blocks)
- [ ] mmap-based storage
- [ ] Write blocks to disk
- [ ] Load blocks on open
- [ ] Handle file growth

**Deliverable**: Events persist across restarts

---

### Phase 3: Indexing (Days 6-7)
**Goal**: Fast queries with inverted indexes

- [ ] Hash table implementation (or use glib)
- [ ] Bitmap implementation
- [ ] Build inverted index (event_type -> bitmap)
- [ ] Build user_id index
- [ ] Query executor uses indexes

**Deliverable**: Queries 100x faster via index lookups

---

### Phase 4: Aggregations (Days 8-9)
**Goal**: count_by, unique_count

- [ ] count_by implementation (group + count)
- [ ] unique_count (use bitmap or hash set)
- [ ] Time-based filtering (timestamp ranges)

**Deliverable**: Analytics queries work

---

### Phase 5: Compression (Days 10+)
**Goal**: Reduce disk usage

- [ ] Delta encoding for timestamps
- [ ] Dictionary encoding for event_types (done in Phase 1)
- [ ] LZ4 compression for properties JSON
- [ ] Run-length encoding for sorted user_ids

**Deliverable**: 5-10x compression ratio

---

## Technical Specifications

### File Format

```
┌─────────────────────────────────────┐
│          File Header                │
│  - Magic number (8 bytes)           │
│  - Version (4 bytes)                │
│  - Block count (8 bytes)            │
│  - Block index offset (8 bytes)     │
└─────────────────────────────────────┘
┌─────────────────────────────────────┐
│          Event Block 1              │
│  - Block header (metadata)          │
│  - event_type array (compressed)    │
│  - user_id array (compressed)       │
│  - timestamp array (delta-encoded)  │
│  - properties array (LZ4)           │
└─────────────────────────────────────┘
┌─────────────────────────────────────┐
│          Event Block 2              │
│  ...                                │
└─────────────────────────────────────┘
│          ...more blocks...          │
└─────────────────────────────────────┘
┌─────────────────────────────────────┐
│          Block Index                │
│  - Array of (block_id, offset)      │
└─────────────────────────────────────┘
```

### Memory Management

- **Initial**: All in-memory (malloc/free)
- **Phase 2**: mmap for blocks (lazy loading)
- **Phase 3**: Arena allocator for indexes (fast bulk alloc)
- **Memory limit**: Configurable max memory (default 1GB)

### Concurrency (Future)

- **MVP**: Single-threaded (Python GIL protects us)
- **Future**: Read-write lock (multiple readers, single writer)
- **Future**: Lock-free append buffer (ring buffer for writes)

---

## Dependencies

### C
- **Required**: Standard C library (C11)
- **Optional**: 
  - `glib` - Hash tables, data structures (if we don't roll our own)
  - `lz4` - Compression (Phase 5)

### Python
- **Required**: Python 3.8+
- **Build**: setuptools, C compiler (gcc/clang)
- **Testing**: pytest
- **Benchmarks**: pandas, sqlite3 (for comparison)

---

## Build System

### Makefile targets

```makefile
make build          # Compile C library
make test           # Run C unit tests
make python         # Build Python extension
make pytest         # Run Python tests
make benchmark      # Run performance benchmarks
make clean          # Clean build artifacts
make install        # Install Python package locally
```

### setup.py

- Use `Extension` from setuptools
- Compile C files into shared library
- Link Python C extension

---

## Testing Strategy

### C Unit Tests
- Test each component in isolation
- Use simple assert-based testing
- Memory leak checks (valgrind)

### Python Integration Tests
- Test full workflows (append → query → verify)
- Edge cases (empty results, large datasets)
- Error handling

### Benchmarks
- Compare vs SQLite, pandas
- Measure throughput (events/sec)
- Measure query latency (p50, p95, p99)
- Test with datasets: 1K, 100K, 10M events

---

## Performance Targets

### Write Performance
- **Target**: 1M events/sec (single-threaded)
- **Bottleneck**: Memory copies, index updates

### Query Performance
- **Linear scan**: 100M events/sec (SIMD future)
- **Index lookup**: 1M matching events in <10ms
- **Aggregations**: count_by on 10M events in <50ms

### Memory Usage
- **In-memory**: ~50 bytes/event (before compression)
- **On-disk**: ~20 bytes/event (after compression)

### Compression Ratios
- **Timestamps**: 8 bytes → 2 bytes (delta encoding)
- **Event types**: 20 bytes → 2 bytes (dictionary)
- **User IDs**: 8 bytes → 4 bytes (if sequential)

---

## Next Steps

1. **Create project structure**: directories, Makefile, setup.py
2. **Implement Phase 1 MVP**: In-memory event storage with basic filtering
3. **Python bindings**: Minimal C extension to expose C API
4. **Basic tests**: Verify append + filter work
5. **Iterate**: Add persistence, indexes, compression

---

## Open Questions / Decisions Needed

- [ ] Use glib for hash tables or implement custom?
- [ ] Block size: 100K events? 1M events?
- [ ] Properties: JSON strings or binary format (msgpack)?
- [ ] Time partitioning: Hourly or daily blocks?
- [ ] Python packaging: Wheels with precompiled binaries?

---

## References & Resources

### Columnar Storage
- Apache Arrow format
- Parquet file format

### Inverted Indexes
- Lucene architecture
- Roaring Bitmaps

### Similar Projects
- DuckDB (for columnar query execution)
- ClickHouse (for event analytics)
- SQLite (for embedded database patterns)

### C Libraries
- glib: https://docs.gtk.org/glib/
- lz4: https://github.com/lz4/lz4

---

## Success Criteria

**MVP is successful when:**
- ✅ Can append 100K events
- ✅ Can filter by event_type and user_id
- ✅ Python API is clean and intuitive
- ✅ Basic tests pass

**Project is successful when:**
- ✅ 10x faster than SQLite for event queries
- ✅ Can handle 10M+ events
- ✅ Uses <1GB memory for 10M events
- ✅ Comprehensive test coverage
- ✅ Real-world usable (error handling, edge cases)

---

**Created**: 2026-04-01
**Last Updated**: 2026-04-01
