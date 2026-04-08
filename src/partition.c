#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define PARTITION_INITIAL_CAPACITY 64

PartitionIndex* partition_index_create(void) {
    PartitionIndex* idx = malloc(sizeof(PartitionIndex));
    if (!idx) return NULL;

    idx->entries = malloc(16 * sizeof(PartitionEntry));
    if (!idx->entries) {
        free(idx);
        return NULL;
    }

    idx->count = 0;
    idx->capacity = PARTITION_INITIAL_CAPACITY;
    return idx;
}

// Binary search for the index at which a new entry with min_ts should be
// inserted to keep entries[] sorted by min_timestamp ascending.
static size_t find_insert_pos(const PartitionIndex* idx, uint64_t min_ts) {
    size_t lo = 0;
    size_t hi = idx->count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (idx->entries[mid].min_timestamp <= min_ts)
            lo = mid + 1;
        else
            hi = mid;
    }

    return lo;
}

int partition_index_add(PartitionIndex* idx, uint64_t min_ts,
    uint64_t max_ts, uint64_t seg_id) {
    if (!idx) return FE_INVALID_ARG;

    // Grow the entries array if full
    if (idx->count >= idx->capacity) {
        size_t new_capacity = idx->capacity * 2;
        PartitionEntry* new_entries = realloc(idx->entries, new_capacity * sizeof(PartitionEntry));
        if (!new_entries) return FE_OUT_OF_MEMORY;
        idx->entries = new_entries;
        idx->capacity = new_capacity;
    }

    // Find where this entry belongs to maintain sort order
    size_t pos = find_insert_pos(idx, min_ts);

    // Shift entries from pos onward right by one to make room
    if (pos < idx->count)
        memmove(&idx->entries[pos + 1], &idx->entries[pos],
                (idx->count - pos) * sizeof(PartitionEntry));

    idx->entries[pos].min_timestamp = min_ts;
    idx->entries[pos].max_timestamp = max_ts;
    idx->entries[pos].segment_id    = seg_id;
    idx->count++;

    return FE_OK;
}

void partition_index_destroy(PartitionIndex* idx) {
    if (!idx) return;
    free(idx->entries);
    free(idx);
}

// Returns a heap-allocated array of segment IDs whose time range overlaps
// [start_ts, end_ts]. Caller must free. Returns NULL if no matches.
uint64_t* partition_index_query(PartitionIndex* idx, uint64_t start_ts,
                                uint64_t end_ts, size_t* out_count) {
    if (!idx || !out_count) return NULL;
    *out_count = 0;

    if (idx->count == 0) return NULL;

    uint64_t* ids = malloc(idx->count * sizeof(uint64_t));
    if (!ids) return NULL;

    for (size_t i = 0; i < idx->count; i++) {
        PartitionEntry* e = &idx->entries[i];
        // A segment overlaps the query range if:
        //   seg.min <= query.end  (or no end bound)
        //   seg.max >= query.start (or no start bound)
        bool under_end   = (end_ts   == 0) || (e->min_timestamp <= end_ts);
        bool above_start = (start_ts == 0) || (e->max_timestamp >= start_ts);
        if (under_end && above_start)
            ids[(*out_count)++] = e->segment_id;
    }

    if (*out_count == 0) { free(ids); return NULL; }
    return ids;
}