# Header Organization

FastEvents uses a simple 3-header structure optimized for embedded systems.

## Structure

```
src/
├── fastevents.h    ← Public API (users include this)
├── internal.h      ← Internal structures (devs include this)
└── wal.h           ← WAL layer (separate subsystem)
```

---

## 1. `fastevents.h` - Public API

**Who uses it:** End users (Python bindings, applications, examples)

**What it contains:**
- Error codes (`EventStoreError`)
- Opaque `EventStore*` handle
- Public functions:
  - `event_store_open()`
  - `event_store_append()`
  - `event_store_filter()`
  - `event_store_close()`
- Query types (`QueryFilter`, `QueryResult`)

**What it hides:**
- Internal data structures
- Implementation details
- Layer organization

**Usage:**
```c
#include "fastevents.h"

EventStore* db = event_store_open("./data");
event_store_append(db, "page_view", 123, now, "{}");
event_store_close(db);
```

---

## 2. `internal.h` - Internal Structures

**Who uses it:** Developers and tests

**What it contains:**
- All internal data structures:
  - `StringDict`
  - `EventBlock`
  - `Segment`
  - `InvertedIndex`
  - `PartitionIndex`
  - Full `EventStore` struct (not opaque)
- Internal functions for each layer
- Layer-by-layer organization

**What it includes:**
- `fastevents.h` (public API)
- `wal.h` (WAL layer)

**Usage:**
```c
#include "internal.h"

// Can access internal structures
StringDict* dict = string_dict_create(64);
EventBlock* block = create_event_block(10000);
```

---

## 3. `wal.h` - Write-Ahead Log

**Who uses it:** Internal implementation only

**What it contains:**
- `WAL` struct definition
- WAL functions:
  - `wal_create()`
  - `wal_append()`
  - `wal_flush_to_disk()`
  - `wal_recover()`

**Why separate:**
- WAL is a self-contained subsystem
- Clear responsibility boundary
- Can be tested independently
- Avoids circular dependencies

**Usage:**
```c
#include "wal.h"

WAL* wal = wal_create("wal.log");
wal_append(wal, data, len);
wal_flush_to_disk(wal);
```

---

## Benefits of This Structure

### For Users
✅ **Simple** - Include one file  
✅ **Clean** - Can't accidentally use internals  
✅ **Stable** - Internal changes don't affect them  

### For Developers
✅ **Organized** - Clear public/internal boundary  
✅ **Testable** - Can test internal components  
✅ **Maintainable** - Easy to navigate layers  

### For Embedded Systems
✅ **Lightweight** - Only 3 headers (not 7+)  
✅ **Fast compilation** - Minimal includes  
✅ **Small footprint** - Simple dependency graph  

---

## Include Hierarchy

```
User Code
    ↓
fastevents.h (public API only)


Developer Code
    ↓
internal.h
    ↓
fastevents.h + wal.h


Tests
    ↓
internal.h (full access to internals)
```

---

## Migration from Old Structure

**Before:**
```c
#include "storage.h"  // Everything in one giant header
```

**After:**
```c
// Users
#include "fastevents.h"

// Developers
#include "internal.h"
```

**Changes:**
- `storage.h` → split into `fastevents.h` + `internal.h`
- All `.c` files now include `internal.h`
- Tests include `internal.h`
- Examples include `fastevents.h`

---

## File Sizes

```
fastevents.h:  ~150 lines (public API)
internal.h:    ~200 lines (internal structures)
wal.h:         ~70 lines  (WAL subsystem)
───────────────────────────────────────
Total:         ~420 lines (vs 300+ in single header)
```

**Trade-off:** Slightly more lines total, but much clearer organization.

---

## When to Add More Headers?

Only if:
1. A layer exceeds 200 lines of just declarations
2. Clear subsystem emerges (like WAL)
3. Circular dependencies appear

**Don't split unless necessary** - keep it simple for embedded use.

---

## Summary

**Users:** `#include "fastevents.h"` and you're done.  
**Developers:** `#include "internal.h"` for full access.  
**WAL layer:** Separate but included by `internal.h`.

Clean, simple, maintainable. ✨
