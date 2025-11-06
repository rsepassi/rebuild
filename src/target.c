#include "target.h"
#include "umka_bridge.h"
#include <string.h>
#include <stdlib.h>

// Global pointer to current registry during BUILD.um loading
// This is used by the rebuild_register_target FFI function to know
// which registry to register targets into
TargetRegistry* g_current_registry = NULL;

// Create a new target registry
TargetRegistry* target_registry_create(void* umka) {
    TargetRegistry* registry = rebuild_malloc(sizeof(TargetRegistry));
    if (!registry) {
        LOG_ERROR("Failed to allocate target registry");
        return NULL;
    }

    registry->targets = map_create(0);
    if (!registry->targets) {
        LOG_ERROR("Failed to create targets map");
        rebuild_free(registry);
        return NULL;
    }

    registry->umka = umka;

    LOG_DEBUG("Created target registry");
    return registry;
}

// Free a single target
void target_free(void* target_ptr) {
    if (!target_ptr) {
        return;
    }

    Target* target = (Target*)target_ptr;

    if (target->name) {
        rebuild_free(target->name);
    }

    if (target->function_name) {
        rebuild_free(target->function_name);
    }

    // Note: umka_script is owned by the registry or elsewhere, not freed here

    rebuild_free(target);
}

// Free target registry
void target_registry_free(TargetRegistry* registry) {
    if (!registry) {
        return;
    }

    // Free all targets
    if (registry->targets) {
        map_free(registry->targets, target_free);
    }

    rebuild_free(registry);

    LOG_DEBUG("Freed target registry");
}

// Register a new target
RebuildError target_registry_register(TargetRegistry* registry,
                                     const char* name,
                                     const char* function_name,
                                     void* script) {
    if (!registry || !name || !function_name) {
        LOG_ERROR("Invalid parameters to target_registry_register");
        return REBUILD_ERROR_PARSE;
    }

    // Check if target already exists
    if (target_registry_has(registry, name)) {
        LOG_WARN("Target '%s' already registered, replacing", name);
        // Remove old target
        Target* old_target = map_remove(registry->targets, name);
        if (old_target) {
            target_free(old_target);
        }
    }

    // Create new target
    Target* target = rebuild_malloc(sizeof(Target));
    if (!target) {
        LOG_ERROR("Failed to allocate target");
        return REBUILD_ERROR_MEMORY;
    }

    target->name = rebuild_strdup(name);
    target->function_name = rebuild_strdup(function_name);
    target->umka_script = script;

    if (!target->name || !target->function_name) {
        LOG_ERROR("Failed to duplicate target strings");
        target_free(target);
        return REBUILD_ERROR_MEMORY;
    }

    // Add to registry
    RebuildError err = map_set(registry->targets, name, target);
    if (err != REBUILD_OK) {
        LOG_ERROR("Failed to add target '%s' to registry", name);
        target_free(target);
        return err;
    }

    LOG_INFO("Registered target: %s -> %s()", name, function_name);
    return REBUILD_OK;
}

// Get a target by name
Target* target_registry_get(TargetRegistry* registry, const char* name) {
    if (!registry || !name) {
        return NULL;
    }

    return (Target*)map_get(registry->targets, name);
}

// Check if a target exists
bool target_registry_has(TargetRegistry* registry, const char* name) {
    if (!registry || !name) {
        return false;
    }

    return map_has(registry->targets, name);
}

// Iterator callback for collecting target names
typedef struct {
    char** names;
    size_t index;
} NameCollector;

static bool collect_name_iterator(const char* key, void* value, void* user_data) {
    NameCollector* collector = (NameCollector*)user_data;
    collector->names[collector->index++] = (char*)key;
    return true;  // Continue iteration
}

// Get list of all target names
char** target_registry_list(TargetRegistry* registry, size_t* count) {
    if (!registry || !count) {
        return NULL;
    }

    *count = map_size(registry->targets);
    if (*count == 0) {
        return NULL;
    }

    // Allocate array for names
    char** names = rebuild_malloc(sizeof(char*) * (*count));
    if (!names) {
        LOG_ERROR("Failed to allocate names array");
        return NULL;
    }

    // Collect names using iterator
    NameCollector collector = {
        .names = names,
        .index = 0
    };

    map_iterate(registry->targets, collect_name_iterator, &collector);

    return names;
}

// Load a BUILD.um file and register its targets
RebuildError target_registry_load_build_file(TargetRegistry* registry, const char* path) {
    if (!registry || !path) {
        LOG_ERROR("Invalid parameters to target_registry_load_build_file");
        return REBUILD_ERROR_PARSE;
    }

    LOG_INFO("Loading BUILD file: %s", path);

    // Set global registry so rebuild_register_target knows where to register
    TargetRegistry* prev_registry = g_current_registry;
    g_current_registry = registry;

    // Load and compile the UMKA script
    Umka* build_script = umka_load_script(path);
    if (!build_script) {
        LOG_ERROR("Failed to load BUILD file: %s", path);
        g_current_registry = prev_registry;
        return REBUILD_ERROR_PARSE;
    }

    // Create fiber for register_targets() function
    UmkaFiber fiber = umka_create_fiber(build_script, "register_targets");
    if (!fiber) {
        LOG_ERROR("BUILD file '%s' does not define register_targets() function", path);
        // Note: We might want to free build_script here if we own it
        g_current_registry = prev_registry;
        return REBUILD_ERROR_PARSE;
    }

    // Execute register_targets() - this will call back to rebuild_register_target
    // for each target defined in the BUILD.um file
    UmkaFiberStatus status = umka_resume_fiber(fiber);

    if (status == UMKA_FIBER_ERROR) {
        LOG_ERROR("Failed to execute register_targets() in BUILD file: %s", path);
        umka_free_fiber(fiber);
        g_current_registry = prev_registry;
        return REBUILD_ERROR_EXEC;
    }

    // Clean up fiber
    umka_free_fiber(fiber);

    // Restore previous registry
    g_current_registry = prev_registry;

    LOG_INFO("Successfully loaded BUILD file: %s", path);
    return REBUILD_OK;
}

// FFI function called from BUILD.um files to register targets
// This is called by the target(name, fn) helper in BUILD.um
// It should be registered with UMKA and added to umka_bridge.c
void target_registry_ffi_register(const char* name, const char* function_name) {
    if (!g_current_registry) {
        LOG_ERROR("rebuild_register_target called with no active registry");
        return;
    }

    if (!name || !function_name) {
        LOG_ERROR("rebuild_register_target called with NULL name or function_name");
        return;
    }

    // Register the target with the current registry
    // The script is the registry's UMKA instance
    RebuildError err = target_registry_register(g_current_registry,
                                                name,
                                                function_name,
                                                g_current_registry->umka);

    if (err != REBUILD_OK) {
        LOG_ERROR("Failed to register target '%s' from BUILD file", name);
    }
}

// Get the current registry (for FFI use)
TargetRegistry* target_registry_get_current(void) {
    return g_current_registry;
}
