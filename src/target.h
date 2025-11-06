#ifndef REBUILD_TARGET_H
#define REBUILD_TARGET_H

#include "common.h"
#include "map.h"

// Forward declaration for UMKA instance
typedef struct tagUmka Umka;

// Target definition
// Represents a buildable target defined in a BUILD.um file
typedef struct Target {
    char* name;              // Target name (e.g., "rebuild", "lib:foo")
    char* function_name;     // UMKA function name (e.g., "target_rebuild")
    void* umka_script;       // UMKA script instance (actually Umka*)
} Target;

// Target registry
// Manages all registered targets across all loaded BUILD.um files
typedef struct TargetRegistry {
    Map* targets;            // name -> Target*
    void* umka;              // UMKA instance (actually Umka*)
} TargetRegistry;

// Create a new target registry
// Returns NULL on allocation failure
TargetRegistry* target_registry_create(void* umka);

// Free target registry and all registered targets
// Safe to call with NULL pointer
void target_registry_free(TargetRegistry* registry);

// Register a new target with the registry
// Makes copies of name and function_name
// Returns REBUILD_OK on success, error code on failure
RebuildError target_registry_register(TargetRegistry* registry,
                                     const char* name,
                                     const char* function_name,
                                     void* script);

// Get a target by name
// Returns NULL if target not found
Target* target_registry_get(TargetRegistry* registry, const char* name);

// Check if a target exists
// Returns true if target is registered, false otherwise
bool target_registry_has(TargetRegistry* registry, const char* name);

// Get list of all target names
// Returns array of target names (caller must free the array but not the strings)
// count is set to the number of targets
// Returns NULL on allocation failure
char** target_registry_list(TargetRegistry* registry, size_t* count);

// Load a BUILD.um file and register its targets
// The BUILD.um file should define a register_targets() function that calls
// target(name, fn) for each target, which in turn calls rebuild_register_target()
// Returns REBUILD_OK on success, error code on failure
RebuildError target_registry_load_build_file(TargetRegistry* registry, const char* path);

// Free a single target
// Helper function for map cleanup
void target_free(void* target_ptr);

// Global registry pointer for FFI callbacks
// Set this before calling register_targets() to enable target registration
extern TargetRegistry* g_current_registry;

#endif // REBUILD_TARGET_H
