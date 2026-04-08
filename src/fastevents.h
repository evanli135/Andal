/**
 * FastEvents - Lightweight Embedded Event Store
 *
 * A high-performance columnar event store optimized for analytics workloads.
 * Provides durability via WAL, fast queries via inverted indexes, and
 * efficient storage via columnar segments.
 *
 * Public API - This is the only header users need to include.
 */

#ifndef FASTEVENTS_H
#define FASTEVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef enum {
    FE_OK = 0,                  // Success
    FE_OUT_OF_MEMORY = -1,      // Memory allocation failed
    FE_INVALID_ARG = -2,        // Invalid argument passed
    FE_CAPACITY_EXCEEDED = -3,  // Buffer/capacity limit reached
    FE_NOT_FOUND = -4,          // Resource not found
    FE_IO_ERROR = -5,           // File I/O error
    FE_CORRUPT_DATA = -6        // Data corruption detected
} EventStoreError;


/**
 * EventStore: Main handle for the event store.
 * Internal structure is hidden in internal.h
 */
typedef struct EventStore EventStore;

/**
 * Open or create an event store at the given path.
 *
 * @param db_path  Path to the database directory (will be created if needed)
 * @return         EventStore handle, or NULL on failure
 *
 * Example:
 *   EventStore* db = event_store_open("./data");
 */
EventStore* event_store_open(const char* db_path);

/**
 * Close the event store and free all resources.
 * Flushes any pending writes before closing.
 *
 * @param store  EventStore handle (can be NULL)
 */
void event_store_close(EventStore* store);


// ============================================================================
// Write Operations
// ============================================================================

/**
 * Append an event to the store.
 * Events are first written to the WAL, then flushed to segments periodically.
 *
 * @param store            EventStore handle
 * @param event_type       Event type string (e.g., "page_view", "click")
 * @param user_id          User identifier
 * @param timestamp        Unix timestamp in milliseconds
 * @param properties_json  JSON properties string (can be NULL)
 * @return                 FE_OK on success, error code otherwise
 *
 * Example:
 *   event_store_append(db, "page_view", 123, 1680000000000, "{\"page\":\"/home\"}");
 */
int event_store_append(
    EventStore* store,
    const char* event_type,
    uint64_t user_id,
    uint64_t timestamp,
    const char* properties_json
);

/**
 * Force flush pending writes from WAL to disk.
 * Creates a new segment from buffered events.
 *
 * @param store  EventStore handle
 * @return       FE_OK on success, error code otherwise
 */
int event_store_flush(EventStore* store);

// ============================================================================
// Query Operations
// ============================================================================

/**
 * QueryResult: Results from a filter query.
 * Contains columnar arrays for efficient access.
 */
typedef struct {
    uint32_t* event_type_ids; // Dictionary-encoded event types
    uint64_t* user_ids;       // User IDs
    uint64_t* timestamps;     // Timestamps
    char** properties;        // JSON properties (NULL if none)
    size_t count;             // Number of matched rows
    size_t capacity;          // Allocated capacity (internal, do not use)
} QueryResult;

/**
 * Filter events by criteria.
 * Searches across all segments and the active block.
 *
 * @param store       EventStore handle
 * @param event_type  Event type to match (NULL = any)
 * @param user_id     User ID to match (0 = any)
 * @param start_ts    Minimum timestamp inclusive (0 = no lower bound)
 * @param end_ts      Maximum timestamp inclusive (0 = no upper bound)
 * @return            QueryResult with matching events, or NULL on error
 *
 * Example:
 *   QueryResult* r = event_store_filter(db, "page_view", 123, 0, 0);
 *   // ... use results ...
 *   query_result_destroy(r);
 */
QueryResult* event_store_filter(
    EventStore*  store,
    const char*  event_type,
    uint64_t     user_id,
    uint64_t     start_ts,
    uint64_t     end_ts
);

/**
 * Free a query result.
 *
 * @param result  QueryResult to free (can be NULL)
 */
void query_result_destroy(QueryResult* result);

// ============================================================================
// Utilities
// ============================================================================

/**
 * Get total number of events in the store.
 *
 * @param store  EventStore handle
 * @return       Total event count
 */
size_t event_store_size(const EventStore* store);

/**
 * Print statistics about the store (for debugging).
 *
 * @param store  EventStore handle
 */
void event_store_stats(const EventStore* store);

#ifdef __cplusplus
}
#endif

#endif // FASTEVENTS_H
