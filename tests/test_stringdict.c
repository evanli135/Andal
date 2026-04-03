#include "../src/internal.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_create_destroy() {
    StringDict* dict = string_dict_create(16);
    assert(dict != NULL);
    assert(dict->count == 0);
    assert(dict->capacity >= 16);
    string_dict_destroy(dict);
    printf("✓ test_create_destroy\n");
}

void test_add_single() {
    StringDict* dict = string_dict_create(16);

    uint32_t id;
    int err = string_dict_get_or_add(dict, "page_view", &id);
    assert(err == FE_OK);
    assert(id == 0);  // First entry gets ID 0
    assert(dict->count == 1);

    string_dict_destroy(dict);
    printf("✓ test_add_single\n");
}

void test_add_multiple() {
    StringDict* dict = string_dict_create(16);

    uint32_t id1, id2, id3;

    string_dict_get_or_add(dict, "page_view", &id1);
    string_dict_get_or_add(dict, "click", &id2);
    string_dict_get_or_add(dict, "purchase", &id3);

    assert(id1 == 0);
    assert(id2 == 1);
    assert(id3 == 2);
    assert(dict->count == 3);

    string_dict_destroy(dict);
    printf("✓ test_add_multiple\n");
}

void test_duplicate_strings() {
    StringDict* dict = string_dict_create(16);

    uint32_t id1, id2;

    // Add "page_view" twice
    string_dict_get_or_add(dict, "page_view", &id1);
    string_dict_get_or_add(dict, "page_view", &id2);

    // Should get same ID both times
    assert(id1 == id2);
    assert(id1 == 0);
    assert(dict->count == 1);  // Only one entry

    string_dict_destroy(dict);
    printf("✓ test_duplicate_strings\n");
}

void test_resize() {
    StringDict* dict = string_dict_create(4);  // Start small

    // Add enough entries to trigger resize (4 * 0.7 = 2.8, so 3 entries will resize)
    uint32_t ids[10];
    const char* events[] = {
        "page_view", "click", "purchase", "signup",
        "login", "logout", "error", "warning"
    };

    for (int i = 0; i < 8; i++) {
        int err = string_dict_get_or_add(dict, events[i], &ids[i]);
        assert(err == FE_OK);
        assert(ids[i] == (uint32_t)i);
    }

    assert(dict->count == 8);
    assert(dict->capacity > 4);  // Should have resized

    // Verify all entries still accessible
    for (int i = 0; i < 8; i++) {
        uint32_t id;
        string_dict_get_or_add(dict, events[i], &id);
        assert(id == ids[i]);  // Same ID as before
    }

    string_dict_destroy(dict);
    printf("✓ test_resize\n");
}

void test_collision_handling() {
    StringDict* dict = string_dict_create(16);

    // Add many strings to ensure some collisions
    uint32_t ids[50];
    char buf[32];

    for (int i = 0; i < 50; i++) {
        snprintf(buf, sizeof(buf), "event_%d", i);
        string_dict_get_or_add(dict, buf, &ids[i]);
        assert(ids[i] == (uint32_t)i);
    }

    assert(dict->count == 50);

    // Verify all are retrievable
    for (int i = 0; i < 50; i++) {
        uint32_t id;
        snprintf(buf, sizeof(buf), "event_%d", i);
        string_dict_get_or_add(dict, buf, &id);
        assert(id == ids[i]);
    }

    string_dict_destroy(dict);
    printf("✓ test_collision_handling\n");
}

void test_stats() {
    StringDict* dict = string_dict_create(16);

    uint32_t id;
    string_dict_get_or_add(dict, "page_view", &id);
    string_dict_get_or_add(dict, "click", &id);
    string_dict_get_or_add(dict, "purchase", &id);

    printf("\n");
    string_dict_stats(dict);
    printf("\n");

    string_dict_destroy(dict);
    printf("✓ test_stats\n");
}

int main() {
    printf("Running StringDict tests...\n\n");

    test_create_destroy();
    test_add_single();
    test_add_multiple();
    test_duplicate_strings();
    test_resize();
    test_collision_handling();
    test_stats();

    printf("\n✅ All StringDict tests passed!\n");
    return 0;
}
