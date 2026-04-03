/**
 * Basic FastEvents Usage Example
 *
 * Shows the public API for users.
 * Only includes fastevents.h - no internal headers needed.
 */

#include "../src/fastevents.h"
#include <stdio.h>
#include <time.h>

int main() {
    printf("FastEvents - Basic Usage Example\n\n");

    // Open or create event store
    printf("Opening event store...\n");
    EventStore* db = event_store_open("./example_data");
    if (!db) {
        fprintf(stderr, "Failed to open event store\n");
        return 1;
    }

    // Get current timestamp
    uint64_t now = (uint64_t)time(NULL) * 1000;

    // Track some events
    printf("Tracking events...\n");

    event_store_append(db, "page_view", 123, now, "{\"page\":\"/home\"}");
    event_store_append(db, "page_view", 123, now + 1000, "{\"page\":\"/pricing\"}");
    event_store_append(db, "click", 123, now + 2000, "{\"button\":\"signup\"}");
    event_store_append(db, "page_view", 456, now + 3000, "{\"page\":\"/home\"}");
    event_store_append(db, "purchase", 123, now + 4000, "{\"amount\":29.99}");

    printf("Tracked 5 events\n\n");

    // Query events
    printf("Querying events...\n");

    QueryFilter filter = {
        .event_type = "page_view",
        .user_id = 0,
        .filter_user = false,
        .start_time = 0,
        .end_time = 0
    };

    QueryResult* results = event_store_filter(db, &filter);
    if (results) {
        printf("Found %zu page_view events\n", results->count);
        query_result_destroy(results);
    }

    // Get stats
    printf("\nStore statistics:\n");
    event_store_stats(db);

    printf("\nTotal events: %zu\n", event_store_size(db));

    // Force flush to disk
    printf("\nFlushing to disk...\n");
    event_store_flush(db);

    // Close and cleanup
    printf("Closing event store...\n");
    event_store_close(db);

    printf("\n✅ Done!\n");
    return 0;
}
