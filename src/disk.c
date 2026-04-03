#define SEG_MAGIC "EVTSEG\0\0"
#define SEG_VERSION 1

#include "stdint.h"
#include "internal.h"


// The on-disk file header
typedef struct {
    uint8_t magic[8];      // "EVTSEG\0\0"
    uint32_t version;

    uint64_t event_count;
    uint64_t min_timestamp;
    uint64_t max_timestamp;

    // Byte offsets (quick column index)
    uint64_t offset_type_ids;
    uint64_t offset_user_ids;
    uint64_t offset_timestamps;
    uint64_t offset_properties_index;
    uint64_t offset_properties_heap;

    uint64_t properties_heap_size;
} SegmentHeader;

int segment_write_to_disk(Segment* seg, const char* dir) {
    if (!seg || !dir) return FE_INVALID_ARG;
    if (!seg->block) return FE_INVALID_ARG;

    EventBlock* bl = seg->block;

    // Build properties heap
    size_t heap_size = 0;
    uint32_t* prop_offsets = malloc(bl->count * sizeof(uint32_t));
    

}   