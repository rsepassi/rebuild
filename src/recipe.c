#include "recipe.h"
#include <string.h>
#include <stdlib.h>

// Helper structure for collecting and sorting dependencies
typedef struct {
    char** deps;
    size_t count;
    size_t capacity;
} DepsArray;

// Callback for set iteration to collect dependencies
static bool collect_dep_callback(const char* value, void* user_data) {
    DepsArray* arr = (DepsArray*)user_data;

    // Grow array if needed
    if (arr->count >= arr->capacity) {
        size_t new_capacity = arr->capacity ? arr->capacity * 2 : 16;
        char** new_deps = rebuild_realloc(arr->deps, new_capacity * sizeof(char*));
        arr->deps = new_deps;
        arr->capacity = new_capacity;
    }

    arr->deps[arr->count++] = (char*)value;
    return true; // Continue iteration
}

// Comparison function for qsort
static int compare_strings(const void* a, const void* b) {
    const char* str_a = *(const char**)a;
    const char* str_b = *(const char**)b;
    return strcmp(str_a, str_b);
}

Recipe* recipe_create(const char* target_name) {
    if (target_name == NULL) {
        LOG_ERROR("Cannot create recipe with NULL target name");
        return NULL;
    }

    Recipe* r = rebuild_malloc(sizeof(Recipe));

    // Initialize all fields
    r->target_name = rebuild_strdup(target_name);
    r->state = RECIPE_PENDING;
    memset(&r->request_key, 0, sizeof(Hash));

    // Create dependency sets
    r->declared_deps = set_create(0);
    if (r->declared_deps == NULL) {
        rebuild_free(r->target_name);
        rebuild_free(r);
        return NULL;
    }

    r->pending_deps = set_create(0);
    if (r->pending_deps == NULL) {
        set_free(r->declared_deps);
        rebuild_free(r->target_name);
        rebuild_free(r);
        return NULL;
    }

    r->output_dir = NULL;
    r->temp_dir = NULL;
    r->fiber = NULL;
    r->user_data = NULL;
    r->start_time = 0;

    LOG_DEBUG("Created recipe for target: %s", target_name);

    return r;
}

void recipe_free(Recipe* r) {
    if (r == NULL) {
        return;
    }

    LOG_DEBUG("Freeing recipe for target: %s", r->target_name ? r->target_name : "(null)");

    // Free strings
    if (r->target_name) {
        rebuild_free(r->target_name);
    }
    if (r->output_dir) {
        rebuild_free(r->output_dir);
    }
    if (r->temp_dir) {
        rebuild_free(r->temp_dir);
    }

    // Free sets
    set_free(r->declared_deps);
    set_free(r->pending_deps);

    // Note: fiber and user_data are owned by scheduler, not freed here

    rebuild_free(r);
}

RebuildError recipe_add_dependency(Recipe* r, const char* dep_path) {
    if (r == NULL || dep_path == NULL) {
        return REBUILD_ERROR_MEMORY;
    }

    // Add to declared_deps (no-op if already exists)
    RebuildError err = set_add(r->declared_deps, dep_path);
    if (err != REBUILD_OK) {
        LOG_ERROR("Failed to add dependency to declared_deps: %s", dep_path);
        return err;
    }

    // Add to pending_deps (no-op if already exists)
    err = set_add(r->pending_deps, dep_path);
    if (err != REBUILD_OK) {
        LOG_ERROR("Failed to add dependency to pending_deps: %s", dep_path);
        return err;
    }

    LOG_DEBUG("Recipe %s: added dependency %s", r->target_name, dep_path);

    return REBUILD_OK;
}

RebuildError recipe_set_output_dir(Recipe* r, const char* dir) {
    if (r == NULL || dir == NULL) {
        return REBUILD_ERROR_MEMORY;
    }

    // Free existing output_dir if set
    if (r->output_dir) {
        rebuild_free(r->output_dir);
    }

    r->output_dir = rebuild_strdup(dir);
    LOG_DEBUG("Recipe %s: set output_dir to %s", r->target_name, dir);

    return REBUILD_OK;
}

RebuildError recipe_set_temp_dir(Recipe* r, const char* dir) {
    if (r == NULL || dir == NULL) {
        return REBUILD_ERROR_MEMORY;
    }

    // Free existing temp_dir if set
    if (r->temp_dir) {
        rebuild_free(r->temp_dir);
    }

    r->temp_dir = rebuild_strdup(dir);
    LOG_DEBUG("Recipe %s: set temp_dir to %s", r->target_name, dir);

    return REBUILD_OK;
}

bool recipe_has_dependency(Recipe* r, const char* dep_path) {
    if (r == NULL || dep_path == NULL) {
        return false;
    }

    return set_has(r->declared_deps, dep_path);
}

void recipe_compute_request_key(Recipe* r, const Hash* recipe_code_hash) {
    if (r == NULL || recipe_code_hash == NULL) {
        LOG_ERROR("Cannot compute request key with NULL arguments");
        return;
    }

    // We'll build up the hash incrementally
    // Start with recipe code hash
    memcpy(&r->request_key, recipe_code_hash, sizeof(Hash));

    // Hash in the target name
    Hash target_hash;
    hash_data(r->target_name, strlen(r->target_name), &target_hash);
    hash_combine(&r->request_key, &target_hash);

    // Collect dependencies into array for sorting
    DepsArray arr = {0};
    set_iterate(r->declared_deps, collect_dep_callback, &arr);

    // Sort dependencies for deterministic ordering
    if (arr.count > 0) {
        qsort(arr.deps, arr.count, sizeof(char*), compare_strings);

        // Hash each dependency in sorted order
        for (size_t i = 0; i < arr.count; i++) {
            Hash dep_hash;
            hash_data(arr.deps[i], strlen(arr.deps[i]), &dep_hash);
            hash_combine(&r->request_key, &dep_hash);
        }

        rebuild_free(arr.deps);
    }

    // Log the computed key for debugging
    char* key_hex = hash_to_hex(&r->request_key);
    if (key_hex) {
        LOG_DEBUG("Recipe %s: computed request key = %s", r->target_name, key_hex);
        rebuild_free(key_hex);
    }
}
