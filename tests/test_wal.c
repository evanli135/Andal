#define _POSIX_C_SOURCE 200809L
#include "../src/wal.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

// Map WAL error codes
#define FE_OK WAL_OK
#define FE_INVALID_ARG WAL_INVALID_ARG

void test_create_destroy() {
    WAL* wal = wal_create("test_wal.log");
    assert(wal != NULL);
    assert(wal->buffer != NULL);
    assert(wal->buf_len == 0);
    assert(wal->event_count == 0);
    wal_destroy(wal);
    unlink("test_wal.log");
    printf("✓ test_create_destroy\n");
}

void test_append_single() {
    WAL* wal = wal_create("test_wal.log");

    uint8_t data[] = {1, 2, 3, 4, 5};
    int err = wal_append(wal, data, sizeof(data));
    assert(err == FE_OK);
    assert(wal->event_count == 1);
    assert(wal->buf_len > 0);  // Should have length prefix + data

    wal_destroy(wal);
    unlink("test_wal.log");
    printf("✓ test_append_single\n");
}

void test_append_multiple() {
    WAL* wal = wal_create("test_wal.log");

    uint8_t data1[] = {1, 2, 3};
    uint8_t data2[] = {4, 5, 6, 7};
    uint8_t data3[] = {8, 9};

    wal_append(wal, data1, sizeof(data1));
    wal_append(wal, data2, sizeof(data2));
    wal_append(wal, data3, sizeof(data3));

    assert(wal->event_count == 3);

    wal_destroy(wal);
    unlink("test_wal.log");
    printf("✓ test_append_multiple\n");
}

void test_flush() {
    WAL* wal = wal_create("test_wal.log");

    uint8_t data[] = {1, 2, 3, 4, 5};
    wal_append(wal, data, sizeof(data));

    size_t buf_len_before = wal->buf_len;
    assert(buf_len_before > 0);

    int err = wal_flush_to_disk(wal);
    assert(err == FE_OK);
    assert(wal->buf_len == 0);        // Buffer reset
    assert(wal->event_count == 0);    // Count reset

    wal_destroy(wal);
    unlink("test_wal.log");
    printf("✓ test_flush\n");
}

// Context for recovery callback
typedef struct {
    int count;
    uint8_t last_data[16];
    size_t last_len;
} RecoveryCtx;

void recovery_callback(const uint8_t* data, size_t len, void* ctx) {
    RecoveryCtx* rctx = (RecoveryCtx*)ctx;
    rctx->count++;
    if (len <= sizeof(rctx->last_data)) {
        memcpy(rctx->last_data, data, len);
        rctx->last_len = len;
    }
}

void test_recovery() {
    // Write some data and flush
    WAL* wal = wal_create("test_recovery.log");

    uint8_t data1[] = {1, 2, 3};
    uint8_t data2[] = {4, 5, 6, 7};

    wal_append(wal, data1, sizeof(data1));
    wal_append(wal, data2, sizeof(data2));
    wal_flush_to_disk(wal);
    wal_destroy(wal);

    // Recover from the same file
    wal = wal_create("test_recovery_new.log");  // Different file for write
    RecoveryCtx ctx = {0};

    // Open the original WAL to read from it
    WAL* recovery_wal = wal_create("test_recovery.log");
    int err = wal_recover(recovery_wal, recovery_callback, &ctx);

    assert(err == FE_OK);
    assert(ctx.count == 2);
    assert(ctx.last_len == sizeof(data2));
    assert(memcmp(ctx.last_data, data2, sizeof(data2)) == 0);

    wal_destroy(wal);
    wal_destroy(recovery_wal);
    unlink("test_recovery.log");
    unlink("test_recovery_new.log");
    printf("✓ test_recovery\n");
}

void test_buffer_growth() {
    WAL* wal = wal_create("test_wal.log");

    size_t initial_capacity = wal->buf_capacity;

    // Add large data to trigger growth
    uint8_t large_data[8192];
    memset(large_data, 0xAB, sizeof(large_data));

    for (int i = 0; i < 1000; i++) {
        wal_append(wal, large_data, sizeof(large_data));
    }

    assert(wal->buf_capacity > initial_capacity);  // Should have grown

    wal_destroy(wal);
    unlink("test_wal.log");
    printf("✓ test_buffer_growth\n");
}

int main() {
    printf("Running WAL tests...\n\n");

    test_create_destroy();
    test_append_single();
    test_append_multiple();
    test_flush();
    test_recovery();
    test_buffer_growth();

    printf("\n✅ All WAL tests passed!\n");
    return 0;
}
