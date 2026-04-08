#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/internal.h"

#define TEST_DB "./test_store_tmp"

static int passed = 0, failed = 0;

#define CHECK(name, cond) do { \
    if (cond) { printf("  PASS: %s\n", name); passed++; } \
    else      { printf("  FAIL: %s\n", name); failed++; } \
} while(0)

static void cleanup(void) { system("rm -rf " TEST_DB); }

// ── Tests ──────────────────────────────────────────────────────────────────────

static void test_open_close(void) {
    printf("test_open_close\n");
    cleanup();
    EventStore* s = event_store_open(TEST_DB);
    CHECK("open returns non-null",          s != NULL);
    CHECK("active block exists",            s->active_block != NULL);
    CHECK("no segments on fresh open",      s->num_segments == 0);
    CHECK("size is 0 on fresh open",        event_store_size(s) == 0);
    event_store_close(s);
    cleanup();
}

static void test_append_basic(void) {
    printf("test_append_basic\n");
    cleanup();
    EventStore* s = event_store_open(TEST_DB);
    CHECK("append 1", event_store_append(s, "click", 1, 1000, NULL) == FE_OK);
    CHECK("append 2", event_store_append(s, "view",  2, 2000, "{\"page\":\"/\"}") == FE_OK);
    CHECK("size is 2",           event_store_size(s) == 2);
    CHECK("active block has 2",  s->active_block->count == 2);
    CHECK("no segments yet",     s->num_segments == 0);
    event_store_close(s);
    cleanup();
}

static void test_filter_by_type(void) {
    printf("test_filter_by_type\n");
    cleanup();
    EventStore* s = event_store_open(TEST_DB);
    event_store_append(s, "click", 1, 1000, NULL);
    event_store_append(s, "click", 2, 2000, NULL);
    event_store_append(s, "view",  3, 3000, NULL);

    QueryResult* r = event_store_filter(s, "click", 0, 0, 0);
    CHECK("click count == 2",      r != NULL && r->count == 2);
    query_result_destroy(r);

    r = event_store_filter(s, "view", 0, 0, 0);
    CHECK("view count == 1",       r != NULL && r->count == 1);
    query_result_destroy(r);

    r = event_store_filter(s, "unknown", 0, 0, 0);
    CHECK("unknown type count == 0", r != NULL && r->count == 0);
    query_result_destroy(r);

    event_store_close(s);
    cleanup();
}

static void test_filter_by_user(void) {
    printf("test_filter_by_user\n");
    cleanup();
    EventStore* s = event_store_open(TEST_DB);
    event_store_append(s, "click", 1, 1000, NULL);
    event_store_append(s, "click", 2, 2000, NULL);
    event_store_append(s, "click", 1, 3000, NULL);

    QueryResult* r = event_store_filter(s, NULL, 1, 0, 0);
    CHECK("user 1 has 2 events", r != NULL && r->count == 2);
    query_result_destroy(r);

    r = event_store_filter(s, NULL, 2, 0, 0);
    CHECK("user 2 has 1 event",  r != NULL && r->count == 1);
    query_result_destroy(r);

    event_store_close(s);
    cleanup();
}

static void test_filter_by_time(void) {
    printf("test_filter_by_time\n");
    cleanup();
    EventStore* s = event_store_open(TEST_DB);
    event_store_append(s, "e", 1, 1000, NULL);
    event_store_append(s, "e", 1, 2000, NULL);
    event_store_append(s, "e", 1, 3000, NULL);
    event_store_append(s, "e", 1, 4000, NULL);

    QueryResult* r = event_store_filter(s, NULL, 0, 2000, 3000);
    CHECK("range [2000,3000] == 2", r != NULL && r->count == 2);
    query_result_destroy(r);

    r = event_store_filter(s, NULL, 0, 3000, 0);
    CHECK("start_ts=3000 == 2",    r != NULL && r->count == 2);
    query_result_destroy(r);

    r = event_store_filter(s, NULL, 0, 0, 2000);
    CHECK("end_ts=2000 == 2",      r != NULL && r->count == 2);
    query_result_destroy(r);

    event_store_close(s);
    cleanup();
}

static void test_filter_no_criteria(void) {
    printf("test_filter_no_criteria\n");
    cleanup();
    EventStore* s = event_store_open(TEST_DB);
    event_store_append(s, "a", 1, 1000, NULL);
    event_store_append(s, "b", 2, 2000, NULL);
    event_store_append(s, "c", 3, 3000, NULL);

    QueryResult* r = event_store_filter(s, NULL, 0, 0, 0);
    CHECK("all 3 returned", r != NULL && r->count == 3);
    query_result_destroy(r);

    event_store_close(s);
    cleanup();
}

static void test_flush_creates_segment(void) {
    printf("test_flush_creates_segment\n");
    cleanup();
    EventStore* s = event_store_open(TEST_DB);
    event_store_append(s, "click", 1, 1000, NULL);
    event_store_append(s, "view",  2, 2000, NULL);

    CHECK("no segments before flush",          s->num_segments == 0);
    CHECK("flush returns FE_OK",               event_store_flush(s) == FE_OK);
    CHECK("one segment after flush",           s->num_segments == 1);
    CHECK("active block empty after flush",    s->active_block->count == 0);
    CHECK("size still 2 after flush",          event_store_size(s) == 2);

    event_store_close(s);
    cleanup();
}

