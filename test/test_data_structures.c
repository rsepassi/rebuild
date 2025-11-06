#include "../src/common.h"
#include "../src/buffer.h"
#include "../src/map.h"
#include "../src/set.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

// Test buffer operations
void test_buffer() {
    printf("Testing Buffer...\n");

    // Test creation
    Buffer* buf = buffer_create(0);
    assert(buf != NULL);
    assert(buffer_size(buf) == 0);
    assert(buffer_capacity(buf) >= 64);

    // Test append string
    RebuildError err = buffer_append_str(buf, "Hello");
    assert(err == REBUILD_OK);
    assert(buffer_size(buf) == 5);

    err = buffer_append_str(buf, " World");
    assert(err == REBUILD_OK);
    assert(buffer_size(buf) == 11);

    // Test append char
    err = buffer_append_char(buf, '!');
    assert(err == REBUILD_OK);
    assert(buffer_size(buf) == 12);

    // Test to_string
    char* str = buffer_to_string(buf);
    assert(str != NULL);
    assert(strcmp(str, "Hello World!") == 0);
    rebuild_free(str);

    // Test append raw data
    const char data[] = {'\0', 'A', 'B', 'C'};
    err = buffer_append(buf, data, sizeof(data));
    assert(err == REBUILD_OK);
    assert(buffer_size(buf) == 16);

    // Test clear
    buffer_clear(buf);
    assert(buffer_size(buf) == 0);
    assert(buffer_capacity(buf) > 0); // Capacity should be preserved

    // Test large append (triggers reallocation)
    for (int i = 0; i < 1000; i++) {
        err = buffer_append_str(buf, "X");
        assert(err == REBUILD_OK);
    }
    assert(buffer_size(buf) == 1000);

    buffer_free(buf);
    printf("  ✓ Buffer tests passed\n");
}

// Test map operations
void test_map() {
    printf("Testing Map...\n");

    // Test creation
    Map* map = map_create(0);
    assert(map != NULL);
    assert(map_size(map) == 0);

    // Test set and get
    RebuildError err = map_set(map, "key1", (void*)100);
    assert(err == REBUILD_OK);
    assert(map_size(map) == 1);

    void* val = map_get(map, "key1");
    assert(val == (void*)100);

    // Test has
    assert(map_has(map, "key1") == true);
    assert(map_has(map, "key2") == false);

    // Test update existing key
    err = map_set(map, "key1", (void*)200);
    assert(err == REBUILD_OK);
    assert(map_size(map) == 1);
    val = map_get(map, "key1");
    assert(val == (void*)200);

    // Test multiple keys
    err = map_set(map, "key2", (void*)300);
    assert(err == REBUILD_OK);
    err = map_set(map, "key3", (void*)400);
    assert(err == REBUILD_OK);
    assert(map_size(map) == 3);

    // Test remove
    void* removed = map_remove(map, "key2");
    assert(removed == (void*)300);
    assert(map_size(map) == 2);
    assert(map_has(map, "key2") == false);

    // Test remove non-existent
    removed = map_remove(map, "nonexistent");
    assert(removed == NULL);
    assert(map_size(map) == 2);

    // Test iteration
    int count = 0;
    map_iterate(map, NULL, &count); // Should handle NULL function

    // Test with many entries (triggers resize)
    Map* big_map = map_create(4);
    for (int i = 0; i < 100; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i);
        err = map_set(big_map, key, (void*)(intptr_t)i);
        assert(err == REBUILD_OK);
    }
    assert(map_size(big_map) == 100);

    // Verify all keys
    for (int i = 0; i < 100; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i);
        assert(map_has(big_map, key) == true);
        val = map_get(big_map, key);
        assert(val == (void*)(intptr_t)i);
    }

    // Test clear
    map_clear(big_map, NULL);
    assert(map_size(big_map) == 0);

    map_free(map, NULL);
    map_free(big_map, NULL);
    printf("  ✓ Map tests passed\n");
}

