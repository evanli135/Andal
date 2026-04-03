#include "../src/storage.h"
#include <stdio.h>
#include <assert.h>
#include <time.h>

void test_create_destroy() {
    EventStore* store = event_store_create(100);
    assert(store != NULL);
    assert(event_store_size(store) == 0);
    event_store_destroy(store);
    printf("✓ test_create_destroy\n");
}

void test_append_events() {
    EventStore* store = event_store_create(10);

    uint64_t now = (uint64_t)time(NULL) * 1000;

    int err = event_store_append(store, "page_view", 123, now, "{\"page\":\"/home\"}");
    assert(err == FE_OK);
    assert(event_store_size(store) == 1);

    err = event_store_append(store, "click", 456, now + 1000, "{\"button\":\"signup\"}");
    assert(err == FE_OK);
    assert(event_store_size(store) == 2);

    event_store_destroy(store);
    printf("✓ test_append_events\n");
}

void test_filter_by_type() {
    EventStore* store = event_store_create(100);
    uint64_t now = (uint64_t)time(NULL) * 1000;

    event_store_append(store, "page_view", 123, now, NULL);
    event_store_append(store, "click", 123, now + 1000, NULL);
    event_store_append(store, "page_view", 456, now + 2000, NULL);

    QueryFilter filter = {
        .event_type = "page_view",
        .user_id = 0,
        .filter_user = false,
        .start_time = 0,
        .end_time = 0
    };

    QueryResult* result = event_store_filter(store, &filter);
    assert(result != NULL);
    assert(result->count == 2);

    query_result_destroy(result);
    event_store_destroy(store);
    printf("✓ test_filter_by_type\n");
}

void test_filter_by_user() {
    EventStore* store = event_store_create(100);
    uint64_t now = (uint64_t)time(NULL) * 1000;

    event_store_append(store, "page_view", 123, now, NULL);
    event_store_append(store, "click", 123, now + 1000, NULL);
    event_store_append(store, "page_view", 456, now + 2000, NULL);

    QueryFilter filter = {
        .event_type = NULL,
        .user_id = 123,
        .filter_user = true,
        .start_time = 0,
        .end_time = 0
    };

    QueryResult* result = event_store_filter(store, &filter);
    assert(result != NULL);
    assert(result->count == 2);

    query_result_destroy(result);
    event_store_destroy(store);
    printf("✓ test_filter_by_user\n");
}

void test_filter_combined() {
    EventStore* store = event_store_create(100);
    uint64_t now = (uint64_t)time(NULL) * 1000;

    event_store_append(store, "page_view", 123, now, NULL);
    event_store_append(store, "click", 123, now + 1000, NULL);
    event_store_append(store, "page_view", 456, now + 2000, NULL);

    QueryFilter filter = {
        .event_type = "page_view",
        .user_id = 123,
        .filter_user = true,
        .start_time = 0,
        .end_time = 0
    };

    QueryResult* result = event_store_filter(store, &filter);
    assert(result != NULL);
    assert(result->count == 1);

    query_result_destroy(result);
    event_store_destroy(store);
    printf("✓ test_filter_combined\n");
}

void test_large_dataset() {
    EventStore* store = event_store_create(100);
    uint64_t now = (uint64_t)time(NULL) * 1000;

    // Add 10K events
    for (int i = 0; i < 10000; i++) {
        const char* type = (i % 3 == 0) ? "page_view" :
                          (i % 3 == 1) ? "click" : "purchase";
        event_store_append(store, type, i % 100, now + i, NULL);
    }

    assert(event_store_size(store) == 10000);

    printf("\n");
    event_store_stats(store);
    printf("\n");

    event_store_destroy(store);
    printf("✓ test_large_dataset\n");
}

int main() {
    printf("Running storage tests...\n\n");

    test_create_destroy();
    test_append_events();
    test_filter_by_type();
    test_filter_by_user();
    test_filter_combined();
    test_large_dataset();

    printf("\n✅ All tests passed!\n");
    return 0;
}
