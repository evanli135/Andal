/**
 * FastEvents - Internal Structures
 *
 * Internal data structures and APIs for development and testing.
 * Users should NOT include this - only for internal implementation.
 */

#ifndef FASTEVENTS_INTERNAL_H
#define FASTEVENTS_INTERNAL_H

#include "fastevents.h"
#include "wal.h"

// ============================================================================
// Layer 1: StringDict (Event Type Encoding)
// ============================================================================

/**
 * StringDict: Hash table mapping event type strings to compact integer IDs.
 * - djb2 hash function
 * - Open addressing with linear probing
 * - Power-of-2 capacity for fast modulo
 */
typedef struct {
    char** strings;      // Hash table slots (NULL = empty)
    uint32_t* ids;       // IDs for each slot
    size_t count;        // Number of unique strings
    size_t capacity;     // Hash table size (always power of 2)
} StringDict;

StringDict* string_dict_create(size_t initial_capacity);

// Look up a string and return its ID, or UINT32_MAX if not present.
uint32_t string_dict_get(const StringDict* dict, const char* str);

// Add a new string and return its assigned ID, or UINT32_MAX on OOM.
// Behaviour is undefined if str is already in the dict.
uint32_t string_dict_add(StringDict* dict, const char* str);

void string_dict_destroy(StringDict* dict);

// Persist dictionary to a plain text file ("string id" per line, sorted by ID).
int string_dict_save(const StringDict* dict, const char* path);

// Load a dictionary from a file saved with string_dict_save. Caller owns result.
StringDict* string_dict_load(const char* path);

// Print diagnostics: load factor, probe lengths, entries
void string_dict_stats(const StringDict* dict);

// ============================================================================
// Layer 2: EventBlock (Columnar In-Memory Representation)
// ============================================================================

/**
 * EventBlock: In-memory columnar storage for a batch of events.
 * Each field stored in a separate array for cache-friendly access.
 * Row N = {event_type_ids[N], user_ids[N], timestamps[N], properties[N]}
 */
typedef struct {
    size_t count;
    size_t capacity;

    // Columnar arrays
    uint32_t* event_type_ids; // Dictionary-encoded event types
    uint64_t* user_ids;       // Raw user IDs
    uint64_t* timestamps;     // Unix timestamps (milliseconds)
    char** properties;        // JSON strings (NULL if no properties)

    // Metadata for query optimization
    uint64_t min_timestamp;
    uint64_t max_timestamp;
} EventBlock;

EventBlock* create_event_block(size_t initial_capacity);
void destroy_event_block(EventBlock* block);

// Print diagnostics: capacity, memory usage, time range
void event_block_stats(const EventBlock* block);

// Append event to block. Returns FE_CAPACITY_EXCEEDED if full (caller must grow).
// Takes ownership of properties_json string (makes internal copy).
int append_to_block(EventBlock* block, uint32_t event_type_id,
                    uint64_t user_id, uint64_t timestamp,
                    const char* properties_json);

// ============================================================================
// Layer 3: Segment (Immutable Columnar File + Metadata)
// ============================================================================

/**
 * Segment: Wraps an EventBlock with file metadata.
 * Once written to disk, the EventBlock can be unloaded and reloaded on demand.
 * Write-once, never modified after creation.
 */
typedef struct {
    uint64_t segment_id;      // Monotonic counter
    EventBlock* block;        // NULL if unloaded from memory
    char* file_path;          // "segments/seg_00001.dat"
    bool is_loaded;           // Is block currently in memory?
    uint64_t min_timestamp;   // Time range for partition pruning
    uint64_t max_timestamp;
    size_t event_count;       // Cached count (avoids loading to check size)
} Segment;

Segment* segment_create(uint64_t id, EventBlock* block, char* file_path);

// Serialize segment to disk: writes columnar arrays to .dat file
int segment_write_to_disk(Segment* seg, const char* dir);

