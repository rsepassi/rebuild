#ifndef REBUILD_RECIPE_H
#define REBUILD_RECIPE_H

#include "common.h"
#include "hash.h"
#include "set.h"

// Recipe execution states
typedef enum {
    RECIPE_PENDING,      // Recipe created, not yet started
    RECIPE_RUNNING,      // Recipe is executing
    RECIPE_SUSPENDED,    // Recipe suspended waiting for dependencies
    RECIPE_COMPLETE,     // Recipe completed successfully
    RECIPE_FAILED        // Recipe failed with error
} RecipeState;

// Recipe execution context
// Tracks the state of a single recipe during build execution
typedef struct Recipe {
    char* target_name;         // Fully qualified target name (e.g., "//foo:bar")
    RecipeState state;         // Current execution state
    Hash request_key;          // Cache key for this recipe execution
    Set* declared_deps;        // All dependencies declared so far
    Set* pending_deps;         // Dependencies we're still waiting for
    char* output_dir;          // Output directory path (e.g., "outputs/foo/bar/")
    char* temp_dir;            // Temporary directory path (e.g., "tmp/foo/bar/")
    void* fiber;               // UMKA fiber handle (opaque pointer for now)
    void* user_data;           // For scheduler use (e.g., waiters list)
    uint64_t start_time;       // Start timestamp (milliseconds since epoch)
} Recipe;

// Create a new recipe for the given target
// Returns NULL on allocation failure
Recipe* recipe_create(const char* target_name);

// Free recipe and all associated resources
// Safe to call with NULL pointer
void recipe_free(Recipe* r);

// Add a dependency to the recipe
// Dependency is added to both declared_deps and pending_deps
// If already declared, this is a no-op
// Returns REBUILD_OK on success, REBUILD_ERROR_MEMORY on allocation failure
RebuildError recipe_add_dependency(Recipe* r, const char* dep_path);

// Set the output directory path for this recipe
// Makes a copy of the provided path
// Returns REBUILD_OK on success, REBUILD_ERROR_MEMORY on allocation failure
RebuildError recipe_set_output_dir(Recipe* r, const char* dir);

// Set the temporary directory path for this recipe
// Makes a copy of the provided path
// Returns REBUILD_OK on success, REBUILD_ERROR_MEMORY on allocation failure
RebuildError recipe_set_temp_dir(Recipe* r, const char* dir);

// Check if a dependency has already been declared
// Returns true if dep_path is in declared_deps, false otherwise
bool recipe_has_dependency(Recipe* r, const char* dep_path);

// Compute the request key (cache key) for this recipe
// Combines:
//   - recipe_code_hash: Hash of the recipe function bytecode
//   - target_name: The target being built
//   - declared_deps: All declared dependencies (in sorted order for determinism)
// The computed hash is stored in r->request_key
void recipe_compute_request_key(Recipe* r, const Hash* recipe_code_hash);

#endif // REBUILD_RECIPE_H
