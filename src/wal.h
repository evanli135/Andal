#ifndef WAL_H
#define WAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

// Error codes (same as storage.h)
typedef enum {
    WAL_OK = 0,
    WAL_OUT_OF_MEMORY = -1,
    WAL_INVALID_ARG = -2,
    WAL_IO_ERROR = -5
} WAL_Error;

// ============================================================================
// WAL: Write-Ahead Log for durability and crash recovery
// ============================================================================

/**
 * WAL: Sequential file writer. The first layer in FastEvents.
 * All writes land here first. A sequential byte buffer staged in RAM,
 * periodically flushed to wal.log on disk via write() + fdatasync().
 *
 * Guarantees durability — events survive process crashes and power loss
 * once flushed. Dumb by design: the WAL knows nothing about event structure,
 * it just stores length-prefixed raw bytes.
 */
typedef struct {
    int fd;                   // File descriptor for wal.log
    uint8_t* buffer;          // Staging buffer (in-memory)
    size_t buf_len;           // Current write cursor
    size_t buf_capacity;      // Total buffer size
    size_t event_count;       // Events staged since last flush
    size_t flush_threshold;   // Flush when this many events accumulate
    char* path;               // File path (e.g., "store/wal.log")
} WAL;

// Create a WAL with a given file path
WAL* wal_create(const char* path);

// Append raw bytes to the WAL buffer (length-prefixed)
// Auto-flushes when event_count >= flush_threshold
int wal_append(WAL* wal, const uint8_t* data, size_t len);

// Force flush buffer to disk with fdatasync() — guarantees durability
int wal_flush_to_disk(WAL* wal);

// Destroy WAL (flushes any pending data first)
void wal_destroy(WAL* wal);

// ============================================================================
// Recovery: Replay WAL on startup
// ============================================================================

/**
 * Called at startup to replay events that made it to disk but never
 * got flushed into a segment before a crash.
 *
 * Reads the WAL file entry by entry and calls on_event once per event.
 * ctx is passed through to on_event so the caller can thread state through.
 *
 * Returns FE_OK on success (including if WAL file doesn't exist).
 */
int wal_recover(
    WAL* wal,
    void (*on_event)(const uint8_t* data, size_t len, void* ctx),
    void* ctx
);

#endif // WAL_H
