#define _POSIX_C_SOURCE 200809L
#include "../src/internal.h"
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

void test_create_destroy() {
    EventBlock* block = create_event_block(100);
    assert(block != NULL);
    assert(block->count == 0);
    assert(block->capacity == 100);
    assert(block->event_type_ids != NULL);
    assert(block->user_ids != NULL);
    assert(block->timestamps != NULL);
    assert(block->properties != NULL);
    assert(block->min_timestamp == UINT64_MAX);
    assert(block->max_timestamp == 0);

    destroy_event_block(block);
    printf("✓ test_create_destroy\n");
}

void test_default_capacity() {
    EventBlock* block = create_event_block(0);  // Use default
    assert(block != NULL);
    assert(block->capacity == 10000);  // DEFAULT_BLOCK_CAPACITY

    destroy_event_block(block);
    printf("✓ test_default_capacity\n");
}

void test_custom_capacity() {
    EventBlock* block = create_event_block(5000);
    assert(block != NULL);
    assert(block->capacity == 5000);

    destroy_event_block(block);
    printf("✓ test_custom_capacity\n");
}

void test_stats_empty() {
    EventBlock* block = create_event_block(100);

    printf("\n");
    event_block_stats(block);
    printf("\n");

    destroy_event_block(block);
    printf("✓ test_stats_empty\n");
}

void test_manual_append() {
    // Manually append an event to test the structure
    EventBlock* block = create_event_block(10);

    // Simulate appending an event
    block->event_type_ids[0] = 0;
    block->user_ids[0] = 123;
    block->timestamps[0] = 1000000;
    block->properties[0] = NULL;
    block->count = 1;
    block->min_timestamp = 1000000;
    block->max_timestamp = 1000000;

    assert(block->count == 1);
    assert(block->event_type_ids[0] == 0);
    assert(block->user_ids[0] == 123);

    destroy_event_block(block);
    printf("✓ test_manual_append\n");
}

void test_properties_memory() {
    EventBlock* block = create_event_block(10);

    // Add some properties
    block->properties[0] = strdup("{\"page\":\"/home\"}");
    block->properties[1] = strdup("{\"button\":\"signup\"}");
    block->count = 2;

    assert(block->properties[0] != NULL);
    assert(block->properties[1] != NULL);

    // destroy_event_block should free these
    destroy_event_block(block);
    printf("✓ test_properties_memory\n");
}

void test_large_capacity() {
    EventBlock* block = create_event_block(100000);
    assert(block != NULL);
    assert(block->capacity == 100000);

    printf("\n");
    event_block_stats(block);
    printf("\n");

    destroy_event_block(block);
    printf("✓ test_large_capacity\n");
}

int main() {
    printf("Running EventBlock tests...\n\n");

    test_create_destroy();
    test_default_capacity();
    test_custom_capacity();
    test_stats_empty();
    test_manual_append();
    test_properties_memory();
    test_large_capacity();

    printf("\n✅ All EventBlock tests passed!\n");
    return 0;
}
