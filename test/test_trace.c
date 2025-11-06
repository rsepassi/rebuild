#include "../src/trace.h"
#include "../src/storage.h"
#include "../src/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

void test_trace_create_free(void) {
    printf("Testing trace_create and trace_free...\n");

    Hash request_key;
    hash_data("test_request", 12, &request_key);

    Trace* t = trace_create(&request_key);
    assert(t != NULL);
    assert(hash_equal(&t->request_key, &request_key));
    assert(t->dep_count == 0);
    assert(t->dep_paths == NULL);
    assert(t->dep_hashes == NULL);
    assert(t->cpu_time_ms == 0);
    assert(t->wall_time_ms == 0);

    trace_free(t);
    printf("  PASS\n\n");
}

void test_trace_add_dependency(void) {
    printf("Testing trace_add_dependency...\n");

    Hash request_key;
    hash_data("test_request", 12, &request_key);

    Trace* t = trace_create(&request_key);
    assert(t != NULL);

    // Add first dependency
    Hash dep1_hash;
    hash_data("dependency 1", 12, &dep1_hash);
    bool success = trace_add_dependency(t, "/path/to/dep1.c", &dep1_hash);
    assert(success);
    assert(t->dep_count == 1);
    assert(strcmp(t->dep_paths[0], "/path/to/dep1.c") == 0);
    assert(hash_equal(&t->dep_hashes[0], &dep1_hash));

    // Add second dependency
    Hash dep2_hash;
    hash_data("dependency 2", 12, &dep2_hash);
    success = trace_add_dependency(t, "/path/to/dep2.h", &dep2_hash);
    assert(success);
    assert(t->dep_count == 2);
    assert(strcmp(t->dep_paths[1], "/path/to/dep2.h") == 0);
    assert(hash_equal(&t->dep_hashes[1], &dep2_hash));

    // Verify first dependency is still intact
    assert(strcmp(t->dep_paths[0], "/path/to/dep1.c") == 0);
    assert(hash_equal(&t->dep_hashes[0], &dep1_hash));

    trace_free(t);
    printf("  PASS\n\n");
}

void test_trace_validate(void) {
    printf("Testing trace_validate...\n");

    Hash request_key;
    hash_data("test_request", 12, &request_key);

    Trace* t = trace_create(&request_key);
    assert(t != NULL);

    // Create a test file
    const char* test_file = "/tmp/rebuild_test_dep.txt";
    FILE* f = fopen(test_file, "w");
    assert(f != NULL);
    fprintf(f, "test content\n");
    fclose(f);

    // Hash the file
    Hash file_hash;
    bool success = hash_file(test_file, &file_hash);
    assert(success);

    // Add as dependency
    success = trace_add_dependency(t, test_file, &file_hash);
    assert(success);

    // Validate should succeed
    assert(trace_validate(t));
    printf("  Valid trace correctly validated\n");

    // Modify the file
    f = fopen(test_file, "w");
    assert(f != NULL);
    fprintf(f, "modified content\n");
    fclose(f);

    // Validate should fail now
    assert(!trace_validate(t));
    printf("  Modified dependency correctly invalidated trace\n");

    // Clean up
    remove(test_file);
    trace_free(t);
    printf("  PASS\n\n");
}

void test_trace_save_load(void) {
    printf("Testing trace_save and trace_load...\n");

    Storage* storage = storage_init();
    assert(storage != NULL);

    Hash request_key;
    hash_data("test_request_save_load", 22, &request_key);

    // Create a trace
    Trace* t1 = trace_create(&request_key);
    assert(t1 != NULL);

    // Add dependencies
    Hash dep1_hash;
    hash_data("dependency 1", 12, &dep1_hash);
    trace_add_dependency(t1, "/path/to/source.c", &dep1_hash);

    Hash dep2_hash;
    hash_data("dependency 2", 12, &dep2_hash);
    trace_add_dependency(t1, "/path/to/header.h", &dep2_hash);

    // Set output hash and timing
    hash_data("output tree", 11, &t1->output_tree_hash);
    t1->cpu_time_ms = 1234;
    t1->wall_time_ms = 5678;

    // Save the trace
    bool success = trace_save(t1, storage);
    assert(success);
    printf("  Trace saved successfully\n");

    // Load the trace
    Trace* t2 = trace_load(&request_key, storage);
    assert(t2 != NULL);
    printf("  Trace loaded successfully\n");

    // Verify the loaded trace matches
    assert(hash_equal(&t2->request_key, &request_key));
    assert(t2->dep_count == 2);
    assert(strcmp(t2->dep_paths[0], "/path/to/source.c") == 0);
    assert(hash_equal(&t2->dep_hashes[0], &dep1_hash));
    assert(strcmp(t2->dep_paths[1], "/path/to/header.h") == 0);
    assert(hash_equal(&t2->dep_hashes[1], &dep2_hash));
    assert(hash_equal(&t2->output_tree_hash, &t1->output_tree_hash));
    assert(t2->cpu_time_ms == 1234);
    assert(t2->wall_time_ms == 5678);
    printf("  Loaded trace matches original\n");

    // Clean up - remove the trace file
    char* trace_path = storage_get_trace_path(storage, &request_key);
    remove(trace_path);
    rebuild_free(trace_path);

    trace_free(t1);
    trace_free(t2);
    storage_free(storage);
    printf("  PASS\n\n");
}

