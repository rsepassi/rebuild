#ifndef REBUILD_UMKA_BRIDGE_H
#define REBUILD_UMKA_BRIDGE_H

#include "common.h"
#include "hash.h"
#include "recipe.h"
#include <stdbool.h>
#include <stddef.h>

// Forward declarations
typedef struct Scheduler Scheduler;
typedef struct tagUmka Umka;  // UMKA's actual type definition

// UMKA execution context - stored in thread-local storage
// Each thread executing UMKA fibers has its own context
typedef struct UmkaContext {
    Recipe* current_recipe;       // Recipe being executed in this fiber
    Scheduler* scheduler;         // Scheduler for dependency requests
    Umka* umka;                   // UMKA instance for this thread
} UmkaContext;

// Fiber handle - opaque pointer to UMKA fiber state
typedef void* UmkaFiber;

// Fiber execution result
typedef enum {
    UMKA_FIBER_RUNNING,      // Fiber is still executing
    UMKA_FIBER_SUSPENDED,    // Fiber yielded (waiting for dependency)
    UMKA_FIBER_COMPLETE,     // Fiber completed successfully
    UMKA_FIBER_ERROR         // Fiber encountered an error
} UmkaFiberStatus;

// Callback for dependency requests (called when recipe calls depend_on)
// Should return the output path when dependency is ready
// If dependency needs to be built, should return NULL and scheduler will handle it
typedef const char* (*DependOnCallback)(Scheduler* sched, Recipe* recipe, const char* target_name);

// Callback for sys() command execution
// Should spawn process asynchronously and return when complete
typedef struct {
    int exit_code;
    char* stdout_output;
    char* stderr_output;
} SysResult;

typedef void (*SysCallback)(Scheduler* sched, Recipe* recipe, const char** args, int argc, SysResult* result);

// Bridge callbacks - provided by scheduler
typedef struct {
    DependOnCallback depend_on;
    SysCallback sys;
} UmkaBridgeCallbacks;

// Initialize UMKA bridge and register FFI functions
// Must be called once at startup before any UMKA scripts are loaded
// Returns REBUILD_OK on success, error code on failure
RebuildError umka_bridge_init(void);

// Cleanup UMKA bridge resources
void umka_bridge_cleanup(void);

// Set thread-local context for current thread
// Must be called before executing any UMKA code in a thread
void umka_bridge_set_context(Recipe* recipe, Scheduler* scheduler, Umka* umka);

// Get thread-local context for current thread
// Returns NULL if no context has been set
UmkaContext* umka_bridge_get_context(void);

// Clear thread-local context
void umka_bridge_clear_context(void);

// Set bridge callbacks for scheduler integration
// Must be called before any recipes execute
void umka_bridge_set_callbacks(const UmkaBridgeCallbacks* callbacks);

// Load and compile a UMKA script from file
// Returns UMKA instance on success, NULL on error
// The caller is responsible for freeing the UMKA instance
Umka* umka_load_script(const char* path);

// Get hash of UMKA script file for cache key computation
// Returns REBUILD_OK on success, error code on failure
RebuildError umka_get_script_hash(const char* path, Hash* out_hash);

// Create a new fiber for recipe execution
// Creates a fiber that will call the specified function in the loaded script
// Returns fiber handle on success, NULL on error
UmkaFiber umka_create_fiber(Umka* umka, const char* function_name);

// Resume fiber execution
// Returns fiber status after execution
UmkaFiberStatus umka_resume_fiber(UmkaFiber fiber);

// Check if fiber has completed (either successfully or with error)
bool umka_fiber_is_done(UmkaFiber fiber);

// Free fiber resources
void umka_free_fiber(UmkaFiber fiber);

// FFI functions registered with UMKA (called from UMKA scripts)
// These are the actual C implementations that UMKA scripts will call

// Request a dependency and get its output path
// Yields the fiber until dependency is ready
// Returns output directory path as string
void umka_ffi_rebuild_depend_on(void* params, void* result);

// Execute system command and capture output
// Yields the fiber until command completes
// Returns exit code
void umka_ffi_rebuild_sys(void* params, void* result);

// Register a discovered dependency (e.g., from depfile parsing)
// Does not yield, just records the dependency
void umka_ffi_rebuild_register_dep(void* params, void* result);

// Perform glob pattern matching on filesystem
// Returns array of matching file paths
void umka_ffi_rebuild_glob(void* params, void* result);

// Get hash of a file
// Returns hex-encoded hash string
void umka_ffi_rebuild_hash_file(void* params, void* result);

// Log info message from UMKA script
void umka_ffi_rebuild_log_info(void* params, void* result);

// Log debug message from UMKA script
void umka_ffi_rebuild_log_debug(void* params, void* result);

// Register a target (called from BUILD.um files)
// Called by target(name, fn) helper to register targets with the TargetRegistry
void umka_ffi_rebuild_register_target(void* params, void* result);

#endif // REBUILD_UMKA_BRIDGE_H
