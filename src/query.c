#include "internal.h"
#include <stdlib.h>
#include <string.h>

// ============================================================================
// QueryResult lifecycle
// ============================================================================

QueryResult* query_result_create(size_t initial_capacity) {
    if (initial_capacity == 0) initial_capacity = 1;

    QueryResult* result = malloc(sizeof(QueryResult));
    if (!result) return NULL;

    result->event_type_ids = malloc(initial_capacity * sizeof(uint32_t));
    result->user_ids       = malloc(initial_capacity * sizeof(uint64_t));
    result->timestamps     = malloc(initial_capacity * sizeof(uint64_t));
    result->properties     = calloc(initial_capacity, sizeof(char*));

    if (!result->event_type_ids || !result->user_ids ||
        !result->timestamps     || !result->properties) {
        query_result_destroy(result);
        return NULL;
    }

    result->count    = 0;
    result->capacity = initial_capacity;
    return result;
}

// Append a single matching row. Grows arrays if at capacity.
int query_result_append(QueryResult* result, uint32_t type_id, uint64_t user_id,
                        uint64_t timestamp, const char* properties) {
    if (!result) return FE_INVALID_ARG;

    if (result->count >= result->capacity) {
        size_t new_cap = result->capacity * 2;

        uint32_t* new_type_ids = realloc(result->event_type_ids, new_cap * sizeof(uint32_t));
        uint64_t* new_user_ids = realloc(result->user_ids,       new_cap * sizeof(uint64_t));
        uint64_t* new_ts       = realloc(result->timestamps,     new_cap * sizeof(uint64_t));
        char**    new_props    = realloc(result->properties,     new_cap * sizeof(char*));

        if (!new_type_ids || !new_user_ids || !new_ts || !new_props) return FE_OUT_OF_MEMORY;

        result->event_type_ids = new_type_ids;
        result->user_ids       = new_user_ids;
        result->timestamps     = new_ts;
        result->properties     = new_props;
        result->capacity       = new_cap;
    }

    size_t i = result->count;
    result->event_type_ids[i] = type_id;
    result->user_ids[i]       = user_id;
    result->timestamps[i]     = timestamp;
    result->properties[i]     = properties ? strdup(properties) : NULL;

    if (properties && !result->properties[i]) return FE_OUT_OF_MEMORY;

    result->count++;
    return FE_OK;
}

void query_result_destroy(QueryResult* result) {
    if (!result) return;

    for (size_t i = 0; i < result->count; i++)
        free(result->properties[i]);

    free(result->event_type_ids);
    free(result->user_ids);
    free(result->timestamps);
    free(result->properties);
    free(result);
}



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
// Returns true if a row passes all active filter criteria.
static bool row_matches(uint32_t type_id, uint64_t uid, uint64_t ts,
                        bool has_type, uint32_t filter_type,
                        uint64_t filter_uid, uint64_t start_ts, uint64_t end_ts) {
    if (has_type      && type_id != filter_type) return false;
    if (filter_uid    && uid     != filter_uid)  return false;
    if (start_ts      && ts      <  start_ts)    return false;
    if (end_ts        && ts      >  end_ts)      return false;
    return true;
}

// Scan one block and append all matching rows into result.
static int scan_block(const EventBlock* block, QueryResult* result,
                      bool has_type, uint32_t filter_type,
                      uint64_t filter_uid, uint64_t start_ts, uint64_t end_ts) {
    for (size_t i = 0; i < block->count; i++) {
        if (!row_matches(block->event_type_ids[i], block->user_ids[i],
                         block->timestamps[i], has_type, filter_type,
                         filter_uid, start_ts, end_ts))
            continue;

        int err = query_result_append(result, block->event_type_ids[i],
                                      block->user_ids[i], block->timestamps[i],
                                      block->properties[i]);
        if (err != FE_OK) return err;
    }
    return FE_OK;
}

static Segment* find_segment(EventStore* store, uint64_t seg_id) {
    for (size_t i = 0; i < store->num_segments; i++) {
        if (store->segments[i]->segment_id == seg_id)
            return store->segments[i];
    }
    return NULL;
}

static int scan_segments(EventStore* store, QueryResult* result,
                         bool has_type, uint32_t filter_type,
                         uint64_t user_id, uint64_t start_ts, uint64_t end_ts) {
    size_t num_candidates = 0;
    uint64_t* seg_ids = partition_index_query(store->partition_idx,
                                              start_ts, end_ts, &num_candidates);

    for (size_t i = 0; i < num_candidates; i++) {
        Segment* seg = find_segment(store, seg_ids[i]);
        if (!seg) continue;

        bool was_loaded = seg->is_loaded;
        if (!was_loaded && segment_load_from_disk(seg) != FE_OK) continue;

        int err = scan_block(seg->block, result, has_type, filter_type,
                             user_id, start_ts, end_ts);
        if (!was_loaded) segment_unload(seg);
        if (err != FE_OK) { free(seg_ids); return err; }
    }

    free(seg_ids);
    return FE_OK;
}

QueryResult* event_store_filter(
    EventStore*  store,
    const char*  event_type,
    uint64_t     user_id,
    uint64_t     start_ts,
    uint64_t     end_ts
) {
    if (!store) return NULL;

    bool has_type = (event_type != NULL);
    uint32_t filter_type = UINT32_MAX;
    if (has_type) {
        filter_type = string_dict_get(store->event_dict, event_type);
        if (filter_type == UINT32_MAX) return query_result_create(0);
    }

    QueryResult* result = query_result_create(64);
    if (!result) return NULL;

    if (scan_segments(store, result, has_type, filter_type, user_id, start_ts, end_ts) != FE_OK) {
        query_result_destroy(result); return NULL;
    }

    if (store->active_block) {
        if (scan_block(store->active_block, result, has_type, filter_type,
                       user_id, start_ts, end_ts) != FE_OK) {
            query_result_destroy(result); return NULL;
        }
    }

    return result;
}