// Deserialize segment from disk into memory (lazy load)
int segment_load_from_disk(Segment* seg);

// Predicate type for segment_delete_where — return true for rows to delete.
typedef bool (*RowPredicate)(uint32_t type_id, uint64_t user_id, uint64_t timestamp,
                             const char* properties, void* ctx);

// Rewrite the segment file with all rows matching predicate removed.
// Writes to a .tmp file first, then renames atomically so the original
// is never left in a corrupt state if the process crashes mid-write.
int segment_delete_where(Segment* seg, RowPredicate predicate, void* ctx);

// ============================================================================
// Encoding (WAL serialization)
// ============================================================================

// Encode event fields into a heap-allocated byte buffer for wal_append.
// Caller must free the returned buffer. Returns NULL on OOM.
uint8_t* event_encode(uint32_t event_type_id, uint64_t user_id,
                      uint64_t timestamp, const char* properties_json,
                      size_t* out_len);

// Decode a WAL entry back into event fields.
// out_properties is heap-allocated and owned by the caller (NULL if no properties).
int event_decode(const uint8_t* data, size_t len,
                 uint32_t* out_type_id, uint64_t* out_user_id,
                 uint64_t* out_timestamp, char** out_properties);

// Free EventBlock from memory but keep metadata (for memory management)
void segment_unload(Segment* seg);

void segment_destroy(Segment* seg);

// ============================================================================
// Layer 4: Inverted Index (Event Type → Row IDs)
// ============================================================================

/**
 * InvertedIndex: Maps event type IDs to sorted arrays of row positions.
 * Filtering by event type becomes O(1) lookup + O(k) for k matching rows,
 * instead of O(n) linear scan.
 */
typedef struct {
    uint32_t event_type_id;   // The event type this entry is for
    size_t* row_ids;          // Sorted array of matching row indices
    size_t count;             // Number of matching rows
} IndexEntry;

typedef struct {
    IndexEntry* entries;      // One entry per unique event type in segment
    size_t num_entries;
} InvertedIndex;

// Build index from EventBlock: maps each unique event_type_id to array of row positions
InvertedIndex* inverted_index_create(const EventBlock* block);

void inverted_index_destroy(InvertedIndex* idx);

// Serialize index to .idx file for persistence
int inverted_index_write(InvertedIndex* idx, const char* path);

// Deserialize index from .idx file
InvertedIndex* inverted_index_load(const char* path);

// ============================================================================
// Layer 5: PartitionIndex (Time Range → Segment ID)
// ============================================================================

/**
 * PartitionIndex: Sorted in-memory array mapping time ranges to segments.
 * Binary search prunes irrelevant segments for time-based queries.
 * Lives entirely in memory, rebuilt on startup from segment metadata.
 */
typedef struct {
    uint64_t min_timestamp;
    uint64_t max_timestamp;
    uint64_t segment_id;
} PartitionEntry;

typedef struct {
    PartitionEntry* entries;  // Sorted by min_timestamp
    size_t count;
    size_t capacity;
} PartitionIndex;

PartitionIndex* partition_index_create(void);

// Add time range entry (keeps sorted by min_ts for binary search)
int partition_index_add(PartitionIndex* idx, uint64_t min_ts,
                        uint64_t max_ts, uint64_t seg_id);

void partition_index_destroy(PartitionIndex* idx);

// Returns heap-allocated array of segment IDs whose time range overlaps [start_ts, end_ts].
// Caller must free. Returns NULL (and *out_count = 0) if no segments match.
uint64_t* partition_index_query(PartitionIndex* idx, uint64_t start_ts,
                                uint64_t end_ts, size_t* out_count);

// ============================================================================
// Query (query.c)
// ============================================================================

QueryResult* query_result_create(size_t initial_capacity);

// Append a matching row to the result. Grows arrays automatically.
int query_result_append(QueryResult* result, uint32_t type_id, uint64_t user_id,
                        uint64_t timestamp, const char* properties);

