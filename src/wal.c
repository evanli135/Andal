#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "wal.h"

#ifdef _WIN32
#include <io.h>
#define datasync(fd) _commit(fd)
#else
#define datasync(fd) fdatasync(fd)
#endif

// Map WAL error codes to storage error codes for compatibility
#define FE_OK WAL_OK
#define FE_INVALID_ARG WAL_INVALID_ARG
#define FE_OUT_OF_MEMORY WAL_OUT_OF_MEMORY
#define FE_IO_ERROR WAL_IO_ERROR

#define WAL_DEFAULT_BUFFER_CAPACITY (4 * 1024 * 1024)  // 4MB
#define WAL_DEFAULT_FLUSH_THRESHOLD 10000               // flush every 10k events

WAL* wal_create(const char* path) {
    WAL* wal = malloc(sizeof(WAL));
    if (!wal) return NULL;

    wal->buffer = malloc(WAL_DEFAULT_BUFFER_CAPACITY);
    if (!wal->buffer) {
        free(wal);
        return NULL;
    }

    wal->path = strdup(path);
    if (!wal->path) {
        free(wal->buffer);
        free(wal);
        return NULL;
    }

    wal->buf_len = 0;
    wal->event_count = 0;
    wal->buf_capacity = WAL_DEFAULT_BUFFER_CAPACITY;
    wal->flush_threshold = WAL_DEFAULT_FLUSH_THRESHOLD;  
    
    wal->fd = open(path, O_CREAT | O_WRONLY | O_APPEND, 0644); // Append mode
    if (wal->fd < 0) {
        free(wal->buffer);
        free(wal);
        return NULL;
    }

    return wal;
}


int wal_append(WAL* wal, const uint8_t* data, size_t len) {
    if (!wal || !data || len == 0) return FE_INVALID_ARG;

    // 4 bytes length prefix + length
    size_t entry_size = sizeof(uint32_t) + len;

    // If we don't have enough room in the buffer, grow it
    if (wal->buf_len + entry_size > wal->buf_capacity) {
        size_t new_capacity = wal->buf_capacity * 2;
        uint8_t* new_buffer = realloc(wal->buffer, new_capacity);

        if (!new_buffer) return FE_OUT_OF_MEMORY;

        wal->buffer = new_buffer;
        wal->buf_capacity = new_capacity; 
    }

    // Wrap with length prefix
    uint32_t len32 = (uint32_t) len;
    memcpy(wal->buffer + wal->buf_len, &len32, sizeof(uint32_t));
    wal->buf_len += sizeof(uint32_t);

    memcpy(wal->buffer + wal->buf_len, data, len);
    wal->buf_len += len;

    wal->event_count++;

    if (wal->event_count >= wal->flush_threshold) {
        return wal_flush_to_disk(wal);
    }

    return FE_OK;
}

int wal_flush_to_disk(WAL* wal) {
    if (!wal || wal->buf_len == 0) return FE_OK;

    ssize_t written = write(wal->fd, wal->buffer, wal->buf_len);
    if (written != (ssize_t) wal->buf_len) return FE_IO_ERROR;

    // blocks until the OS confirms the data has physically hit storage — not just the kernel page cache
    // write + fdatasync survives powerloss and process crash
    if (datasync(wal->fd) != 0) {
        return FE_IO_ERROR;
    }

    // Reset WAL cursor
    wal->buf_len = 0;
    wal->event_count = 0;

    return FE_OK;
}

int wal_truncate(WAL* wal) {
    if (!wal) return FE_INVALID_ARG;

    wal->buf_len = 0;
    wal->event_count = 0;

    close(wal->fd);
    FILE* f = fopen(wal->path, "w");
    if (!f) return FE_IO_ERROR;
    fclose(f);

    wal->fd = open(wal->path, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (wal->fd < 0) return FE_IO_ERROR;

    return FE_OK;
}

void wal_destroy(WAL* wal) {
    if (!wal) return;

    // Flush anything remaining in the staging buffer before freeing !!
    if (wal->buf_len > 0) {
        wal_flush_to_disk(wal);
    }

    close(wal->fd);
    free(wal->buffer);
    free(wal->path);  // Fixed: need to free the strdup'd path
    free(wal);
}

int wal_recover(WAL* wal, void (*on_event)(const uint8_t* data, size_t len, void* ctx),
    void* ctx) {
    if (!wal || !on_event) return FE_INVALID_ARG;

    // Open separate read-only fd — don't disturb the write fd
    int fd = open(wal->path, O_RDONLY);
    if (fd < 0) return FE_OK;  // No WAL file = nothing to recover, not an error

    uint32_t len32;
    uint8_t stack_buf[4096];  // Stack buffer avoids malloc for small events

    while (read(fd, &len32, sizeof(uint32_t)) == sizeof(uint32_t)) {
        size_t len = (size_t)len32;

        // Use stack buffer for small events, heap for large ones
        uint8_t* buf = (len <= sizeof(stack_buf)) ? stack_buf : malloc(len);
        if (!buf) {
            close(fd);
            return FE_OUT_OF_MEMORY;
        }

        ssize_t got = read(fd, buf, len);

        if (got == (ssize_t)len) {
            // Full entry — replay it
            on_event(buf, len, ctx);
        }

        if (buf != stack_buf) free(buf);

        // Truncated entry means the process crashed mid-write on this entry.
        // Everything before it was written cleanly, so stop here — don't error.
        if (got != (ssize_t)len) break;
    }

    close(fd);
    return FE_OK;
}