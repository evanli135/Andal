#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INITIAL_DICT_CAPACITY 64
#define MAX_LOAD_FACTOR 0.7  // Resize when 70% full

// ============================================================================
// Hash Function
// ============================================================================

// djb2 hash algorithm
static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

// ============================================================================
// String Dictionary Implementation (Hash Table with Open Addressing)
// ============================================================================

// Create a string dictionary
StringDict* string_dict_create(size_t initial_capacity) {
    // Ensure capacity is power of 2 (makes modulo faster with & operation)
    if (initial_capacity == 0) {
        initial_capacity = INITIAL_DICT_CAPACITY;
    }

    // Round up to next power of 2
    size_t capacity = 1;
    while (capacity < initial_capacity) {
        capacity *= 2;
    }

    StringDict* dict = malloc(sizeof(StringDict));
    if (!dict) return NULL;

    dict->strings = calloc(capacity, sizeof(char*));
    dict->ids = calloc(capacity, sizeof(uint32_t));

    if (!dict->strings || !dict->ids) {
        free(dict->strings);
        free(dict->ids);
        free(dict);
        return NULL;
    }

    dict->count = 0;
    dict->capacity = capacity;
    return dict;
}

// Find slot for a string (returns index, modifies found flag)
static size_t string_dict_find_slot(
    const StringDict* dict,
    const char* str,
    bool* found
) {
    uint32_t hash = hash_string(str);
    size_t index = hash & (dict->capacity - 1);  // Fast modulo for power-of-2

    // Linear probing
    for (size_t i = 0; i < dict->capacity; i++) {
        size_t slot = (index + i) & (dict->capacity - 1);

        if (dict->strings[slot] == NULL) {
            // Empty slot - not found
            *found = false;
            return slot;
        }

        if (strcmp(dict->strings[slot], str) == 0) {
            // Found it!
            *found = true;
            return slot;
        }

        // Collision, continue probing
    }

    // Table is full (shouldn't happen with proper load factor)
    *found = false;
    return 0;
}

// Resize and rehash the dictionary
static int string_dict_resize(StringDict* dict) {
    size_t new_capacity = dict->capacity * 2;

    // Allocate new tables
    char** new_strings = calloc(new_capacity, sizeof(char*));
    uint32_t* new_ids = calloc(new_capacity, sizeof(uint32_t));

    if (!new_strings || !new_ids) {
        free(new_strings);
        free(new_ids);
        return FE_OUT_OF_MEMORY;
    }

    // Rehash all entries
    char** old_strings = dict->strings;
    uint32_t* old_ids = dict->ids;
    size_t old_capacity = dict->capacity;

    dict->strings = new_strings;
    dict->ids = new_ids;
    dict->capacity = new_capacity;
    size_t old_count = dict->count;
    dict->count = 0;  // Will be incremented during rehash

    for (size_t i = 0; i < old_capacity; i++) {
        if (old_strings[i] != NULL) {
            // Rehash this entry
            bool found;
            size_t slot = string_dict_find_slot(dict, old_strings[i], &found);

            dict->strings[slot] = old_strings[i];  // Transfer ownership
            dict->ids[slot] = old_ids[i];
            dict->count++;
        }
    }

    // Verify count
    if (dict->count != old_count) {
        fprintf(stderr, "ERROR: Lost entries during resize!\n");
        return FE_OUT_OF_MEMORY;
    }

    // Free old tables (but not the strings - they were transferred)
    free(old_strings);
    free(old_ids);

    return FE_OK;
}

// Look up a string and return its ID, or UINT32_MAX if not present.
uint32_t string_dict_get(const StringDict* dict, const char* str) {
    if (!dict || !str) return UINT32_MAX;

    bool found;
    size_t slot = string_dict_find_slot(dict, str, &found);
    return found ? dict->ids[slot] : UINT32_MAX;
}

