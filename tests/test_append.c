#define _POSIX_C_SOURCE 200809L
#include "../src/internal.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_append_single() {
    EventBlock* block = create_event_block(10);

    int err = append_to_block(block, 0, 123, 1000, NULL);
    assert(err == FE_OK);
    assert(block->count == 1);
    assert(block->event_type_ids[0] == 0);
    assert(block->user_ids[0] == 123);
    assert(block->timestamps[0] == 1000);
    assert(block->properties[0] == NULL);

    destroy_event_block(block);
    printf("✓ test_append_single\n");
}

void test_append_multiple() {
    EventBlock* block = create_event_block(10);

    append_to_block(block, 0, 123, 1000, NULL);
    append_to_block(block, 1, 456, 2000, NULL);
    append_to_block(block, 2, 789, 3000, NULL);

    assert(block->count == 3);
    assert(block->user_ids[0] == 123);
    assert(block->user_ids[1] == 456);
    assert(block->user_ids[2] == 789);

    destroy_event_block(block);
    printf("✓ test_append_multiple\n");
}

void test_append_with_properties() {
    EventBlock* block = create_event_block(10);

    const char* json = "{\"page\":\"/home\"}";
    int err = append_to_block(block, 0, 123, 1000, json);

    assert(err == FE_OK);
    assert(block->properties[0] != NULL);
    assert(strcmp(block->properties[0], json) == 0);
    // Verify it's a copy, not the same pointer
    assert(block->properties[0] != json);

    destroy_event_block(block);
    printf("✓ test_append_with_properties\n");
}

void test_append_empty_properties() {
    EventBlock* block = create_event_block(10);

    // Empty string should store NULL
    int err = append_to_block(block, 0, 123, 1000, "");
    assert(err == FE_OK);
    assert(block->properties[0] == NULL);

    destroy_event_block(block);
    printf("✓ test_append_empty_properties\n");
}

void test_append_capacity_exceeded() {
    EventBlock* block = create_event_block(3);  // Small capacity

    // Fill the block
    assert(append_to_block(block, 0, 1, 1000, NULL) == FE_OK);
    assert(append_to_block(block, 0, 2, 2000, NULL) == FE_OK);
    assert(append_to_block(block, 0, 3, 3000, NULL) == FE_OK);
    assert(block->count == 3);

    // Try to add one more - should fail
    int err = append_to_block(block, 0, 4, 4000, NULL);
    assert(err == FE_CAPACITY_EXCEEDED);
    assert(block->count == 3);  // Count didn't increase

    destroy_event_block(block);
    printf("✓ test_append_capacity_exceeded\n");
}

void test_append_null_block() {
    int err = append_to_block(NULL, 0, 123, 1000, NULL);
    assert(err == FE_INVALID_ARG);
    printf("✓ test_append_null_block\n");
}

void test_metadata_updates() {
    EventBlock* block = create_event_block(10);

    // First event sets both min and max
    append_to_block(block, 0, 123, 5000, NULL);
    assert(block->min_timestamp == 5000);
    assert(block->max_timestamp == 5000);

    // Earlier timestamp updates min
    append_to_block(block, 0, 123, 1000, NULL);
    assert(block->min_timestamp == 1000);
    assert(block->max_timestamp == 5000);

    // Later timestamp updates max
    append_to_block(block, 0, 123, 9000, NULL);
    assert(block->min_timestamp == 1000);
    assert(block->max_timestamp == 9000);

    // Middle timestamp changes nothing
    append_to_block(block, 0, 123, 5000, NULL);
    assert(block->min_timestamp == 1000);
    assert(block->max_timestamp == 9000);

    destroy_event_block(block);
    printf("✓ test_metadata_updates\n");
}

void test_properties_memory_ownership() {
    EventBlock* block = create_event_block(10);

    char json[64] = "{\"page\":\"/home\"}";
    append_to_block(block, 0, 123, 1000, json);

    // Modify original string
    strcpy(json, "{\"page\":\"/changed\"}");

    // Block should still have original value
    assert(strcmp(block->properties[0], "{\"page\":\"/home\"}") == 0);

    destroy_event_block(block);
    printf("✓ test_properties_memory_ownership\n");
}

int main() {
    printf("Running append_to_block tests...\n\n");

    test_append_single();
    test_append_multiple();
    test_append_with_properties();
    test_append_empty_properties();
    test_append_capacity_exceeded();
    test_append_null_block();
    test_metadata_updates();
    test_properties_memory_ownership();

    printf("\n✅ All append tests passed!\n");
    return 0;
}
