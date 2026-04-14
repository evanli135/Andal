#define SEG_MAGIC "EVTSEG\0\0"
#define SEG_VERSION 1

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>

// O_BINARY is a MinGW/Windows extension; on POSIX it doesn't exist
#ifndef O_BINARY
#define O_BINARY 0
#endif

// fdatasync isn't available on MinGW; _commit from <io.h> is equivalent
#ifdef _WIN32
#include <io.h>
#define datasync(fd) _commit(fd)
#else
#define datasync(fd) fdatasync(fd)
#endif

#include "internal.h"
#include "fastevents.h"

typedef struct {
    uint8_t  magic[8];
    uint32_t version;
    uint64_t event_count;
    uint64_t min_timestamp;
    uint64_t max_timestamp;
    uint64_t offset_type_ids;
    uint64_t offset_user_ids;
    uint64_t offset_timestamps;
    uint64_t offset_prop_index;
    uint64_t offset_prop_heap;
    uint64_t prop_heap_size;
} SegmentHeader;

// ── I/O primitives ────────────────────────────────────────────────────────────

static int read_buf(int fd, void* buf, size_t size) {
    if (read(fd, buf, size) != (ssize_t)size) return FE_IO_ERROR;
    return FE_OK;
}

static int write_buf(int fd, const void* buf, size_t size) {
    if (write(fd, buf, size) != (ssize_t)size) return FE_IO_ERROR;
    return FE_OK;
}

// ── Write helpers ─────────────────────────────────────────────────────────────

// Record byte offsets for each property string; return total heap size.
static size_t build_prop_offsets(EventBlock* block, uint32_t* prop_offsets) {
    size_t heap_size = 0;

    for (size_t i = 0; i < block->count; i++) {
        if (block->properties[i]) {
            prop_offsets[i]  = (uint32_t)heap_size;
            heap_size       += strlen(block->properties[i]) + 1;
        } else {
            prop_offsets[i] = UINT32_MAX; // sentinel for NULL
        }
    }

    return heap_size;
}

// Allocate a contiguous buffer and pack all property strings into it.
static char* build_prop_heap(EventBlock* block, size_t heap_size) {
    if (heap_size == 0) return NULL;

    char* heap = malloc(heap_size);
    if (!heap) return NULL;

    size_t cursor = 0;
    for (size_t i = 0; i < block->count; i++) {
        if (block->properties[i]) {
            size_t len = strlen(block->properties[i]) + 1;
            memcpy(heap + cursor, block->properties[i], len);
            cursor += len;
        }
    }

    return heap;
}

static SegmentHeader build_header(EventBlock* b, size_t heap_size) {
    SegmentHeader hdr = {0};

    uint64_t off_type_ids   = sizeof(SegmentHeader);
    uint64_t off_user_ids   = off_type_ids   + b->count * sizeof(uint32_t);
    uint64_t off_timestamps = off_user_ids   + b->count * sizeof(uint64_t);
    uint64_t off_prop_index = off_timestamps + b->count * sizeof(uint64_t);
    uint64_t off_prop_heap  = off_prop_index + b->count * sizeof(uint32_t);

    memcpy(hdr.magic, SEG_MAGIC, 8);
    hdr.version           = SEG_VERSION;
    hdr.event_count       = b->count;
    hdr.min_timestamp     = b->min_timestamp;
    hdr.max_timestamp     = b->max_timestamp;
    hdr.offset_type_ids   = off_type_ids;
    hdr.offset_user_ids   = off_user_ids;
    hdr.offset_timestamps = off_timestamps;
    hdr.offset_prop_index = off_prop_index;
    hdr.offset_prop_heap  = off_prop_heap;
    hdr.prop_heap_size    = heap_size;

    return hdr;
}

static int write_columns(int fd, EventBlock* b) {
    if (write_buf(fd, b->event_type_ids, b->count * sizeof(uint32_t)) != FE_OK) return FE_IO_ERROR;
    if (write_buf(fd, b->user_ids,       b->count * sizeof(uint64_t)) != FE_OK) return FE_IO_ERROR;
    if (write_buf(fd, b->timestamps,     b->count * sizeof(uint64_t)) != FE_OK) return FE_IO_ERROR;
    return FE_OK;
}

static int write_to_file(
    const char*    path,
    SegmentHeader* hdr,
    EventBlock*    b,
    uint32_t*      prop_offsets,
    char*          prop_heap,
    size_t         heap_size
) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) return FE_IO_ERROR;

    int err = FE_OK;

    if (write_buf(fd, hdr, sizeof(SegmentHeader))               != FE_OK) { err = FE_IO_ERROR; goto cleanup; }
    if (write_columns(fd, b)                                    != FE_OK) { err = FE_IO_ERROR; goto cleanup; }
    if (write_buf(fd, prop_offsets, b->count * sizeof(uint32_t)) != FE_OK) { err = FE_IO_ERROR; goto cleanup; }
    if (heap_size > 0) {
        if (write_buf(fd, prop_heap, heap_size)                 != FE_OK) { err = FE_IO_ERROR; goto cleanup; }
    }
    if (datasync(fd) != 0) err = FE_IO_ERROR;

cleanup:
    close(fd);
    return err;
}

// ── Read helpers ──────────────────────────────────────────────────────────────

