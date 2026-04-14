# FastEvents Implementation Scratchpad

## Core Storage Implementation Steps

### Overview
Build the columnar storage engine that stores events in separate arrays (one per field). This is the foundation everything else builds on.

---

## Step 1: Define Core Data Structures

**File: `src/storage.h`**

- Error codes enum (`FE_OK`, `FE_OUT_OF_MEMORY`, etc.)
- `StringDict`: Dictionary for string → ID encoding
  - `char** strings` - array of unique strings
  - `uint32_t* ids` - corresponding IDs
  - `size_t count/capacity`
- `EventBlock`: Single event block with columnar storage
  - `size_t count/capacity`
  - `uint32_t* event_type_ids` - dictionary-encoded
  - `uint64_t* user_ids` - raw IDs
  - `uint64_t* timestamps` - Unix ms
  - `char** properties` - JSON strings
  - `uint64_t min/max_timestamp` - metadata
- `EventStore`: Main store handle
  - `EventBlock* block` - single block for MVP
  - `StringDict* event_dict` - event_type → ID mapping
  - `size_t total_events`
- `QueryFilter`: Filter specification
- `QueryResult`: Array of matching indices

---

## Step 2: Implement String Dictionary

**File: `src/storage.c` (Part 1)**

```c
#define INITIAL_DICT_CAPACITY 64

static StringDict* string_dict_create(size_t initial_capacity)
static int string_dict_get_or_add(StringDict* dict, const char* str, uint32_t* out_id)
static const char* string_dict_get(const StringDict* dict, uint32_t id)
static void string_dict_destroy(StringDict* dict)
```

**Key points:**
- Linear search for MVP (replace with hash table later)
- Automatic resizing when full (double capacity)
- `strdup()` to own string memory

---

## Step 3: Implement EventBlock

**File: `src/storage.c` (Part 2)**

```c
#define DEFAULT_BLOCK_CAPACITY 100000  // 100K events

static EventBlock* event_block_create(size_t capacity)
static void event_block_destroy(EventBlock* block)
static int event_block_grow(EventBlock* block)
static int event_block_append(
    EventBlock* block,
    uint32_t event_type_id,
    uint64_t user_id,
    uint64_t timestamp,
    const char* properties_json
)
```

**Key points:**
- Allocate all columnar arrays upfront with `calloc()`
- Grow capacity by 2x when full
- Update `min/max_timestamp` metadata on append
- Free properties strings in destroy

---

## Step 4: Implement EventStore

**File: `src/storage.c` (Part 3)**

```c
EventStore* event_store_create(size_t initial_capacity)
void event_store_destroy(EventStore* store)
int event_store_append(
    EventStore* store,
    const char* event_type,
    uint64_t user_id,
    uint64_t timestamp,
    const char* properties_json
)
size_t event_store_size(const EventStore* store)
void event_store_stats(const EventStore* store)
```

**Key points:**
- `event_store_append()` does:
  1. Look up event_type in dictionary (or add if new)
  2. Append to block with the encoded ID
  3. Increment total_events counter
- `event_store_stats()` prints memory usage, capacity, etc.

---

## Step 5: Implement Filter Query (Linear Scan)

**File: `src/storage.c` (Part 4)**

```c
static QueryResult* query_result_create(size_t initial_capacity)
static int query_result_add(QueryResult* result, size_t index)
QueryResult* event_store_filter(EventStore* store, const QueryFilter* filter)
void query_result_destroy(QueryResult* result)
```

**Key points:**
- Linear scan through all events in block
- Check each filter condition (type, user, time range)
- Collect matching row indices in QueryResult
- Auto-grow result array if needed

---

## Step 6: Create Test Program

**File: `tests/test_storage.c`**

Tests:
- `test_create_destroy()` - basic lifecycle
- `test_append_events()` - add events
- `test_filter_by_type()` - filter by event_type
- `test_filter_by_user()` - filter by user_id
- `test_filter_combined()` - multiple filters
- `test_large_dataset()` - 10K events, print stats

---

## Step 7: Create Makefile

**File: `Makefile`**

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g

test_storage: tests/test_storage.c src/storage.c
	$(CC) $(CFLAGS) -o $@ $^

test: test_storage
	./test_storage

clean:
	rm -f test_storage *.o
```

---

## Step 8: Build and Test

```bash
make clean
make test
```

Expected: All tests pass, 10K events stored, ~0.4MB memory

---

## Current Status

✅ Columnar storage (separate arrays per field)  
✅ String dictionary encoding  
✅ Dynamic growth  
✅ Linear scan filtering  
✅ Working tests  

**Next:** Inverted indexes (`index.c`) for 100x speedup

---

## Design Decisions Made

1. **Use glib?** → No. Custom hash table for control.
2. **Block size?** → 1M events (100K for MVP).
3. **Properties format?** → JSON strings for MVP.
4. **Time partitioning?** → Daily blocks (future).
5. **Dictionary search?** → Linear for MVP, hash table later.

---

## Implementation Priorities (Product Focus)

### Week 1: Prove Core Value
1. ✅ In-memory columnar storage (EventBlock)
2. 🔲 Inverted indexes (hash table → bitmap)
3. 🔲 Fast filter implementation
4. 🔲 Benchmark vs SQLite (must be 10x+ faster)

### Week 2: Make it Usable
5. 🔲 Persistence (mmap)
6. 🔲 Error handling
7. 🔲 Memory limits & overflow handling
8. 🔲 Crash recovery

### Week 3: Differentiate
9. 🔲 Session reconstruction OR funnel analysis
10. 🔲 Time helpers (`-24h`)
11. 🔲 `explain()` for query optimization
12. 🔲 Real benchmarks on real workloads

---

## Product Differentiation Notes

**Not a clone if we add:**
- Analytics-native API (funnels, cohorts, retention)
- Real-time streaming (`db.stream()`)
- Session reconstruction
- Time-travel queries
- Built-in anomaly detection

**Avoid scope creep:**
- ❌ SQL interface
- ❌ Clustering/replication
- ❌ Full-text search
- ❌ Admin UI

**Target niche:** "Analytics for bootstrappers/indie hackers" - fast embedded analytics without Mixpanel/Amplitude costs.