// Add a new string and return its assigned ID, or UINT32_MAX on OOM.
// Behaviour is undefined if str is already in the dict.
uint32_t string_dict_add(StringDict* dict, const char* str) {
    if (!dict || !str) return UINT32_MAX;

    // Resize before inserting if we're above the load factor threshold
    double load_factor = (double)dict->count / dict->capacity;
    if (load_factor > MAX_LOAD_FACTOR) {
        if (string_dict_resize(dict) != FE_OK) return UINT32_MAX;
    }

    bool found;
    size_t slot = string_dict_find_slot(dict, str, &found);

    dict->strings[slot] = strdup(str);
    if (!dict->strings[slot]) return UINT32_MAX;

    uint32_t new_id = (uint32_t)dict->count;
    dict->ids[slot] = new_id;
    dict->count++;

    return new_id;
}

// Destroy dictionary
void string_dict_destroy(StringDict* dict) {
    if (!dict) return;

    for (size_t i = 0; i < dict->capacity; i++) {
        free(dict->strings[i]);
    }
    free(dict->strings);
    free(dict->ids);
    free(dict);
}

// Persist the dictionary to a plain text file — one "string id" line per entry.
// Entries are written in ID order so load restores the same ID assignments.
int string_dict_save(const StringDict* dict, const char* path) {
    if (!dict || !path) return FE_INVALID_ARG;

    FILE* f = fopen(path, "w");
    if (!f) return FE_IO_ERROR;

    // Build a sorted-by-id list so the file is in stable ID order
    // (hash table iteration order is not insertion order)
    const char** sorted = calloc(dict->count, sizeof(char*));
    if (!sorted) { fclose(f); return FE_OUT_OF_MEMORY; }

    for (size_t i = 0; i < dict->capacity; i++) {
        if (dict->strings[i])
            sorted[dict->ids[i]] = dict->strings[i];
    }

    int err = FE_OK;
    for (size_t id = 0; id < dict->count; id++) {
        if (fprintf(f, "%s %zu\n", sorted[id], id) < 0) {
            err = FE_IO_ERROR;
            break;
        }
    }

    free(sorted);
    fclose(f);
    return err;
}

// Load a dictionary previously saved with string_dict_save.
// Returns a new StringDict — caller owns it.
StringDict* string_dict_load(const char* path) {
    if (!path) return NULL;

    FILE* f = fopen(path, "r");
    if (!f) return NULL;

    StringDict* dict = string_dict_create(64);
    if (!dict) { fclose(f); return NULL; }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char name[1024];
        size_t id;
        if (sscanf(line, "%1023s %zu", name, &id) != 2) continue;

        uint32_t assigned = string_dict_add(dict, name);
        if (assigned == UINT32_MAX || assigned != (uint32_t)id) {
            // ID mismatch means the file is corrupt or out of order
            string_dict_destroy(dict);
            fclose(f);
            return NULL;
        }
    }

    fclose(f);
    return dict;
}

static void compute_probe_stats(const StringDict* dict, size_t* out_total, size_t* out_max) {
    *out_total = 0;
    *out_max   = 0;
    for (size_t i = 0; i < dict->capacity; i++) {
        if (!dict->strings[i]) continue;
        uint32_t hash  = hash_string(dict->strings[i]);
        size_t   ideal = hash & (dict->capacity - 1);
        size_t   probes = (i >= ideal) ? i - ideal : dict->capacity - ideal + i;
        *out_total += probes;
        if (probes > *out_max) *out_max = probes;
    }
}

static void print_entries(const StringDict* dict) {
    if (dict->count > 20) {
        printf("  ... (%zu total entries)\n", dict->count);
        return;
    }
    for (size_t i = 0; i < dict->capacity; i++) {
        if (dict->strings[i])
            printf("  [%zu] \"%s\" -> %u\n", i, dict->strings[i], dict->ids[i]);
    }
}

void string_dict_stats(const StringDict* dict) {
    if (!dict) return;

    size_t total_probes, max_probe;
    compute_probe_stats(dict, &total_probes, &max_probe);

    printf("=== StringDict Stats ===\n");
    printf("Entries: %zu / %zu (%.1f%% full)\n",
           dict->count, dict->capacity,
           100.0 * dict->count / dict->capacity);
    printf("Load factor: %.2f (max %.2f)\n",
           (double)dict->count / dict->capacity, MAX_LOAD_FACTOR);
    printf("Avg probe length: %.2f\n",
           dict->count > 0 ? (double)total_probes / dict->count : 0);
    printf("Max probe length: %zu\n", max_probe);
    printf("\nEntries:\n");
    print_entries(dict);
}
