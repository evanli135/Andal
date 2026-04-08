#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <stdlib.h>
#include "../src/internal.h"
#include "../src/fastevents.h"

#define TMP_FILE "test_disk_tmp.dat"

// ── Helpers ───────────────────────────────────────────────────────────────────

static Segment* make_segment(EventBlock* block) {
    return segment_create(1, block, TMP_FILE);
}

static void cleanup_file(void) {
    unlink(TMP_FILE);
}

// ── Tests ─────────────────────────────────────────────────────────────────────

void test_write_invalid_args(void) {
    EventBlock* b = create_event_block(4);
    Segment* seg  = make_segment(b);

    assert(segment_write_to_disk(NULL, ".")   == FE_INVALID_ARG);
    assert(segment_write_to_disk(seg,  NULL)  == FE_INVALID_ARG);

    // NULL block
    seg->block = NULL;
    assert(segment_write_to_disk(seg,  ".")   == FE_INVALID_ARG);
    seg->block = b;

    segment_destroy(seg);
    printf("✓ test_write_invalid_args\n");
}

void test_load_invalid_args(void) {
    assert(segment_load_from_disk(NULL) == FE_INVALID_ARG);

    // NULL file_path
    Segment* seg = segment_create(1, NULL, "");
    free(seg->file_path);
    seg->file_path = NULL;
    assert(segment_load_from_disk(seg) == FE_INVALID_ARG);
    free(seg);

    printf("✓ test_load_invalid_args\n");
}

void test_load_missing_file(void) {
    unlink("nonexistent_segment.dat");
    Segment* seg = segment_create(1, NULL, "nonexistent_segment.dat");
    seg->is_loaded = false;
    assert(segment_load_from_disk(seg) == FE_IO_ERROR);
    free(seg->file_path);
    free(seg);
    printf("✓ test_load_missing_file\n");
}

void test_roundtrip_no_properties(void) {
    EventBlock* b = create_event_block(4);
    append_to_block(b, 1, 100, 1000, NULL);
    append_to_block(b, 2, 200, 2000, NULL);
    append_to_block(b, 3, 300, 3000, NULL);

    Segment* seg = make_segment(b);
    assert(segment_write_to_disk(seg, ".") == FE_OK);

    // Verify metadata was updated on the segment
    assert(seg->event_count   == 3);
    assert(seg->min_timestamp == 1000);
    assert(seg->max_timestamp == 3000);

    // Load into a fresh segment
    Segment* seg2 = segment_create(2, NULL, TMP_FILE);
    seg2->is_loaded = false;
    assert(segment_load_from_disk(seg2) == FE_OK);
    assert(seg2->is_loaded);

    EventBlock* b2 = seg2->block;
    assert(b2->count         == 3);
    assert(b2->min_timestamp == 1000);
    assert(b2->max_timestamp == 3000);

    assert(b2->event_type_ids[0] == 1); assert(b2->user_ids[0] == 100); assert(b2->timestamps[0] == 1000);
    assert(b2->event_type_ids[1] == 2); assert(b2->user_ids[1] == 200); assert(b2->timestamps[1] == 2000);
    assert(b2->event_type_ids[2] == 3); assert(b2->user_ids[2] == 300); assert(b2->timestamps[2] == 3000);

    assert(b2->properties[0] == NULL);
    assert(b2->properties[1] == NULL);
    assert(b2->properties[2] == NULL);

    segment_destroy(seg);
    segment_destroy(seg2);
    cleanup_file();
    printf("✓ test_roundtrip_no_properties\n");
}

void test_roundtrip_with_properties(void) {
    EventBlock* b = create_event_block(4);
    append_to_block(b, 10, 42, 5000, "{\"page\":\"/home\"}");
    append_to_block(b, 11, 43, 6000, "{\"button\":\"signup\"}");

    Segment* seg = make_segment(b);
    assert(segment_write_to_disk(seg, ".") == FE_OK);

    Segment* seg2 = segment_create(2, NULL, TMP_FILE);
    seg2->is_loaded = false;
    assert(segment_load_from_disk(seg2) == FE_OK);

    EventBlock* b2 = seg2->block;
    assert(b2->count == 2);
    assert(b2->properties[0] != NULL);
    assert(b2->properties[1] != NULL);
    assert(strcmp(b2->properties[0], "{\"page\":\"/home\"}")   == 0);
    assert(strcmp(b2->properties[1], "{\"button\":\"signup\"}") == 0);

    segment_destroy(seg);
    segment_destroy(seg2);
    cleanup_file();
    printf("✓ test_roundtrip_with_properties\n");
}

