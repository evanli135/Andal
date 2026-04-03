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

// Get ID for string, or add if not exists
int string_dict_get_or_add(StringDict* dict, const char* str, uint32_t* out_id) {
    if (!dict || !str || !out_id) {
        return FE_INVALID_ARG;
    }

    // Check load factor - resize if needed
    double load_factor = (double)dict->count / dict->capacity;
    if (load_factor > MAX_LOAD_FACTOR) {
        int err = string_dict_resize(dict);
        if (err != FE_OK) return err;
    }

    // Find slot
    bool found;
    size_t slot = string_dict_find_slot(dict, str, &found);

    if (found) {
        // String already exists
        *out_id = dict->ids[slot];
        return FE_OK;
    }

    // Add new entry
    dict->strings[slot] = strdup(str);
    if (!dict->strings[slot]) {
        return FE_OUT_OF_MEMORY;
    }

    // Assign next sequential ID
    uint32_t new_id = (uint32_t)dict->count;
    dict->ids[slot] = new_id;
    dict->count++;

    *out_id = new_id;
    return FE_OK;
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

// Print dictionary stats (for debugging)
void string_dict_stats(const StringDict* dict) {
    if (!dict) return;

    printf("=== StringDict Stats ===\n");
    printf("Entries: %zu / %zu (%.1f%% full)\n",
           dict->count, dict->capacity,
           100.0 * dict->count / dict->capacity);
    printf("Load factor: %.2f (max %.2f)\n",
           (double)dict->count / dict->capacity,
           MAX_LOAD_FACTOR);

    // Calculate probe statistics
    size_t total_probes = 0;
    size_t max_probe = 0;

    for (size_t i = 0; i < dict->capacity; i++) {
        if (dict->strings[i] != NULL) {
            uint32_t hash = hash_string(dict->strings[i]);
            size_t ideal_slot = hash & (dict->capacity - 1);
            size_t actual_slot = i;

            size_t probes;
            if (actual_slot >= ideal_slot) {
                probes = actual_slot - ideal_slot;
            } else {
                probes = (dict->capacity - ideal_slot) + actual_slot;
            }

            total_probes += probes;
            if (probes > max_probe) {
                max_probe = probes;
            }
        }
    }

    printf("Avg probe length: %.2f\n",
           dict->count > 0 ? (double)total_probes / dict->count : 0);
    printf("Max probe length: %zu\n", max_probe);

    printf("\nEntries:\n");
    for (size_t i = 0; i < dict->capacity && dict->count <= 20; i++) {
        if (dict->strings[i] != NULL) {
            printf("  [%zu] \"%s\" -> %u\n", i, dict->strings[i], dict->ids[i]);
        }
    }
    if (dict->count > 20) {
        printf("  ... (%zu total entries)\n", dict->count);
    }
}