static int read_and_validate_header(int fd, SegmentHeader* hdr) {
    if (read_buf(fd, hdr, sizeof(*hdr)) != FE_OK)  return FE_IO_ERROR;
    if (memcmp(hdr->magic, SEG_MAGIC, 8) != 0)     return FE_CORRUPT_DATA;
    if (hdr->version != SEG_VERSION)               return FE_CORRUPT_DATA;
    return FE_OK;
}

static int read_columns(int fd, EventBlock* b, size_t n) {
    if (read_buf(fd, b->event_type_ids, n * sizeof(uint32_t)) != FE_OK) return FE_IO_ERROR;
    if (read_buf(fd, b->user_ids,       n * sizeof(uint64_t)) != FE_OK) return FE_IO_ERROR;
    if (read_buf(fd, b->timestamps,     n * sizeof(uint64_t)) != FE_OK) return FE_IO_ERROR;
    return FE_OK;
}

// Reconstruct per-event property strings from the prop index + heap.
// Increments block->count as each entry is set so destroy_event_block
// can clean up correctly on an early error.
static int read_prop_strings(EventBlock* block, uint32_t* prop_offsets, const char* prop_heap, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (prop_offsets[i] == UINT32_MAX) {
            block->properties[i] = NULL;
        } else {
            const char* src = prop_heap + prop_offsets[i];
            size_t len = strlen(src) + 1;
            block->properties[i] = malloc(len);
            if (!block->properties[i]) return FE_OUT_OF_MEMORY;
            memcpy(block->properties[i], src, len);
        }
        block->count = i + 1;
    }
    return FE_OK;
}

// ── Public API ────────────────────────────────────────────────────────────────

int segment_write_to_disk(Segment* seg, const char* dir) {
    if (!seg || !dir || !seg->block) return FE_INVALID_ARG;

    EventBlock* b = seg->block;

    uint32_t* prop_offsets = malloc(b->count * sizeof(uint32_t));
    if (!prop_offsets) return FE_OUT_OF_MEMORY;

    size_t heap_size = build_prop_offsets(b, prop_offsets);

    char* prop_heap = build_prop_heap(b, heap_size);
    if (heap_size > 0 && !prop_heap) {
        free(prop_offsets);
        return FE_OUT_OF_MEMORY;
    }

    SegmentHeader hdr = build_header(b, heap_size);
    int err = write_to_file(seg->file_path, &hdr, b, prop_offsets, prop_heap, heap_size);

    if (err == FE_OK) {
        seg->min_timestamp = b->min_timestamp;
        seg->max_timestamp = b->max_timestamp;
        seg->event_count   = b->count;
    }

    free(prop_offsets);
    free(prop_heap);
    return err;
}

// Read column arrays and property strings from fd into a pre-allocated block.
static int read_block_data(int fd, size_t n, size_t prop_heap_size, EventBlock* block) {
    if (read_columns(fd, block, n) != FE_OK) return FE_IO_ERROR;

    uint32_t* prop_offsets = malloc(n * sizeof(uint32_t));
    if (!prop_offsets) return FE_OUT_OF_MEMORY;

    char* prop_heap = NULL;
    int err = FE_OK;

    if (read_buf(fd, prop_offsets, n * sizeof(uint32_t)) != FE_OK) {
        err = FE_IO_ERROR; goto done;
    }

    if (prop_heap_size > 0) {
        prop_heap = malloc(prop_heap_size);
        if (!prop_heap) { err = FE_OUT_OF_MEMORY; goto done; }
        if (read_buf(fd, prop_heap, prop_heap_size) != FE_OK) { err = FE_IO_ERROR; goto done; }
    }

    err = read_prop_strings(block, prop_offsets, prop_heap, n);

done:
    free(prop_offsets);
    free(prop_heap);
    return err;
}

int segment_peek_metadata(Segment* seg) {
    if (!seg || !seg->file_path) return FE_INVALID_ARG;

    int fd = open(seg->file_path, O_RDONLY | O_BINARY);
    if (fd < 0) return FE_IO_ERROR;

    SegmentHeader hdr;
    int err = read_and_validate_header(fd, &hdr);
    close(fd);
    if (err != FE_OK) return err;

    seg->min_timestamp = hdr.min_timestamp;
    seg->max_timestamp = hdr.max_timestamp;
    seg->event_count   = hdr.event_count;
    seg->is_loaded     = false;
    seg->block         = NULL;
    return FE_OK;
}

int segment_load_from_disk(Segment* seg) {
    if (!seg || !seg->file_path) return FE_INVALID_ARG;

    int fd = open(seg->file_path, O_RDONLY | O_BINARY);
    if (fd < 0) return FE_IO_ERROR;

    SegmentHeader hdr;
    int err = read_and_validate_header(fd, &hdr);
    if (err != FE_OK) { close(fd); return err; }

    size_t n = (size_t)hdr.event_count;
    EventBlock* block = create_event_block(n ? n : 1);
    if (!block) { close(fd); return FE_OUT_OF_MEMORY; }

    if (n > 0) {
        err = read_block_data(fd, n, (size_t)hdr.prop_heap_size, block);
        if (err != FE_OK) { destroy_event_block(block); close(fd); return err; }
    }

    block->min_timestamp = hdr.min_timestamp;
    block->max_timestamp = hdr.max_timestamp;
    seg->block         = block;
    seg->is_loaded     = true;
    seg->min_timestamp = hdr.min_timestamp;
    seg->max_timestamp = hdr.max_timestamp;
    seg->event_count   = hdr.event_count;

    close(fd);
    return FE_OK;
}

