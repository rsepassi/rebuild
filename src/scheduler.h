#ifndef REBUILD_SCHEDULER_H
#define REBUILD_SCHEDULER_H

#include "common.h"
#include "storage.h"
#include "tool.h"
#include "recipe.h"
#include "map.h"
#include <uv.h>
#include <stdbool.h>

// Forward declarations
typedef struct Queue Queue;
typedef struct WaiterList WaiterList;
typedef struct TargetRegistry TargetRegistry;

// Scheduler manages the build execution with async I/O via libuv
// Coordinates recipe execution, dependency resolution, and caching
typedef struct Scheduler {
    uv_loop_t* loop;               // libuv event loop
    Storage* storage;              // Content-addressed storage
    ToolManager* tools;            // Tool manager
    Map* recipes;                  // target_name -> Recipe*
    Map* completed;                // target_name -> output_path (char*)
    Queue* ready_queue;            // Recipes ready to execute
    Map* waiting;                  // target_name -> WaiterList*
    void* umka;                    // UMKA instance (opaque)
    struct TargetRegistry* registry; // Target registry
    int active_count;              // Number of active/running recipes
    bool failed;                   // True if any recipe has failed
    const char* target_error;      // Name of failed target (for error reporting)
} Scheduler;

// Create a new scheduler with the given storage
// Initializes libuv event loop and all internal data structures
// Returns NULL on allocation failure
Scheduler* scheduler_create(Storage* storage);

// Free scheduler and all associated resources
// Does not free the storage (caller's responsibility)
void scheduler_free(Scheduler* sched);

// Build a target by name
// This is the main entry point for building
// Returns REBUILD_OK on success, error code on failure
RebuildError scheduler_build(Scheduler* sched, const char* target_name);

// Run the event loop until all recipes complete
// Returns REBUILD_OK if all recipes succeeded, error code if any failed
RebuildError scheduler_run(Scheduler* sched);

// Internal API - these are called by the scheduler and UMKA bridge

// Get or create a recipe for the given target
// Returns existing recipe if already created, otherwise creates new one
// Returns NULL on allocation failure
Recipe* scheduler_get_recipe(Scheduler* sched, const char* target_name);

// Check if target is already completed
// Returns output path if completed, NULL otherwise
const char* scheduler_get_completed(Scheduler* sched, const char* target_name);

// Mark a recipe as completed with the given output path
// Makes a copy of output_path
// Returns REBUILD_OK on success, error code on allocation failure
RebuildError scheduler_mark_completed(Scheduler* sched, const char* target_name, const char* output_path);

// Check cache for a recipe
// Loads trace from storage and validates dependencies
// If valid, marks recipe as complete and returns true
// If invalid or no trace, returns false
bool scheduler_check_cache(Scheduler* sched, Recipe* recipe);

// Execute a recipe (queue it for execution)
// For Phase 1+2: executes synchronously
// For Phase 3+: queues to thread pool
void scheduler_execute_recipe(Scheduler* sched, Recipe* recipe);

// Handle recipe completion
// Updates state, notifies waiters, queues dependent recipes
void scheduler_on_recipe_complete(Scheduler* sched, Recipe* recipe, bool success);

// Handle depend_on() call from recipe
// This is called when a recipe requests a dependency
// If dependency is ready, returns output path
// If not ready, suspends recipe and queues dependency
// Returns output path when ready, NULL if needs to suspend
const char* scheduler_on_depend_request(Scheduler* sched, Recipe* recipe, const char* target_name);

// Resume a suspended recipe after dependency is ready
// Queues recipe for execution with dependency result
void scheduler_resume_recipe(Scheduler* sched, Recipe* recipe, const char* dep_output_path);

// Execute system command (for sys() calls)
// For Phase 1+2: executes synchronously
// For Phase 3+: uses uv_spawn for async execution
// Returns exit code
int scheduler_execute_sys(Scheduler* sched, Recipe* recipe, const char** args, int argc,
                          char** out_stdout, char** out_stderr);

#endif // REBUILD_SCHEDULER_H
