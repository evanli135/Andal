#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "internal.h"

#ifdef _WIN32
#  include <windows.h>
#  include <direct.h>
#  define make_dir(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  include <dirent.h>
#  define make_dir(p) mkdir(p, 0755)
#endif

#define FLUSH_EVENT_THRESHOLD  10000          // events
#define FLUSH_SIZE_THRESHOLD   (4 * 1024 * 1024) // 4 MB
#define FLUSH_INTERVAL_MS      1000           // 1 second


#define INITIAL_SEGMENTS_CAPACITY 16
#define STRING_DICT_PATH "event_types.txt"

void event_store_destroy(EventStore* store); // defined below
static char* build_segment_path(const char* db_path, uint64_t seg_id); // defined below
static int   grow_segments_array(EventStore* store);                    // defined below
static void  register_segment(EventStore* store, Segment* seg);         // defined below

// ── WAL recovery ─────────────────────────────────────────────────────────────

typedef struct {
    EventBlock* block;
} RecoverCtx;

static void on_wal_event(const uint8_t* data, size_t len, void* ctx) {
    RecoverCtx* c = ctx;
    uint32_t type_id; uint64_t user_id, ts; char* props;

    if (event_decode(data, len, &type_id, &user_id, &ts, &props) != FE_OK)
        return;

    append_to_block(c->block, type_id, user_id, ts, props);
    free(props);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint64_t current_time_ms(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static bool should_flush(const EventStore* store) {
    const EventBlock* b = store->active_block;
    if (!b || b->count == 0) return false;
    if (b->count >= store->flush_event_threshold) return true;
    if (b->estimated_bytes >= store->flush_size_threshold) return true;
    if (store->flush_interval_ms > 0 &&
        current_time_ms() - store->last_flush_time_ms >= store->flush_interval_ms)
        return true;
    return false;
}

static char* build_path(const char* dir, const char* filename) {
    size_t len = strlen(dir) + 1 + strlen(filename) + 1;
    char* path = malloc(len);
    if (!path) return NULL;
    snprintf(path, len, "%s/%s", dir, filename);    
    return path;
}

static WAL* open_wal(const char* db_path) {
    char* path = build_path(db_path, "wal.log");
    if (!path) return NULL;
    WAL* wal = wal_create(path);
    free(path);
    return wal;
}

// Load the event type dictionary from disk, or create a fresh one for new stores.
static StringDict* load_or_create_dict(const char* db_path) {
    char* path = build_path(db_path, STRING_DICT_PATH);
    if (!path) return NULL;
    StringDict* dict = string_dict_load(path);
    if (!dict) dict = string_dict_create(1024);
    free(path);
    return dict;
}

// Write the event type dictionary to disk. Called after every new type is added.
static int persist_dict(EventStore* store) {
    char* path = build_path(store->db_path, STRING_DICT_PATH);
    if (!path) return FE_OUT_OF_MEMORY;
    int err = string_dict_save(store->event_dict, path);
    free(path);
    return err;
}

// Scan db_path for existing seg_*.dat files and register them in the store's
// segment index. Called once during event_store_open so queries after a
// close/reopen see all previously flushed segments.
static int scan_and_register_segments(EventStore* store) {
    uint64_t max_seg_id = 0;

#ifdef _WIN32
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\seg_*.dat", store->db_path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return FE_OK;  // no segments yet

    do {
        unsigned long long raw_id;
        if (sscanf(fd.cFileName, "seg_%llu.dat", &raw_id) != 1) continue;

        uint64_t seg_id = (uint64_t)raw_id;
        char* path = build_segment_path(store->db_path, seg_id);
        if (!path) { FindClose(h); return FE_OUT_OF_MEMORY; }

        Segment* seg = calloc(1, sizeof(Segment));
        if (!seg) { free(path); FindClose(h); return FE_OUT_OF_MEMORY; }
        seg->segment_id = seg_id;
        seg->file_path  = path;

        if (segment_peek_metadata(seg) != FE_OK) { segment_destroy(seg); continue; }

        int err = grow_segments_array(store);
        if (err != FE_OK) { segment_destroy(seg); FindClose(h); return err; }

        register_segment(store, seg);
        if (seg_id > max_seg_id) max_seg_id = seg_id;
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    DIR* dir = opendir(store->db_path);
    if (!dir) return FE_OK;  // fresh store with no directory yet is fine

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        unsigned long long raw_id;
        if (sscanf(entry->d_name, "seg_%llu.dat", &raw_id) != 1) continue;

        uint64_t seg_id = (uint64_t)raw_id;
        char* path = build_segment_path(store->db_path, seg_id);
        if (!path) { closedir(dir); return FE_OUT_OF_MEMORY; }

        Segment* seg = calloc(1, sizeof(Segment));
        if (!seg) { free(path); closedir(dir); return FE_OUT_OF_MEMORY; }
        seg->segment_id = seg_id;
        seg->file_path  = path;

        if (segment_peek_metadata(seg) != FE_OK) { segment_destroy(seg); continue; }

        int err = grow_segments_array(store);
        if (err != FE_OK) { segment_destroy(seg); closedir(dir); return err; }

        register_segment(store, seg);
        if (seg_id > max_seg_id) max_seg_id = seg_id;
    }

    closedir(dir);
#endif

    if (max_seg_id >= store->next_segment_id)
        store->next_segment_id = max_seg_id + 1;

    return FE_OK;
}

EventStore* event_store_open(const char* db_path) {
    if (!db_path) return NULL;

    make_dir(db_path);

    EventStore* store = malloc(sizeof(EventStore));
    if (!store) return NULL;

    store->db_path = strdup(db_path);
    if (!store->db_path) goto fail_store;

    store->wal = open_wal(db_path);
    if (!store->wal) goto fail_db_path;

    store->event_dict = load_or_create_dict(db_path);
    if (!store->event_dict) goto fail_wal;

    store->segments = calloc(INITIAL_SEGMENTS_CAPACITY, sizeof(Segment*));
    if (!store->segments) goto fail_dict;

    store->partition_idx = partition_index_create();
    if (!store->partition_idx) goto fail_segments;

    store->active_block = create_event_block(0);
    if (!store->active_block) goto fail_partition;

    store->num_segments      = 0;
    store->segments_capacity = INITIAL_SEGMENTS_CAPACITY;
    store->next_segment_id   = 1;

    store->flush_event_threshold = FLUSH_EVENT_THRESHOLD;
    store->flush_size_threshold  = FLUSH_SIZE_THRESHOLD;
    store->flush_interval_ms     = FLUSH_INTERVAL_MS;
    store->last_flush_time_ms    = current_time_ms();

    // Rebuild segment index from previously-flushed .dat files on disk
    if (scan_and_register_segments(store) != FE_OK) {
        event_store_destroy(store);
        return NULL;
    }

    // Replay any WAL entries that never made it into a segment before last crash
    RecoverCtx recover_ctx = { store->active_block };
    wal_recover(store->wal, on_wal_event, &recover_ctx);

    return store;

fail_partition:  partition_index_destroy(store->partition_idx);
fail_segments:   free(store->segments);
fail_dict:       string_dict_destroy(store->event_dict);
fail_wal:        wal_destroy(store->wal);
fail_db_path:    free(store->db_path);
fail_store:      free(store);
    return NULL;
}



void event_store_close(EventStore* store) {
    if (!store) return;
    wal_flush_to_disk(store->wal);  // ensure nothing is stranded in the buffer
    event_store_destroy(store);
}

void event_store_destroy(EventStore* store) {
    if (!store) return;

    destroy_event_block(store->active_block);

    for (size_t i = 0; i < store->num_segments; i++)
        segment_destroy(store->segments[i]);
    free(store->segments);

    partition_index_destroy(store->partition_idx);
    string_dict_destroy(store->event_dict);
    wal_destroy(store->wal);
    free(store->db_path);
    free(store);
}



int event_store_append(EventStore* store, const char* event_type,
                       uint64_t user_id, uint64_t timestamp,
                       const char* properties_json) {
    if (!store || !event_type) return FE_INVALID_ARG;

    // Resolve event type string to a compact integer ID
    uint32_t type_id = string_dict_get(store->event_dict, event_type);
    if (type_id == UINT32_MAX) {
        type_id = string_dict_add(store->event_dict, event_type);
        if (type_id == UINT32_MAX) return FE_OUT_OF_MEMORY;
        int err = persist_dict(store);
        if (err != FE_OK) return err;
    }

    // Encode and append to WAL first — durability before in-memory staging
    size_t len = 0;
    uint8_t* data = event_encode(type_id, user_id, timestamp, properties_json, &len);
    if (!data) return FE_OUT_OF_MEMORY;

    int err = wal_append(store->wal, data, len);
    free(data);
    if (err != FE_OK) return err;

    // Stage in the active EventBlock
    err = append_to_block(store->active_block, type_id, user_id, timestamp, properties_json);
    if (err == FE_CAPACITY_EXCEEDED) {
        err = event_store_flush(store);
        if (err != FE_OK) return err;
        err = append_to_block(store->active_block, type_id, user_id, timestamp, properties_json);
    }
    if (err != FE_OK) return err;

    // Auto-flush when any threshold is exceeded
    if (should_flush(store)) {
        err = event_store_flush(store);
    }

    return err;
}

static char* build_segment_path(const char* db_path, uint64_t seg_id) {
    char filename[32];
    snprintf(filename, sizeof(filename), "seg_%05llu.dat", (unsigned long long)seg_id);
    return build_path(db_path, filename);
}

// Grow the segments array if it's full. Returns FE_OK or FE_OUT_OF_MEMORY.
static int grow_segments_array(EventStore* store) {
    if (store->num_segments < store->segments_capacity) return FE_OK;

    size_t new_capacity = store->segments_capacity * 2;
    Segment** grown = realloc(store->segments, new_capacity * sizeof(Segment*));
    if (!grown) return FE_OUT_OF_MEMORY;

    store->segments = grown;
    store->segments_capacity = new_capacity;
    return FE_OK;
}

// Add a fully-written segment to the store's index. Assumes array has room.
static void register_segment(EventStore* store, Segment* seg) {
    store->segments[store->num_segments++] = seg;
    partition_index_add(store->partition_idx, seg->min_timestamp,
                        seg->max_timestamp, seg->segment_id);
}

size_t event_store_size(const EventStore* store) {
    if (!store) return 0;
    size_t total = store->active_block ? store->active_block->count : 0;
    for (size_t i = 0; i < store->num_segments; i++)
        total += store->segments[i]->event_count;
    return total;
}

void event_store_stats(const EventStore* store) {
    if (!store) return;
    printf("=== EventStore Stats ===\n");
    printf("Path:         %s\n", store->db_path);
    printf("Segments:     %zu\n", store->num_segments);
    printf("Active block: %zu events\n",
           store->active_block ? store->active_block->count : 0);
    printf("Total events: %zu\n", event_store_size(store));
    if (store->event_dict)
        printf("Event types:  %zu\n", store->event_dict->count);
}

int event_store_flush(EventStore* store) {
    if (!store) return FE_INVALID_ARG;
    EventBlock* block = store->active_block;
    if (!block || block->count == 0) return FE_OK;

    char* path = build_segment_path(store->db_path, store->next_segment_id);
    if (!path) return FE_OUT_OF_MEMORY;

    Segment* seg = segment_create(store->next_segment_id, block, path);
    free(path);
    if (!seg) return FE_OUT_OF_MEMORY;

    // Write to disk before registering — segment must be on disk before it's visible
    int err = segment_write_to_disk(seg, store->db_path);
    if (err != FE_OK) {
        seg->block = NULL; // active_block still owned by store
        segment_destroy(seg);
        return err;
    }

    err = grow_segments_array(store);
    if (err != FE_OK) {
        seg->block = NULL;
        segment_destroy(seg);
        return err;
    }

    store->next_segment_id++;
    register_segment(store, seg);
    wal_truncate(store->wal);

    store->last_flush_time_ms = current_time_ms();

    // Fresh active_block for the next batch (segment now owns the old one)
    store->active_block = create_event_block(0);
    if (!store->active_block) return FE_OUT_OF_MEMORY;

    return FE_OK;
}