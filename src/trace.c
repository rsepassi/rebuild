#include "trace.h"
#include "hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Magic bytes for trace file format
#define TRACE_MAGIC "RBTR"
#define TRACE_VERSION 1

// Allocate a new trace with the given request key
Trace* trace_create(const Hash* request_key) {
    if (request_key == NULL) {
        LOG_ERROR("trace_create: request_key is NULL");
        return NULL;
    }

    Trace* t = (Trace*)rebuild_calloc(1, sizeof(Trace));
    if (t == NULL) {
        LOG_ERROR("trace_create: failed to allocate Trace");
        return NULL;
    }

    // Copy the request key
    memcpy(&t->request_key, request_key, sizeof(Hash));

    // Initialize other fields (calloc already zeros them)
    t->dep_count = 0;
    t->dep_paths = NULL;
    t->dep_hashes = NULL;
    memset(&t->output_tree_hash, 0, sizeof(Hash));
    t->cpu_time_ms = 0;
    t->wall_time_ms = 0;

    return t;
}

// Free trace and all arrays
void trace_free(Trace* t) {
    if (t == NULL) {
        return;
    }

    // Free each dependency path
    if (t->dep_paths != NULL) {
        for (size_t i = 0; i < t->dep_count; i++) {
            if (t->dep_paths[i] != NULL) {
                rebuild_free(t->dep_paths[i]);
            }
        }
        rebuild_free(t->dep_paths);
    }

    // Free dependency hashes array
    if (t->dep_hashes != NULL) {
        rebuild_free(t->dep_hashes);
    }

    // Free the trace itself
    rebuild_free(t);
}

// Add a dependency to the trace
bool trace_add_dependency(Trace* t, const char* path, const Hash* hash) {
    if (t == NULL || path == NULL || hash == NULL) {
        LOG_ERROR("trace_add_dependency: invalid arguments");
        return false;
    }

    // Grow the arrays
    size_t new_count = t->dep_count + 1;

    char** new_paths = (char**)rebuild_realloc(
        t->dep_paths,
        new_count * sizeof(char*)
    );
    if (new_paths == NULL) {
        LOG_ERROR("trace_add_dependency: failed to reallocate dep_paths");
        return false;
    }
    t->dep_paths = new_paths;

    Hash* new_hashes = (Hash*)rebuild_realloc(
        t->dep_hashes,
        new_count * sizeof(Hash)
    );
    if (new_hashes == NULL) {
        LOG_ERROR("trace_add_dependency: failed to reallocate dep_hashes");
        return false;
    }
    t->dep_hashes = new_hashes;

    // Copy the path and hash
    t->dep_paths[t->dep_count] = rebuild_strdup(path);
    if (t->dep_paths[t->dep_count] == NULL) {
        LOG_ERROR("trace_add_dependency: failed to duplicate path");
        return false;
    }

    memcpy(&t->dep_hashes[t->dep_count], hash, sizeof(Hash));

    t->dep_count = new_count;
    return true;
}

// Check if all dependencies still match their recorded hashes
bool trace_validate(const Trace* t) {
    if (t == NULL) {
        LOG_ERROR("trace_validate: trace is NULL");
        return false;
    }

    // Check each dependency
    for (size_t i = 0; i < t->dep_count; i++) {
        const char* path = t->dep_paths[i];
        const Hash* expected_hash = &t->dep_hashes[i];

        // Check if dependency exists
        struct stat st;
        if (stat(path, &st) != 0) {
            LOG_DEBUG("trace_validate: dependency missing: %s", path);
            return false;
        }

        // Hash the dependency (file or directory tree)
        Hash actual_hash;
        bool hash_success;

        if (S_ISDIR(st.st_mode)) {
            // Directory: use hash_tree() for deterministic recursive hashing
            hash_success = hash_tree(path, &actual_hash);
            if (!hash_success) {
                LOG_WARN("trace_validate: failed to hash directory dependency: %s", path);
                return false;
            }
        } else if (S_ISREG(st.st_mode)) {
            // Regular file: use hash_file()
            hash_success = hash_file(path, &actual_hash);
            if (!hash_success) {
                LOG_WARN("trace_validate: failed to hash file dependency: %s", path);
                return false;
            }
        } else {
            LOG_WARN("trace_validate: dependency is neither file nor directory: %s", path);
            return false;
        }

        if (!hash_equal(&actual_hash, expected_hash)) {
            LOG_DEBUG("trace_validate: dependency changed: %s", path);
            return false;
        }
    }

    LOG_DEBUG("trace_validate: all %zu dependencies valid", t->dep_count);
    return true;
}

