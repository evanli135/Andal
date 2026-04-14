# Implementation Notes for append_to_block()

## Function Signature
```c
int append_to_block(
    EventBlock* block,
    uint32_t event_type_id,
    uint64_t user_id,
    uint64_t timestamp,
    const char* properties_json
);
```

## Implementation Checklist

### Step 1: Validate Input
- [ ] Check if `block` is NULL → return `FE_INVALID_ARG`

### Step 2: Check Capacity
- [ ] Check if `block->count >= block->capacity`
- [ ] If full → return `FE_CAPACITY_EXCEEDED` (don't auto-resize)

### Step 3: Get Current Index
- [ ] Set `size_t idx = block->count`

### Step 4: Write to Columnar Arrays
- [ ] `block->event_type_ids[idx] = event_type_id`
- [ ] `block->user_ids[idx] = user_id`
- [ ] `block->timestamps[idx] = timestamp`

### Step 5: Handle Properties String
- [ ] If `properties_json` is NULL or empty string:
  - Set `block->properties[idx] = NULL`
- [ ] Else:
  - Use `strdup(properties_json)` to make a copy
  - Check if `strdup` failed (returns NULL) → return `FE_OUT_OF_MEMORY`
  - Set `block->properties[idx] = <the copy>`

### Step 6: Update Metadata
- [ ] If `timestamp < block->min_timestamp`:
  - `block->min_timestamp = timestamp`
- [ ] If `timestamp > block->max_timestamp`:
  - `block->max_timestamp = timestamp`

### Step 7: Increment Count
- [ ] `block->count++`

### Step 8: Return Success
- [ ] `return FE_OK`

## Edge Cases to Consider

1. **Empty properties**: `properties_json` can be NULL or ""
   - Don't allocate memory, just store NULL

2. **strdup failure**: Check return value
   - If NULL, return `FE_OUT_OF_MEMORY` immediately
   - Don't increment count on failure

3. **First event**: `min_timestamp` starts at `UINT64_MAX`
   - First event will always be less than this
   - Sets both min and max on first append

4. **Full block**: Don't automatically resize
   - Return error, let caller decide what to do
   - This gives caller control over memory growth

## Memory Ownership

- **Input**: `properties_json` is borrowed (caller owns it)
- **Storage**: Block makes its own copy with `strdup()`
- **Cleanup**: Block frees the copy in `destroy_event_block()`

## Example Usage

```c
EventBlock* block = create_event_block(10);

// Success case
int err = append_to_block(block, 0, 123, 1000, "{\"page\":\"/home\"}");
assert(err == FE_OK);
assert(block->count == 1);

// Capacity exceeded case
for (int i = 0; i < 10; i++) {
    append_to_block(block, 0, i, 1000 + i, NULL);
}
// Block is now full (count == capacity)

err = append_to_block(block, 0, 999, 2000, NULL);
assert(err == FE_CAPACITY_EXCEEDED);
assert(block->count == 10);  // Count didn't increase
```

## Test Cases to Write

1. Successful append
2. Multiple appends
3. Append with NULL properties
4. Append with empty string properties
5. Append with valid JSON properties
6. Capacity exceeded error
7. NULL block error
8. Metadata updates (min/max timestamp)
9. Properties memory is copied (not shared)
