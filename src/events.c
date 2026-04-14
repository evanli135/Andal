#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DEFAULT_BLOCK_CAPACITY 10000  // 10K events = 280 KB

// ============================================================================
// EventBlock Implementation
// ============================================================================

EventBlock* create_event_block(size_t initial_capacity) {
    if (initial_capacity == 0) {
        initial_capacity = DEFAULT_BLOCK_CAPACITY;
    }

    // Allocate the block struct
    EventBlock* block = malloc(sizeof(EventBlock));
    if (!block) return NULL;

    // Allocate column arrays
    block->event_type_ids = calloc(initial_capacity, sizeof(uint32_t));
    block->user_ids = calloc(initial_capacity, sizeof(uint64_t));
    block->timestamps = calloc(initial_capacity, sizeof(uint64_t));
    block->properties = calloc(initial_capacity, sizeof(char*));

    // Check if any allocation failed
    if (!block->event_type_ids || !block->user_ids ||
        !block->timestamps || !block->properties) {
        // Clean up partial allocations
        free(block->event_type_ids);
        free(block->user_ids);
        free(block->timestamps);
        free(block->properties);
        free(block);
        return NULL;
    }

    // Initialize metadata
    block->count           = 0;
    block->capacity        = initial_capacity;
    block->min_timestamp   = UINT64_MAX;  // Will be updated on first append
    block->max_timestamp   = 0;
    block->estimated_bytes = 0;

    return block;
}


/**
 * Append an event to an EventBlock.
 *
 * @param block           The event block to append to (must not be NULL)
 * @param event_type_id   The event type ID (from StringDict encoding)
 * @param user_id         The user ID
 * @param timestamp       Unix timestamp in milliseconds
 * @param properties_json JSON string for event properties (can be NULL)
 *
 * @return FE_OK on success
 *         FE_INVALID_ARG if block is NULL
 *         FE_CAPACITY_EXCEEDED if block is full (caller should resize)
 *         FE_OUT_OF_MEMORY if properties string allocation fails
 *
 * Notes:
 * - Does NOT automatically resize - returns FE_CAPACITY_EXCEEDED when full
 * - Caller is responsible for checking capacity and calling grow_event_block()
 * - Takes ownership of properties_json (makes a copy with strdup)
 * - Updates min_timestamp and max_timestamp metadata
 * - If properties_json is NULL or empty string, stores NULL (no allocation)
 *
 * Example:
 *   int err = append_to_block(block, 0, 123, 1000000, "{\"page\":\"/home\"}");
 *   if (err == FE_CAPACITY_EXCEEDED) {
 *       grow_event_block(block);
 *       err = append_to_block(block, 0, 123, 1000000, "{\"page\":\"/home\"}");
 *   }
 */
int append_to_block(
    EventBlock* block,
    uint32_t event_type_id,
    uint64_t user_id,
    uint64_t timestamp,
    const char* properties_json
) {
    // Validate input
    if (!block) {
        return FE_INVALID_ARG;
    }

    // Check capacity
    if (block->count >= block->capacity) {
        return FE_CAPACITY_EXCEEDED;
    }

    // Write to columnar arrays
    block->event_type_ids[block->count] = event_type_id;
    block->user_ids[block->count] = user_id;
    block->timestamps[block->count] = timestamp;

    // Handle properties
    if (properties_json && strlen(properties_json) > 0) {
        block->properties[block->count] = strdup(properties_json);
        if (!block->properties[block->count]) {
            return FE_OUT_OF_MEMORY;
        }
    } else {
        block->properties[block->count] = NULL;
    }

    // Increment count
    block->count++;

    // Track estimated serialized size: fixed fields + properties string
    block->estimated_bytes += sizeof(uint32_t) + sizeof(uint64_t) * 2 +
        (properties_json ? strlen(properties_json) + 1 : 0);

    // Update metadata (check both - not else if!)
    if (timestamp < block->min_timestamp) {
        block->min_timestamp = timestamp;
    }
    if (timestamp > block->max_timestamp) {
        block->max_timestamp = timestamp;
    }

    return FE_OK;
}

void destroy_event_block(EventBlock* block) {
    if (!block) return;

    // Free all properties strings
    for (size_t i = 0; i < block->count; i++) {
        free(block->properties[i]);
    }

    // Free columnar arrays
    free(block->event_type_ids);
    free(block->user_ids);
    free(block->timestamps);
    free(block->properties);

    // Free the block itself
    free(block);
}

void event_block_stats(const EventBlock* block) {
    if (!block) return;

    printf("=== EventBlock Stats ===\n");
    printf("Events: %zu / %zu (%.1f%% full)\n",
           block->count, block->capacity,
           100.0 * block->count / block->capacity);

    if (block->count > 0) {
        printf("Time range: %llu - %llu\n",
               (unsigned long long)block->min_timestamp,
               (unsigned long long)block->max_timestamp);
        uint64_t time_span = block->max_timestamp - block->min_timestamp;
        printf("Time span: %llu ms (%.1f seconds)\n",
               (unsigned long long)time_span, time_span / 1000.0);
    }

    size_t mem_bytes = block->capacity * (
        sizeof(uint32_t) +           // event_type_ids
        sizeof(uint64_t) * 2 +       // user_ids + timestamps
        sizeof(char*)                // properties pointers
    );
    printf("Memory allocated: %.2f MB\n", mem_bytes / (1024.0 * 1024.0));

    // Estimate actual memory (including properties strings)
    size_t properties_bytes = 0;
    for (size_t i = 0; i < block->count; i++) {
        if (block->properties[i]) {
            properties_bytes += strlen(block->properties[i]) + 1;
        }
    }
    if (properties_bytes > 0) {
        printf("Properties data: %.2f KB\n", properties_bytes / 1024.0);
    }
}


Segment* segment_create(uint64_t id, EventBlock* block, char* file_path) {
    Segment* segment = malloc(sizeof(Segment));
    if (!segment) return NULL;


    segment->segment_id = id;
    segment->file_path = strdup(file_path);
    segment->block = block;

    segment->is_loaded = true;

    segment->min_timestamp = block ? block->min_timestamp : UINT64_MAX;
    segment->max_timestamp = block ? block->max_timestamp : 0;
    segment->event_count   = block ? block->count : 0;

    return segment;
}

void segment_unload(Segment* seg) {
    if (!seg || !seg->is_loaded) return;
    destroy_event_block(seg->block);
    seg->block     = NULL;
    seg->is_loaded = false;
}

void segment_destroy(Segment* seg) {
    if (!seg) return;

    // Free file path string
    free(seg->file_path);

    // Destroy block if loaded
    if (seg->is_loaded && seg->block) {
        destroy_event_block(seg->block);
    }

    free(seg);
}