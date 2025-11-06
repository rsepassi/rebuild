#ifndef REBUILD_TRACE_H
#define REBUILD_TRACE_H

#include "common.h"
#include "storage.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Trace represents a constructive cache entry for a build target
// It records dependencies, their hashes, and the output tree hash
typedef struct Trace {
    Hash request_key;          // Cache key for this trace
    size_t dep_count;          // Number of dependencies
    char** dep_paths;          // Dependency file paths
    Hash* dep_hashes;          // Content hashes of dependencies
    Hash output_tree_hash;     // Hash of output directory tree
    uint64_t cpu_time_ms;      // CPU time taken
    uint64_t wall_time_ms;     // Wall clock time taken
} Trace;

// Allocate a new trace with the given request key
// Returns NULL on allocation failure
Trace* trace_create(const Hash* request_key);

// Free trace and all arrays
void trace_free(Trace* t);

// Add a dependency to the trace
// Creates a copy of the path and hash
// Returns true on success, false on allocation failure
bool trace_add_dependency(Trace* t, const char* path, const Hash* hash);

// Check if all dependencies still match their recorded hashes (early cutoff)
// Returns true if all dependencies are valid, false if any have changed or are missing
bool trace_validate(const Trace* t);

// Save trace to disk in binary format
// Returns true on success, false on I/O error
bool trace_save(const Trace* t, Storage* storage);

// Load trace from disk
// Returns NULL if trace doesn't exist or on I/O error
Trace* trace_load(const Hash* request_key, Storage* storage);

#endif // REBUILD_TRACE_H