// Test set operations
void test_set() {
    printf("Testing Set...\n");

    // Test creation
    Set* set = set_create(0);
    assert(set != NULL);
    assert(set_size(set) == 0);

    // Test add
    RebuildError err = set_add(set, "apple");
    assert(err == REBUILD_OK);
    assert(set_size(set) == 1);

    // Test has
    assert(set_has(set, "apple") == true);
    assert(set_has(set, "banana") == false);

    // Test add duplicate
    err = set_add(set, "apple");
    assert(err == REBUILD_OK);
    assert(set_size(set) == 1); // Size should not increase

    // Test multiple adds
    err = set_add(set, "banana");
    assert(err == REBUILD_OK);
    err = set_add(set, "cherry");
    assert(err == REBUILD_OK);
    assert(set_size(set) == 3);

    // Test remove
    bool removed = set_remove(set, "banana");
    assert(removed == true);
    assert(set_size(set) == 2);
    assert(set_has(set, "banana") == false);

    // Test remove non-existent
    removed = set_remove(set, "nonexistent");
    assert(removed == false);
    assert(set_size(set) == 2);

    // Test with many entries (triggers resize)
    Set* big_set = set_create(4);
    for (int i = 0; i < 100; i++) {
        char value[32];
        snprintf(value, sizeof(value), "value_%d", i);
        err = set_add(big_set, value);
        assert(err == REBUILD_OK);
    }
    assert(set_size(big_set) == 100);

    // Verify all values
    for (int i = 0; i < 100; i++) {
        char value[32];
        snprintf(value, sizeof(value), "value_%d", i);
        assert(set_has(big_set, value) == true);
    }

    // Test copy
    Set* copy = set_copy(big_set);
    assert(copy != NULL);
    assert(set_size(copy) == 100);
    for (int i = 0; i < 100; i++) {
        char value[32];
        snprintf(value, sizeof(value), "value_%d", i);
        assert(set_has(copy, value) == true);
    }

    // Test union
    Set* set1 = set_create(0);
    set_add(set1, "a");
    set_add(set1, "b");

    Set* set2 = set_create(0);
    set_add(set2, "b");
    set_add(set2, "c");

    err = set_union(set1, set2);
    assert(err == REBUILD_OK);
    assert(set_size(set1) == 3);
    assert(set_has(set1, "a") == true);
    assert(set_has(set1, "b") == true);
    assert(set_has(set1, "c") == true);

    // Test contains_all
    assert(set_contains_all(set1, set2) == true);
    assert(set_contains_all(set2, set1) == false);

    // Test clear
    set_clear(big_set);
    assert(set_size(big_set) == 0);

    set_free(set);
    set_free(big_set);
    set_free(copy);
    set_free(set1);
    set_free(set2);
    printf("  ✓ Set tests passed\n");
}

// Callback for testing iteration
static bool count_callback(const char* value, void* user_data) {
    int* count = (int*)user_data;
    (*count)++;
    return true; // Continue iteration
}

static bool early_stop_callback(const char* value, void* user_data) {
    int* count = (int*)user_data;
    (*count)++;
    return *count < 5; // Stop after 5 items
}

void test_set_iteration() {
    printf("Testing Set iteration...\n");

    Set* set = set_create(0);
    for (int i = 0; i < 10; i++) {
        char value[32];
        snprintf(value, sizeof(value), "item_%d", i);
        set_add(set, value);
    }

    // Test full iteration
    int count = 0;
    set_iterate(set, count_callback, &count);
    assert(count == 10);

    // Test early stop
    count = 0;
    set_iterate(set, early_stop_callback, &count);
    assert(count == 5);

    set_free(set);
    printf("  ✓ Set iteration tests passed\n");
}

// Callback for testing map iteration
static bool map_count_callback(const char* key, void* value, void* user_data) {
    int* count = (int*)user_data;
    (*count)++;
    return true;
}

void test_map_iteration() {
    printf("Testing Map iteration...\n");

    Map* map = map_create(0);
    for (int i = 0; i < 10; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i);
        map_set(map, key, (void*)(intptr_t)i);
    }

    // Test iteration
    int count = 0;
    map_iterate(map, map_count_callback, &count);
    assert(count == 10);

    map_free(map, NULL);
    printf("  ✓ Map iteration tests passed\n");
}

int main() {
    printf("Running data structure tests...\n\n");

    test_buffer();
    test_map();
    test_set();
    test_set_iteration();
    test_map_iteration();

    printf("\n✓ All tests passed!\n");
    return 0;
}
