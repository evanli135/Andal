#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"

#define HEADER_SIZE (sizeof(uint32_t) + sizeof(uint64_t) + sizeof(uint64_t))

// Encode event fields into a heap-allocated byte buffer for wal_append.
// Wire format: [4B type_id][8B user_id][8B timestamp][props (null-terminated, omitted if NULL)]
// Caller must free the returned buffer. Returns NULL on OOM.
uint8_t* event_encode(uint32_t event_type_id, uint64_t user_id,
                      uint64_t timestamp, const char* properties_json,
                      size_t* out_len) {
    size_t prop_len  = properties_json ? strlen(properties_json) + 1 : 0;
    size_t total_len = HEADER_SIZE + prop_len;

    uint8_t* buf = malloc(total_len);
    if (!buf) return NULL;

    size_t offset = 0;
    memcpy(buf + offset, &event_type_id, sizeof(uint32_t)); offset += sizeof(uint32_t);
    memcpy(buf + offset, &user_id,       sizeof(uint64_t)); offset += sizeof(uint64_t);
    memcpy(buf + offset, &timestamp,     sizeof(uint64_t)); offset += sizeof(uint64_t);
    if (properties_json)
        memcpy(buf + offset, properties_json, prop_len);

    *out_len = total_len;
    return buf;
}

// Decode a WAL entry back into event fields.
// out_properties is heap-allocated and owned by the caller (NULL if no properties).
// Returns FE_INVALID_ARG if data is too short or required out params are NULL.
int event_decode(const uint8_t* data, size_t len,
                 uint32_t* out_type_id, uint64_t* out_user_id,
                 uint64_t* out_timestamp, char** out_properties) {
    if (!data || len < HEADER_SIZE || !out_type_id || !out_user_id || !out_timestamp)
        return FE_INVALID_ARG;

    size_t offset = 0;
    memcpy(out_type_id,   data + offset, sizeof(uint32_t)); offset += sizeof(uint32_t);
    memcpy(out_user_id,   data + offset, sizeof(uint64_t)); offset += sizeof(uint64_t);
    memcpy(out_timestamp, data + offset, sizeof(uint64_t)); offset += sizeof(uint64_t);

    if (out_properties) {
        size_t prop_len = len - HEADER_SIZE;
        if (prop_len > 0) {
            *out_properties = malloc(prop_len);
            if (!*out_properties) return FE_OUT_OF_MEMORY;
            memcpy(*out_properties, data + offset, prop_len);
        } else {
            *out_properties = NULL;
        }
    }

    return FE_OK;
}
