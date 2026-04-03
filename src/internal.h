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

// Look up string and return its ID, or add it if not present. Thread-safe for single writer.
int string_dict_get_or_add(StringDict* dict, const char* str, uint32_t* out_id);

void string_dict_destroy(StringDict* dict);

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

// Binary search for segments overlapping [start_time, end_time). Returns array of segment IDs.
size_t* partition_index_query(PartitionIndex* idx, uint64_t start_time,
                               uint64_t end_time, size_t* out_count);

// ============================================================================
// EventStore (Coordinator)
// ============================================================================

/**
 * EventStore: Internal structure (exposed for testing).
 * Users only see the opaque typedef in fastevents.h
 */
struct EventStore {
    char* db_path;            // Root directory (e.g., "store/")
    WAL* wal;                 // Write-ahead log
    StringDict* event_dict;   // Global event type → ID mapping

    // Segments
    Segment** segments;       // Array of segments
    size_t num_segments;
    size_t segments_capacity;
    uint64_t next_segment_id; // Monotonic counter

    // Indexes
    PartitionIndex* partition_idx;

    // In-memory staging (not yet flushed to segment)
    EventBlock* active_block;
};

#endif // FASTEVENTS_INTERNAL_H