void test_trace_load_nonexistent(void) {
    printf("Testing trace_load with nonexistent trace...\n");

    Storage* storage = storage_init();
    assert(storage != NULL);

    Hash request_key;
    hash_data("nonexistent_trace", 17, &request_key);

    // Try to load a trace that doesn't exist
    Trace* t = trace_load(&request_key, storage);
    assert(t == NULL);
    printf("  Nonexistent trace correctly returns NULL\n");

    storage_free(storage);
    printf("  PASS\n\n");
}

void test_trace_binary_format(void) {
    printf("Testing trace binary format...\n");

    Storage* storage = storage_init();
    assert(storage != NULL);

    Hash request_key;
    hash_data("test_format", 11, &request_key);

    // Create and save a trace
    Trace* t1 = trace_create(&request_key);
    Hash dep_hash;
    hash_data("dep", 3, &dep_hash);
    trace_add_dependency(t1, "/test/path.txt", &dep_hash);
    hash_data("output", 6, &t1->output_tree_hash);
    t1->cpu_time_ms = 999;
    t1->wall_time_ms = 888;

    trace_save(t1, storage);

    // Read the file directly to verify format
    char* trace_path = storage_get_trace_path(storage, &request_key);
    FILE* f = fopen(trace_path, "rb");
    assert(f != NULL);

    // Check magic bytes
    char magic[4];
    fread(magic, 1, 4, f);
    assert(memcmp(magic, "RBTR", 4) == 0);
    printf("  Magic bytes correct: RBTR\n");

    // Check version
    uint32_t version;
    fread(&version, sizeof(uint32_t), 1, f);
    assert(version == 1);
    printf("  Version correct: %u\n", version);

    fclose(f);
    remove(trace_path);
    rebuild_free(trace_path);

    trace_free(t1);
    storage_free(storage);
    printf("  PASS\n\n");
}

void test_trace_empty(void) {
    printf("Testing trace with no dependencies...\n");

    Storage* storage = storage_init();
    assert(storage != NULL);

    Hash request_key;
    hash_data("empty_trace", 11, &request_key);

    // Create a trace with no dependencies
    Trace* t1 = trace_create(&request_key);
    hash_data("output", 6, &t1->output_tree_hash);
    t1->cpu_time_ms = 100;
    t1->wall_time_ms = 200;

    // Save and load
    bool success = trace_save(t1, storage);
    assert(success);

    Trace* t2 = trace_load(&request_key, storage);
    assert(t2 != NULL);
    assert(t2->dep_count == 0);
    assert(t2->dep_paths == NULL);
    assert(t2->dep_hashes == NULL);
    assert(hash_equal(&t2->output_tree_hash, &t1->output_tree_hash));
    assert(t2->cpu_time_ms == 100);
    assert(t2->wall_time_ms == 200);
    printf("  Empty trace saved and loaded correctly\n");

    // Validate should succeed for empty trace
    assert(trace_validate(t2));
    printf("  Empty trace validates successfully\n");

    // Clean up
    char* trace_path = storage_get_trace_path(storage, &request_key);
    remove(trace_path);
    rebuild_free(trace_path);

    trace_free(t1);
    trace_free(t2);
    storage_free(storage);
    printf("  PASS\n\n");
}

void test_trace_large_dependency_set(void) {
    printf("Testing trace with many dependencies...\n");

    Storage* storage = storage_init();
    assert(storage != NULL);

    Hash request_key;
    hash_data("large_trace", 11, &request_key);

    Trace* t1 = trace_create(&request_key);
    assert(t1 != NULL);

    // Add many dependencies
    const size_t num_deps = 100;
    for (size_t i = 0; i < num_deps; i++) {
        char path[256];
        char data[64];
        snprintf(path, sizeof(path), "/path/to/file%zu.txt", i);
        snprintf(data, sizeof(data), "content %zu", i);

        Hash dep_hash;
        hash_data(data, strlen(data), &dep_hash);
        trace_add_dependency(t1, path, &dep_hash);
    }

    assert(t1->dep_count == num_deps);
    printf("  Added %zu dependencies\n", num_deps);

    // Save and load
    trace_save(t1, storage);
    Trace* t2 = trace_load(&request_key, storage);
    assert(t2 != NULL);
    assert(t2->dep_count == num_deps);

    // Verify all dependencies
    for (size_t i = 0; i < num_deps; i++) {
        char expected_path[256];
        snprintf(expected_path, sizeof(expected_path), "/path/to/file%zu.txt", i);
        assert(strcmp(t2->dep_paths[i], expected_path) == 0);
    }
    printf("  All %zu dependencies loaded correctly\n", num_deps);

    // Clean up
    char* trace_path = storage_get_trace_path(storage, &request_key);
    remove(trace_path);
    rebuild_free(trace_path);

    trace_free(t1);
    trace_free(t2);
    storage_free(storage);
    printf("  PASS\n\n");
}

int main(void) {
    printf("=== Trace System Tests ===\n\n");

    test_trace_create_free();
    test_trace_add_dependency();
    test_trace_validate();
    test_trace_save_load();
    test_trace_load_nonexistent();
    test_trace_binary_format();
    test_trace_empty();
    test_trace_large_dependency_set();

    printf("=== All tests passed! ===\n");
    return 0;
}