void test_roundtrip_mixed_properties(void) {
    EventBlock* b = create_event_block(4);
    append_to_block(b, 1, 1, 100, "{\"a\":1}");
    append_to_block(b, 2, 2, 200, NULL);
    append_to_block(b, 3, 3, 300, "{\"b\":2}");
    append_to_block(b, 4, 4, 400, NULL);

    Segment* seg = make_segment(b);
    assert(segment_write_to_disk(seg, ".") == FE_OK);

    Segment* seg2 = segment_create(2, NULL, TMP_FILE);
    seg2->is_loaded = false;
    assert(segment_load_from_disk(seg2) == FE_OK);

    EventBlock* b2 = seg2->block;
    assert(b2->count == 4);
    assert(b2->properties[0] != NULL); assert(strcmp(b2->properties[0], "{\"a\":1}") == 0);
    assert(b2->properties[1] == NULL);
    assert(b2->properties[2] != NULL); assert(strcmp(b2->properties[2], "{\"b\":2}") == 0);
    assert(b2->properties[3] == NULL);

    segment_destroy(seg);
    segment_destroy(seg2);
    cleanup_file();
    printf("✓ test_roundtrip_mixed_properties\n");
}

void test_roundtrip_many_events(void) {
    const int N = 500;
    EventBlock* b = create_event_block(N);
    for (int i = 0; i < N; i++) {
        append_to_block(b, (uint32_t)(i % 10), (uint64_t)i, (uint64_t)(i * 1000), NULL);
    }

    Segment* seg = make_segment(b);
    assert(segment_write_to_disk(seg, ".") == FE_OK);

    Segment* seg2 = segment_create(2, NULL, TMP_FILE);
    seg2->is_loaded = false;
    assert(segment_load_from_disk(seg2) == FE_OK);

    EventBlock* b2 = seg2->block;
    assert(b2->count == (size_t)N);
    for (int i = 0; i < N; i++) {
        assert(b2->event_type_ids[i] == (uint32_t)(i % 10));
        assert(b2->user_ids[i]       == (uint64_t)i);
        assert(b2->timestamps[i]     == (uint64_t)(i * 1000));
        assert(b2->properties[i]     == NULL);
    }

    segment_destroy(seg);
    segment_destroy(seg2);
    cleanup_file();
    printf("✓ test_roundtrip_many_events\n");
}

void test_corrupt_magic(void) {
    // Write a valid segment, then corrupt the magic bytes
    EventBlock* b = create_event_block(2);
    append_to_block(b, 1, 1, 100, NULL);
    Segment* seg = make_segment(b);
    assert(segment_write_to_disk(seg, ".") == FE_OK);
    segment_destroy(seg);

    // Overwrite the first 8 bytes with garbage
    FILE* f = fopen(TMP_FILE, "r+b");
    assert(f != NULL);
    fwrite("BADMAGIC", 8, 1, f);
    fclose(f);

    Segment* seg2 = segment_create(2, NULL, TMP_FILE);
    seg2->is_loaded = false;
    assert(segment_load_from_disk(seg2) == FE_CORRUPT_DATA);
    free(seg2->file_path);
    free(seg2);

    cleanup_file();
    printf("✓ test_corrupt_magic\n");
}

void test_wrong_version(void) {
    // Write valid segment, then flip the version field (bytes 8-11)
    EventBlock* b = create_event_block(2);
    append_to_block(b, 1, 1, 100, NULL);
    Segment* seg = make_segment(b);
    assert(segment_write_to_disk(seg, ".") == FE_OK);
    segment_destroy(seg);

    FILE* f = fopen(TMP_FILE, "r+b");
    assert(f != NULL);
    fseek(f, 8, SEEK_SET); // skip magic, seek to version
    uint32_t bad_version = 99;
    fwrite(&bad_version, sizeof(bad_version), 1, f);
    fclose(f);

    Segment* seg2 = segment_create(2, NULL, TMP_FILE);
    seg2->is_loaded = false;
    assert(segment_load_from_disk(seg2) == FE_CORRUPT_DATA);
    free(seg2->file_path);
    free(seg2);

    cleanup_file();
    printf("✓ test_wrong_version\n");
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(void) {
    printf("Running disk tests...\n\n");

    test_write_invalid_args();
    test_load_invalid_args();
    test_load_missing_file();
    test_roundtrip_no_properties();
    test_roundtrip_with_properties();
    test_roundtrip_mixed_properties();
    test_roundtrip_many_events();
    test_corrupt_magic();
    test_wrong_version();

    printf("\n✅ All disk tests passed!\n");
    return 0;
}