// Helper function to write data to file
static bool write_all(FILE* f, const void* data, size_t size) {
    size_t written = fwrite(data, 1, size, f);
    if (written != size) {
        LOG_ERROR("write_all: failed to write %zu bytes (wrote %zu)", size, written);
        return false;
    }
    return true;
}

// Save trace to disk in binary format
bool trace_save(const Trace* t, Storage* storage) {
    if (t == NULL || storage == NULL) {
        LOG_ERROR("trace_save: invalid arguments");
        return false;
    }

    // Get the trace file path
    char* trace_path = storage_get_trace_path(storage, &t->request_key);
    if (trace_path == NULL) {
        LOG_ERROR("trace_save: failed to get trace path");
        return false;
    }

    // Open file for writing
    FILE* f = fopen(trace_path, "wb");
    if (f == NULL) {
        LOG_ERROR("trace_save: failed to open file: %s", trace_path);
        rebuild_free(trace_path);
        return false;
    }

    bool success = true;

    // Write magic bytes
    if (!write_all(f, TRACE_MAGIC, 4)) {
        success = false;
        goto cleanup;
    }

    // Write version
    uint32_t version = TRACE_VERSION;
    if (!write_all(f, &version, sizeof(uint32_t))) {
        success = false;
        goto cleanup;
    }

    // Write request key
    if (!write_all(f, &t->request_key.bytes, 32)) {
        success = false;
        goto cleanup;
    }

    // Write dependency count
    uint64_t dep_count = t->dep_count;
    if (!write_all(f, &dep_count, sizeof(uint64_t))) {
        success = false;
        goto cleanup;
    }

    // Write each dependency
    for (size_t i = 0; i < t->dep_count; i++) {
        const char* path = t->dep_paths[i];
        uint32_t path_len = (uint32_t)strlen(path);

        // Write path length
        if (!write_all(f, &path_len, sizeof(uint32_t))) {
            success = false;
            goto cleanup;
        }

        // Write path
        if (!write_all(f, path, path_len)) {
            success = false;
            goto cleanup;
        }

        // Write hash
        if (!write_all(f, &t->dep_hashes[i].bytes, 32)) {
            success = false;
            goto cleanup;
        }
    }

    // Write output tree hash
    if (!write_all(f, &t->output_tree_hash.bytes, 32)) {
        success = false;
        goto cleanup;
    }

    // Write CPU time
    if (!write_all(f, &t->cpu_time_ms, sizeof(uint64_t))) {
        success = false;
        goto cleanup;
    }

    // Write wall time
    if (!write_all(f, &t->wall_time_ms, sizeof(uint64_t))) {
        success = false;
        goto cleanup;
    }

    LOG_INFO("trace_save: saved trace with %zu dependencies to %s", t->dep_count, trace_path);

cleanup:
    fclose(f);
    rebuild_free(trace_path);
    return success;
}

// Helper function to read data from file
static bool read_all(FILE* f, void* data, size_t size) {
    size_t read_count = fread(data, 1, size, f);
    if (read_count != size) {
        if (feof(f)) {
            LOG_ERROR("read_all: unexpected end of file");
        } else {
            LOG_ERROR("read_all: failed to read %zu bytes (read %zu)", size, read_count);
        }
        return false;
    }
    return true;
}

