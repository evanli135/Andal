# Known Issues / Design Warnings

## segment_create / segment_write_to_disk are not atomic

`event_store_flush` calls `segment_create` (which adds the segment to `segments[]` and `partition_idx`) and then calls `segment_write_to_disk` separately. If the process crashes between those two calls, the segment exists in the in-memory index but has no corresponding `.dat` file on disk. On the next open, WAL recovery would restore the events to `active_block` correctly, but the orphaned segment entry would be gone (since `segments[]` is rebuilt in memory, not persisted). So data isn't actually lost — but the gap means a partially-initialized segment could theoretically be observed during a query in a multi-threaded context.

**Fix**: write to disk inside `segment_create` and only return a valid `Segment*` if the write succeeded. Alternatively, don't register the segment in `segments[]` / `partition_idx` until after `segment_write_to_disk` returns `FE_OK`.
