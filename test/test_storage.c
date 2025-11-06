#include "../src/storage.h"
#include "../src/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

void test_storage_init(void) {
    printf("Testing storage_init...\n");

    Storage* s = storage_init();
    assert(s != NULL);
    assert(s->base_dir != NULL);
    assert(s->traces_dir != NULL);
    assert(s->objects_dir != NULL);
    assert(s->tmp_dir != NULL);

    printf("  Base directory: %s\n", s->base_dir);
    printf("  Traces directory: %s\n", s->traces_dir);
    printf("  Objects directory: %s\n", s->objects_dir);
    printf("  Tmp directory: %s\n", s->tmp_dir);

    storage_free(s);
    printf("  PASS\n\n");
}

void test_storage_paths(void) {
    printf("Testing storage path generation...\n");

    Storage* s = storage_init();
    assert(s != NULL);

    // Create a test hash
    Hash test_hash;
    const char* test_data = "test data for hashing";
    hash_data(test_data, strlen(test_data), &test_hash);

    // Get trace path
    char* trace_path = storage_get_trace_path(s, &test_hash);
    assert(trace_path != NULL);
    printf("  Trace path: %s\n", trace_path);

    // Verify the path format (should be like traces/ab/cdef...)
    assert(strstr(trace_path, "/traces/") != NULL);

    // Get object path
    char* object_path = storage_get_object_path(s, &test_hash);
    assert(object_path != NULL);
    printf("  Object path: %s\n", object_path);

    // Verify the path format (should be like objects/ab/cdef...)
    assert(strstr(object_path, "/objects/") != NULL);

    // Get tmp directory
    char* tmp_dir = storage_get_tmp_dir(s, "test_target");
    assert(tmp_dir != NULL);
    printf("  Tmp directory: %s\n", tmp_dir);

    // Verify the path format (should be like tmp/test_target_...)
    assert(strstr(tmp_dir, "/tmp/test_target_") != NULL);

    rebuild_free(trace_path);
    rebuild_free(object_path);
    rebuild_free(tmp_dir);
    storage_free(s);
    printf("  PASS\n\n");
}

void test_storage_exists(void) {
    printf("Testing storage_exists functions...\n");

    Storage* s = storage_init();
    assert(s != NULL);

    // Create a test hash
    Hash test_hash;
    const char* test_data = "nonexistent test data";
    hash_data(test_data, strlen(test_data), &test_hash);

    // These should not exist
    assert(!storage_trace_exists(s, &test_hash));
    assert(!storage_object_exists(s, &test_hash));

    printf("  Non-existent trace/object correctly reported as not existing\n");

    // Create a trace file
    char* trace_path = storage_get_trace_path(s, &test_hash);
    assert(trace_path != NULL);

    FILE* f = fopen(trace_path, "w");
    assert(f != NULL);
    fprintf(f, "test trace data\n");
    fclose(f);

    // Now it should exist
    assert(storage_trace_exists(s, &test_hash));
    printf("  Created trace correctly detected as existing\n");

    // Clean up
    remove(trace_path);
    rebuild_free(trace_path);
    storage_free(s);
    printf("  PASS\n\n");
}

void test_sharding(void) {
    printf("Testing 2-level sharding...\n");

    Storage* s = storage_init();
    assert(s != NULL);

    // Create multiple hashes and verify they get sharded correctly
    for (int i = 0; i < 5; i++) {
        char test_data[64];
        snprintf(test_data, sizeof(test_data), "test data %d", i);

        Hash hash;
        hash_data(test_data, strlen(test_data), &hash);

        char* trace_path = storage_get_trace_path(s, &hash);
        assert(trace_path != NULL);

        // Verify the path has the 2-level structure: base/XX/YYYYYY...
        // Count the slashes after traces/
        const char* after_traces = strstr(trace_path, "/traces/");
        assert(after_traces != NULL);
        after_traces += strlen("/traces/");

        // Should have exactly one more slash (the shard directory)
        const char* slash = strchr(after_traces, '/');
        assert(slash != NULL);
        assert(slash - after_traces == 2);  // First level is 2 characters

        printf("  Hash %d -> %s\n", i, trace_path);
        rebuild_free(trace_path);
    }

    storage_free(s);
    printf("  PASS\n\n");
}

int main(void) {
    printf("=== Storage System Tests ===\n\n");

    test_storage_init();
    test_storage_paths();
    test_storage_exists();
    test_sharding();

    printf("=== All tests passed! ===\n");
    return 0;
}