// ============================================================================
// EventStore (Coordinator)
// ============================================================================

/**
 * EventStore: Top-level coordinator that owns all subsystems.
 * Users interact with this exclusively through the public API in fastevents.h.
 * The internal structure is exposed here only for testing.
 *
 * ── End-to-end write flow ────────────────────────────────────────────────────
 *
 *   event_store_append(store, "click", user_id, timestamp, props)
 *       │
 *       ├─ 1. string_dict_get/add(event_dict, "click")
 *       │        Translates the human-readable event type string into a compact
 *       │        uint32_t ID. The dict is the single source of truth for this
 *       │        mapping across all segments in the store.
 *       │
 *       ├─ 2. event_encode(type_id, user_id, timestamp, props)  →  raw bytes
 *       │        Serialises the event into a flat byte buffer:
 *       │        [4B type_id][8B user_id][8B timestamp][props (null-terminated)]
 *       │
 *       ├─ 3. wal_append(wal, bytes)
 *       │        Writes the encoded bytes into the WAL's in-memory staging
 *       │        buffer. The WAL auto-flushes to wal.log (with fdatasync) once
 *       │        flush_threshold events accumulate. This guarantees durability:
 *       │        the event survives a crash even before it reaches a segment.
 *       │
 *       └─ 4. append_to_block(active_block, type_id, user_id, timestamp, props)
 *                Stages the event in the columnar in-memory EventBlock for
 *                fast query access without a disk read.
 *
 * ── Flush flow (active_block → segment file) ────────────────────────────────
 *
 *   event_store_flush(store)
 *       │
 *       ├─ 1. Build a file path: db_path/seg_<id>.dat
 *       ├─ 2. segment_write_to_disk(seg) — serialises active_block to .dat
 *       ├─ 3. Register segment in segments[] and partition_idx
 *       ├─ 4. Truncate/reset the WAL (entries now redundant)
 *       └─ 5. Reset active_block for the next batch
 *
 * ── Crash recovery flow ──────────────────────────────────────────────────────
 *
 *   event_store_open(db_path)
 *       │
 *       ├─ (normal init: create WAL, dict, active_block, etc.)
 *       └─ wal_recover(wal, on_event_cb)
 *              Replays any WAL entries that were flushed to wal.log but never
 *              made it into a segment before the crash. Each entry is decoded
 *              with event_decode() and fed back into append_to_block(), fully
 *              restoring the active_block to its pre-crash state.
 *
 * ── State summary ────────────────────────────────────────────────────────────
 *
 *   active_block   In-flight events not yet on disk as a segment. Lost on
 *                  crash without the WAL; recovered via wal_recover() on open.
 *
 *   segments[]     Immutable .dat files. Each is a fully self-contained
 *                  columnar snapshot of a past active_block.
 *
 *   wal.log        Append-only durability log. Redundant for any event already
 *                  in a segment; essential for events only in active_block.
 *
 *   event_dict     Global string → uint32_t mapping. Must be consistent across
 *                  all segments (IDs are baked into segment files).
 *
 *   partition_idx  In-memory time-range index over segments. Rebuilt on open
 *                  from segment metadata — never persisted separately.
 */
struct EventStore {
    char* db_path;            // Root directory for all store files (e.g., "store/")
    WAL* wal;                 // Write-ahead log — durability layer before segments
    StringDict* event_dict;   // Global event type string → compact uint32_t ID mapping

    // Immutable segment files written to disk
    Segment** segments;       // Array of pointers to all known segments
    size_t num_segments;      // Number of segments currently in the store
    size_t segments_capacity; // Allocated capacity of the segments array
    uint64_t next_segment_id; // Monotonic counter for generating unique segment IDs

    // In-memory time-range index (rebuilt on open, never written to disk)
    PartitionIndex* partition_idx;

    // Live staging area — events appended here before the next flush
    EventBlock* active_block;
};

#endif // FASTEVENTS_INTERNAL_H