static void test_filter_across_flush(void) {
    printf("test_filter_across_flush\n");
    cleanup();
    EventStore* s = event_store_open(TEST_DB);

    event_store_append(s, "click", 1, 1000, NULL);
    event_store_append(s, "click", 2, 2000, NULL);
    event_store_flush(s);                             // goes to seg_00001.dat

    event_store_append(s, "click", 3, 3000, NULL);   // stays in active_block

    QueryResult* r = event_store_filter(s, "click", 0, 0, 0);
    CHECK("spans segment + active block", r != NULL && r->count == 3);
    query_result_destroy(r);

    event_store_close(s);
    cleanup();
}

static void test_wal_recovery(void) {
    printf("test_wal_recovery\n");
    cleanup();

    // Close normally without flushing: WAL has the events, no segment written
    EventStore* s = event_store_open(TEST_DB);
    event_store_append(s, "click", 1, 1000, NULL);
    event_store_append(s, "view",  2, 2000, NULL);
    event_store_close(s);   // flushes WAL buffer → wal.log, no segment

    // Reopen — WAL recovery should restore both events into active_block
    s = event_store_open(TEST_DB);
    CHECK("size recovered from WAL",       event_store_size(s) == 2);
    CHECK("events in active_block",        s->active_block->count == 2);
    CHECK("no segments (never flushed)",   s->num_segments == 0);

    QueryResult* r = event_store_filter(s, "click", 0, 0, 0);
    CHECK("recovered event is queryable",  r != NULL && r->count == 1);
    query_result_destroy(r);

    event_store_close(s);
    cleanup();
}

static void test_dict_persistence(void) {
    printf("test_dict_persistence\n");
    cleanup();

    EventStore* s = event_store_open(TEST_DB);
    event_store_append(s, "purchase", 1, 1000, NULL);
    event_store_flush(s);   // type_id is now baked into seg_00001.dat
    event_store_close(s);

    // Reopen — dict must reload so the stored type_id is still valid
    s = event_store_open(TEST_DB);
    uint32_t id = string_dict_get(s->event_dict, "purchase");
    CHECK("type id survived close/reopen", id != UINT32_MAX);
    CHECK("type id is 0 (first type)",     id == 0);
    event_store_close(s);
    cleanup();
}

static void test_properties_roundtrip(void) {
    printf("test_properties_roundtrip\n");
    cleanup();
    EventStore* s = event_store_open(TEST_DB);
    event_store_append(s, "click", 1, 1000, "{\"btn\":\"signup\"}");

    QueryResult* r = event_store_filter(s, "click", 0, 0, 0);
    CHECK("result has 1 row",           r != NULL && r->count == 1);
    if (r && r->count == 1)
        CHECK("properties match",
              strcmp(r->properties[0], "{\"btn\":\"signup\"}") == 0);
    query_result_destroy(r);

    event_store_close(s);
    cleanup();
}

static void test_properties_roundtrip_after_flush(void) {
    printf("test_properties_roundtrip_after_flush\n");
    cleanup();
    EventStore* s = event_store_open(TEST_DB);
    event_store_append(s, "click", 1, 1000, "{\"btn\":\"signup\"}");
    event_store_flush(s);

    QueryResult* r = event_store_filter(s, "click", 0, 0, 0);
    CHECK("result has 1 row after flush", r != NULL && r->count == 1);
    if (r && r->count == 1)
        CHECK("properties survive flush",
              strcmp(r->properties[0], "{\"btn\":\"signup\"}") == 0);
    query_result_destroy(r);

    event_store_close(s);
    cleanup();
}

static void test_multiple_segments(void) {
    printf("test_multiple_segments\n");
    cleanup();
    EventStore* s = event_store_open(TEST_DB);

    event_store_append(s, "a", 1, 1000, NULL);
    event_store_flush(s);
    event_store_append(s, "b", 2, 2000, NULL);
    event_store_flush(s);
    event_store_append(s, "c", 3, 3000, NULL);  // still in active_block

    CHECK("two segments",         s->num_segments == 2);
    CHECK("total size is 3",      event_store_size(s) == 3);

    QueryResult* r = event_store_filter(s, NULL, 0, 0, 0);
    CHECK("all 3 events found",   r != NULL && r->count == 3);
    query_result_destroy(r);

    event_store_close(s);
    cleanup();
}

// ── Main ───────────────────────────────────────────────────────────────────────

int main(void) {
    printf("=== Store Tests ===\n");
    test_open_close();
    test_append_basic();
    test_filter_by_type();
    test_filter_by_user();
    test_filter_by_time();
    test_filter_no_criteria();
    test_flush_creates_segment();
    test_filter_across_flush();
    test_wal_recovery();
    test_dict_persistence();
    test_properties_roundtrip();
    test_properties_roundtrip_after_flush();
    test_multiple_segments();
    printf("\nResults: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