// Load trace from disk
Trace* trace_load(const Hash* request_key, Storage* storage) {
    if (request_key == NULL || storage == NULL) {
        LOG_ERROR("trace_load: invalid arguments");
        return NULL;
    }

    // Get the trace file path
    char* trace_path = storage_get_trace_path(storage, request_key);
    if (trace_path == NULL) {
        LOG_ERROR("trace_load: failed to get trace path");
        return NULL;
    }

    // Check if trace exists
    if (!storage_trace_exists(storage, request_key)) {
        LOG_DEBUG("trace_load: trace does not exist");
        rebuild_free(trace_path);
        return NULL;
    }

    // Open file for reading
    FILE* f = fopen(trace_path, "rb");
    if (f == NULL) {
        LOG_ERROR("trace_load: failed to open file: %s", trace_path);
        rebuild_free(trace_path);
        return NULL;
    }

    Trace* t = NULL;
    bool success = true;

    // Read and verify magic bytes
    char magic[4];
    if (!read_all(f, magic, 4) || memcmp(magic, TRACE_MAGIC, 4) != 0) {
        LOG_ERROR("trace_load: invalid magic bytes");
        success = false;
        goto cleanup;
    }

    // Read and verify version
    uint32_t version;
    if (!read_all(f, &version, sizeof(uint32_t))) {
        success = false;
        goto cleanup;
    }
    if (version != TRACE_VERSION) {
        LOG_ERROR("trace_load: unsupported version %u", version);
        success = false;
        goto cleanup;
    }

    // Create trace
    t = trace_create(request_key);
    if (t == NULL) {
        success = false;
        goto cleanup;
    }

    // Read request key (verify it matches)
    Hash stored_key;
    if (!read_all(f, &stored_key.bytes, 32)) {
        success = false;
        goto cleanup;
    }
    if (!hash_equal(&stored_key, request_key)) {
        LOG_ERROR("trace_load: request key mismatch");
        success = false;
        goto cleanup;
    }

    // Read dependency count
    uint64_t dep_count;
    if (!read_all(f, &dep_count, sizeof(uint64_t))) {
        success = false;
        goto cleanup;
    }

    // Read each dependency
    for (uint64_t i = 0; i < dep_count; i++) {
        // Read path length
        uint32_t path_len;
        if (!read_all(f, &path_len, sizeof(uint32_t))) {
            success = false;
            goto cleanup;
        }

        // Sanity check path length (prevent huge allocations)
        if (path_len > 4096) {
            LOG_ERROR("trace_load: path length too large: %u", path_len);
            success = false;
            goto cleanup;
        }

        // Read path
        char* path = (char*)rebuild_malloc(path_len + 1);
        if (path == NULL) {
            success = false;
            goto cleanup;
        }
        if (!read_all(f, path, path_len)) {
            rebuild_free(path);
            success = false;
            goto cleanup;
        }
        path[path_len] = '\0';

        // Read hash
        Hash hash;
        if (!read_all(f, &hash.bytes, 32)) {
            rebuild_free(path);
            success = false;
            goto cleanup;
        }

        // Add dependency
        if (!trace_add_dependency(t, path, &hash)) {
            rebuild_free(path);
            success = false;
            goto cleanup;
        }

        rebuild_free(path);
    }

    // Read output tree hash
    if (!read_all(f, &t->output_tree_hash.bytes, 32)) {
        success = false;
        goto cleanup;
    }

    // Read CPU time
    if (!read_all(f, &t->cpu_time_ms, sizeof(uint64_t))) {
        success = false;
        goto cleanup;
    }

    // Read wall time
    if (!read_all(f, &t->wall_time_ms, sizeof(uint64_t))) {
        success = false;
        goto cleanup;
    }

    LOG_INFO("trace_load: loaded trace with %zu dependencies from %s", t->dep_count, trace_path);

cleanup:
    fclose(f);
    rebuild_free(trace_path);

    if (!success && t != NULL) {
        trace_free(t);
        return NULL;
    }

    return t;
}
