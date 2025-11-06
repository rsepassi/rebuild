/**
 * scheduler.c - Async recipe scheduler for Rebuild build system
 *
 * The scheduler orchestrates the execution of build recipes with support for:
 * - Dynamic dependency discovery (recipes can request dependencies during execution)
 * - Suspending execution (recipes suspend when waiting for dependencies)
 * - Content-addressed caching (via constructive traces)
 * - Async I/O via libuv (Phase 3+)
 *
 * PHASE 1+2 IMPLEMENTATION (Current):
 * - Synchronous recipe execution (simplified from full async)
 * - Focus on correctness over maximum parallelism
 * - Proper trace checking and caching
 * - Foundation for future async parallelism
 *
 * PHASE 3+ ENHANCEMENTS (Future):
 * - Parallel recipe execution via libuv thread pool
 * - Async process spawning with uv_spawn
 * - Async file I/O for hashing and trace validation
 * - Maximum parallelism across independent recipes
 *
 * ARCHITECTURE:
 * - Recipes: Tracked in a map, keyed by target name
 * - Ready Queue: Recipes ready to execute (FIFO)
 * - Waiting Map: Maps targets to recipes waiting on them
 * - Completed Map: Maps targets to their output paths
 * - Storage: Content-addressed trace and output storage
 *
 * RECIPE STATE MACHINE:
 * PENDING -> RUNNING -> COMPLETE
 *         \-> SUSPENDED -> RUNNING -> COMPLETE
 *         \-> FAILED
 *
 * When a recipe calls depend_on():
 * 1. If dependency is complete, return output path immediately
 * 2. If dependency is pending, suspend recipe and queue dependency
 * 3. When dependency completes, resume all waiting recipes
 */

#define _GNU_SOURCE

#include "scheduler.h"
#include "target.h"
#include "trace.h"
#include "hash.h"
#include "set.h"
#include "buffer.h"
#include "umka_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

// ============================================================================
// Queue implementation for recipe scheduling
// ============================================================================

typedef struct QueueNode {
    Recipe* recipe;
    struct QueueNode* next;
} QueueNode;

struct Queue {
    QueueNode* head;
    QueueNode* tail;
    size_t size;
};

static Queue* queue_create(void) {
    Queue* q = (Queue*)rebuild_calloc(1, sizeof(Queue));
    return q;
}

static void queue_free(Queue* q) {
    if (!q) return;

    QueueNode* node = q->head;
    while (node) {
        QueueNode* next = node->next;
        rebuild_free(node);
        node = next;
    }
    rebuild_free(q);
}

static bool queue_push(Queue* q, Recipe* recipe) {
    if (!q || !recipe) return false;

    QueueNode* node = (QueueNode*)rebuild_malloc(sizeof(QueueNode));
    if (!node) return false;

    node->recipe = recipe;
    node->next = NULL;

    if (q->tail) {
        q->tail->next = node;
        q->tail = node;
    } else {
        q->head = q->tail = node;
    }

    q->size++;
    return true;
}

static Recipe* queue_pop(Queue* q) {
    if (!q || !q->head) return NULL;

    QueueNode* node = q->head;
    Recipe* recipe = node->recipe;

    q->head = node->next;
    if (!q->head) {
        q->tail = NULL;
    }

    rebuild_free(node);
    q->size--;
    return recipe;
}

static bool queue_is_empty(const Queue* q) {
    return !q || q->size == 0;
}

// ============================================================================
// Waiter list for tracking recipes waiting on dependencies
// ============================================================================

typedef struct WaiterNode {
    Recipe* recipe;
    struct WaiterNode* next;
} WaiterNode;

struct WaiterList {
    WaiterNode* head;
    size_t count;
};

static WaiterList* waiter_list_create(void) {
    WaiterList* list = (WaiterList*)rebuild_calloc(1, sizeof(WaiterList));
    return list;
}

static void waiter_list_free(WaiterList* list) {
    if (!list) return;

    WaiterNode* node = list->head;
    while (node) {
        WaiterNode* next = node->next;
        rebuild_free(node);
        node = next;
    }
    rebuild_free(list);
}

static bool waiter_list_add(WaiterList* list, Recipe* recipe) {
    if (!list || !recipe) return false;

    WaiterNode* node = (WaiterNode*)rebuild_malloc(sizeof(WaiterNode));
    if (!node) return false;

    node->recipe = recipe;
    node->next = list->head;
    list->head = node;
    list->count++;
    return true;
}

static void waiter_list_notify_all(WaiterList* list, Scheduler* sched, const char* dep_output_path) {
    if (!list) return;

    WaiterNode* node = list->head;
    while (node) {
        Recipe* waiter = node->recipe;

        // Remove this dependency from pending set
        if (waiter->pending_deps) {
            // We need to track which dependency completed - store in recipe's user_data
            // For now, just mark recipe as ready if no more pending deps
            // Note: This is simplified - full implementation would track specific deps
        }

        // Resume the recipe
        scheduler_resume_recipe(sched, waiter, dep_output_path);

        node = node->next;
    }
}

// ============================================================================
// Helper functions
// ============================================================================

// Ensure directory exists, creating parent directories as needed
static bool ensure_directory(const char* path) {
    if (!path) return false;

    // Check if already exists
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    // Create parent directories recursively
    char* path_copy = rebuild_strdup(path);
    if (!path_copy) return false;

    for (char* p = path_copy + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(path_copy, 0755);
            *p = '/';
        }
    }

    // Create final directory
    bool result = (mkdir(path_copy, 0755) == 0) || (errno == EEXIST);
    rebuild_free(path_copy);
    return result;
}

// ============================================================================
// Scheduler implementation
// ============================================================================

Scheduler* scheduler_create(Storage* storage) {
    if (!storage) return NULL;

    Scheduler* sched = (Scheduler*)rebuild_calloc(1, sizeof(Scheduler));
    if (!sched) return NULL;

    // Initialize libuv event loop
    sched->loop = uv_default_loop();
    if (!sched->loop) {
        rebuild_free(sched);
        return NULL;
    }

    sched->storage = storage;

    // Create tool manager
    sched->tools = tool_manager_create();
    if (!sched->tools) {
        rebuild_free(sched);
        return NULL;
    }

    // Create maps and queues
    sched->recipes = map_create(64);
    sched->completed = map_create(64);
    sched->waiting = map_create(64);
    sched->ready_queue = queue_create();

    if (!sched->recipes || !sched->completed || !sched->waiting || !sched->ready_queue) {
        scheduler_free(sched);
        return NULL;
    }

    sched->active_count = 0;
    sched->failed = false;
    sched->target_error = NULL;
    sched->umka = NULL;  // Will be set when UMKA is initialized
    sched->registry = NULL;  // Will be set after BUILD.um loads

    LOG_DEBUG("Scheduler created");
    return sched;
}

void scheduler_free(Scheduler* sched) {
    if (!sched) return;

    // Free recipes
    if (sched->recipes) {
        map_free(sched->recipes, (MapValueFreeFn)recipe_free);
    }

    // Free completed paths
    if (sched->completed) {
        map_free(sched->completed, (MapValueFreeFn)rebuild_free);
    }

    // Free waiter lists
    if (sched->waiting) {
        map_free(sched->waiting, (MapValueFreeFn)waiter_list_free);
    }

    // Free queue
    queue_free(sched->ready_queue);

    // Free tool manager
    tool_manager_free(sched->tools);

    // Free target registry if present
    if (sched->registry) {
        target_registry_free(sched->registry);
    }

    rebuild_free(sched);
    LOG_DEBUG("Scheduler freed");
}

Recipe* scheduler_get_recipe(Scheduler* sched, const char* target_name) {
    if (!sched || !target_name) return NULL;

    // Check if recipe already exists
    Recipe* recipe = (Recipe*)map_get(sched->recipes, target_name);
    if (recipe) {
        return recipe;
    }

    // Create new recipe
    recipe = recipe_create(target_name);
    if (!recipe) {
        LOG_ERROR("Failed to create recipe for target: %s", target_name);
        return NULL;
    }

    // Add to recipes map
    if (map_set(sched->recipes, target_name, recipe) != REBUILD_OK) {
        LOG_ERROR("Failed to add recipe to map: %s", target_name);
        recipe_free(recipe);
        return NULL;
    }

    LOG_DEBUG("Created recipe for target: %s", target_name);
    return recipe;
}

const char* scheduler_get_completed(Scheduler* sched, const char* target_name) {
    if (!sched || !target_name) return NULL;
    return (const char*)map_get(sched->completed, target_name);
}

RebuildError scheduler_mark_completed(Scheduler* sched, const char* target_name, const char* output_path) {
    if (!sched || !target_name || !output_path) {
        return REBUILD_ERROR_MEMORY;
    }

    // Make a copy of the output path
    char* path_copy = rebuild_strdup(output_path);
    if (!path_copy) {
        return REBUILD_ERROR_MEMORY;
    }

    // Add to completed map
    RebuildError err = map_set(sched->completed, target_name, path_copy);
    if (err != REBUILD_OK) {
        rebuild_free(path_copy);
        return err;
    }

    LOG_INFO("Target completed: %s -> %s", target_name, output_path);
    return REBUILD_OK;
}

bool scheduler_check_cache(Scheduler* sched, Recipe* recipe) {
    if (!sched || !recipe) return false;

    LOG_DEBUG("Checking cache for: %s", recipe->target_name);

    // Compute request key for this recipe
    // For Phase 1+2, we'll use a simplified key based on target name
    // In Phase 3+, this would include recipe bytecode hash, tool hashes, etc.
    Hash request_key;
    // For now, just hash the target name as a placeholder
    // TODO: Implement proper request key computation with recipe bytecode
    hash_data((const uint8_t*)recipe->target_name, strlen(recipe->target_name), &request_key);

    // Store request key in recipe
    memcpy(&recipe->request_key, &request_key, sizeof(Hash));

    // Try to load trace from storage
    Trace* trace = trace_load(&request_key, sched->storage);
    if (!trace) {
        LOG_DEBUG("No cached trace found for: %s", recipe->target_name);
        return false;
    }

    // Validate trace dependencies
    bool valid = trace_validate(trace);

    if (valid) {
        LOG_INFO("Cache hit for: %s", recipe->target_name);

        // Get cached output path from trace
        // For now, we'll construct the output path from the recipe
        // In a full implementation, this would be stored in the trace
        char* output_path = rebuild_strdup(recipe->output_dir ? recipe->output_dir : "outputs");

        if (output_path) {
            // Mark recipe as complete
            recipe->state = RECIPE_COMPLETE;
            scheduler_mark_completed(sched, recipe->target_name, output_path);
            rebuild_free(output_path);
        }

        trace_free(trace);
        return true;
    } else {
        LOG_DEBUG("Cache invalid for: %s (dependencies changed)", recipe->target_name);
        trace_free(trace);
        return false;
    }
}

void scheduler_execute_recipe(Scheduler* sched, Recipe* recipe) {
    if (!sched || !recipe) return;

    LOG_INFO("Executing recipe: %s", recipe->target_name);

    // Set recipe state to running
    recipe->state = RECIPE_RUNNING;
    sched->active_count++;

    // Record start time for performance tracking
    recipe->start_time = uv_hrtime() / 1000000;  // Convert to milliseconds

    // For Phase 1+2: Synchronous execution
    // In Phase 3+, this would queue to thread pool with uv_queue_work:
    // uv_work_t* work = malloc(sizeof(uv_work_t));
    // work->data = recipe;
    // uv_queue_work(sched->loop, work, worker_execute_recipe, on_worker_complete);

    // Set up UMKA context for this recipe
    umka_bridge_set_context(recipe, sched);

    // Create output and temp directories
    if (!recipe->output_dir) {
        char path[256];
        snprintf(path, sizeof(path), "outputs/%s", recipe->target_name);
        recipe_set_output_dir(recipe, path);

        // Ensure output directory exists
        ensure_directory(recipe->output_dir);
    }

    if (!recipe->temp_dir) {
        recipe->temp_dir = storage_get_tmp_dir(sched->storage, recipe->target_name);

        // Ensure temp directory exists
        if (recipe->temp_dir) {
            ensure_directory(recipe->temp_dir);
        }
    }

    // Execute the UMKA recipe
    if (!sched->umka || !sched->registry) {
        LOG_ERROR("No UMKA instance or registry available");
        scheduler_on_recipe_complete(sched, recipe, false);
        return;
    }

    // Get the target from registry
    Target* target = target_registry_get(sched->registry, recipe->target_name);
    if (!target) {
        LOG_ERROR("Target not found in registry: %s", recipe->target_name);
        scheduler_on_recipe_complete(sched, recipe, false);
        return;
    }

    LOG_INFO("Executing UMKA function for target: %s -> %s",
             recipe->target_name, target->function_name);

    // Create fiber for target function
    UmkaFiber fiber = umka_create_fiber(sched->umka, target->function_name);
    if (!fiber) {
        LOG_ERROR("Failed to create fiber for target: %s", recipe->target_name);
        scheduler_on_recipe_complete(sched, recipe, false);
        return;
    }

    // Execute the fiber
    UmkaFiberStatus status = umka_resume_fiber(fiber);

    // Handle result
    bool success = (status == UMKA_FIBER_COMPLETE);
    if (!success) {
        LOG_ERROR("Recipe execution failed: %s", recipe->target_name);
    }

    umka_free_fiber(fiber);
    scheduler_on_recipe_complete(sched, recipe, success);
}

void scheduler_on_recipe_complete(Scheduler* sched, Recipe* recipe, bool success) {
    if (!sched || !recipe) return;

    sched->active_count--;

    // Calculate elapsed time
    uint64_t elapsed_time = (uv_hrtime() / 1000000) - recipe->start_time;

    if (success) {
        LOG_INFO("Recipe succeeded: %s (took %llu ms)", recipe->target_name,
                 (unsigned long long)elapsed_time);
        recipe->state = RECIPE_COMPLETE;

        // Create and save trace
        Trace* trace = trace_create(&recipe->request_key);
        if (trace) {
            // Set performance metrics
            trace->wall_time_ms = elapsed_time;
            trace->cpu_time_ms = elapsed_time;  // For Phase 1+2, use wall time

            // Add all dependencies to trace
            // TODO: Iterate over recipe->declared_deps and add to trace with hashes
            // Example:
            // set_iterate(recipe->declared_deps, add_dep_to_trace_callback, trace);

            // Set output tree hash
            // TODO: Hash the output directory tree
            // For now, use a placeholder
            hash_data((const uint8_t*)"", 0, &trace->output_tree_hash);

            // Save trace to storage
            if (!trace_save(trace, sched->storage)) {
                LOG_WARN("Failed to save trace for: %s", recipe->target_name);
            }

            trace_free(trace);
        }

        // Mark as completed
        const char* output_path = recipe->output_dir ? recipe->output_dir : "outputs";
        scheduler_mark_completed(sched, recipe->target_name, output_path);

        // Notify waiters
        WaiterList* waiters = (WaiterList*)map_get(sched->waiting, recipe->target_name);
        if (waiters) {
            waiter_list_notify_all(waiters, sched, output_path);
            map_remove(sched->waiting, recipe->target_name);
            waiter_list_free(waiters);
        }
    } else {
        LOG_ERROR("Recipe failed: %s", recipe->target_name);
        recipe->state = RECIPE_FAILED;
        sched->failed = true;
        sched->target_error = recipe->target_name;
    }

    // Clear UMKA context
    umka_bridge_clear_context();
}

const char* scheduler_on_depend_request(Scheduler* sched, Recipe* recipe, const char* target_name) {
    if (!sched || !recipe || !target_name) return NULL;

    LOG_DEBUG("Dependency request from %s: %s", recipe->target_name, target_name);

    // Add to recipe's dependencies
    recipe_add_dependency(recipe, target_name);

    // Check if dependency is already completed
    const char* completed_path = scheduler_get_completed(sched, target_name);
    if (completed_path) {
        LOG_DEBUG("Dependency already completed: %s -> %s", target_name, completed_path);
        return completed_path;
    }

    // Get or create recipe for dependency
    Recipe* dep_recipe = scheduler_get_recipe(sched, target_name);
    if (!dep_recipe) {
        LOG_ERROR("Failed to create recipe for dependency: %s", target_name);
        return NULL;
    }

    // Check dependency state
    if (dep_recipe->state == RECIPE_COMPLETE) {
        // Already complete
        const char* output = scheduler_get_completed(sched, target_name);
        return output;
    } else if (dep_recipe->state == RECIPE_PENDING) {
        // Need to build it
        LOG_DEBUG("Queuing dependency for build: %s", target_name);

        // Suspend current recipe
        recipe->state = RECIPE_SUSPENDED;

        // Add current recipe to waiter list for dependency
        WaiterList* waiters = (WaiterList*)map_get(sched->waiting, target_name);
        if (!waiters) {
            waiters = waiter_list_create();
            if (waiters) {
                map_set(sched->waiting, target_name, waiters);
            }
        }
        if (waiters) {
            waiter_list_add(waiters, recipe);
        }

        // Queue dependency for execution
        queue_push(sched->ready_queue, dep_recipe);

        return NULL;  // Indicates suspension
    } else {
        // Dependency is running or suspended - add to waiters
        LOG_DEBUG("Waiting for in-progress dependency: %s", target_name);

        recipe->state = RECIPE_SUSPENDED;

        WaiterList* waiters = (WaiterList*)map_get(sched->waiting, target_name);
        if (!waiters) {
            waiters = waiter_list_create();
            if (waiters) {
                map_set(sched->waiting, target_name, waiters);
            }
        }
        if (waiters) {
            waiter_list_add(waiters, recipe);
        }

        return NULL;  // Indicates suspension
    }
}

void scheduler_resume_recipe(Scheduler* sched, Recipe* recipe, const char* dep_output_path) {
    if (!sched || !recipe) return;

    LOG_DEBUG("Resuming recipe: %s (dependency ready: %s)",
              recipe->target_name, dep_output_path ? dep_output_path : "unknown");

    // Change state from suspended to ready
    if (recipe->state == RECIPE_SUSPENDED) {
        recipe->state = RECIPE_PENDING;
    }

    // In Phase 3+, we would pass dep_output_path to the UMKA fiber
    // For now, the dependency result is available via scheduler_get_completed()
    (void)dep_output_path;  // Suppress unused warning for Phase 1+2

    // Queue for execution
    queue_push(sched->ready_queue, recipe);
}

int scheduler_execute_sys(Scheduler* sched, Recipe* recipe, const char** args, int argc,
                          char** out_stdout, char** out_stderr) {
    if (!sched || !recipe || !args || argc == 0) {
        return -1;
    }

    // For Phase 1+2: Synchronous execution using fork/exec
    // In Phase 3+: Use uv_spawn for async execution

    LOG_DEBUG("Executing sys command: %s", args[0]);

    // Create pipes for stdout and stderr
    int stdout_pipe[2];
    int stderr_pipe[2];

    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        LOG_ERROR("Failed to create pipes: %s", strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("Failed to fork: %s", strerror(errno));
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        // Child process

        // Redirect stdout
        close(stdout_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdout_pipe[1]);

        // Redirect stderr
        close(stderr_pipe[0]);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stderr_pipe[1]);

        // Change to temp directory if available
        if (recipe->temp_dir) {
            chdir(recipe->temp_dir);
        }

        // Execute command
        execvp(args[0], (char* const*)args);

        // If we get here, exec failed
        fprintf(stderr, "Failed to execute: %s\n", strerror(errno));
        _exit(127);
    } else {
        // Parent process
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        // Read stdout
        Buffer* stdout_buf = buffer_create(1024);
        char buf[4096];
        ssize_t n;
        while ((n = read(stdout_pipe[0], buf, sizeof(buf))) > 0) {
            buffer_append(stdout_buf, buf, n);
        }
        close(stdout_pipe[0]);

        // Read stderr
        Buffer* stderr_buf = buffer_create(1024);
        while ((n = read(stderr_pipe[0], buf, sizeof(buf))) > 0) {
            buffer_append(stderr_buf, buf, n);
        }
        close(stderr_pipe[0]);

        // Wait for child to complete
        int status;
        waitpid(pid, &status, 0);

        // Extract exit code
        int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        // Store output if requested
        if (out_stdout) {
            *out_stdout = buffer_to_string(stdout_buf);
        }
        if (out_stderr) {
            *out_stderr = buffer_to_string(stderr_buf);
        }

        buffer_free(stdout_buf);
        buffer_free(stderr_buf);

        LOG_DEBUG("Command completed with exit code: %d", exit_code);
        return exit_code;
    }
}

RebuildError scheduler_build(Scheduler* sched, const char* target_name) {
    if (!sched || !target_name) {
        return REBUILD_ERROR_MEMORY;
    }

    LOG_INFO("Building target: %s", target_name);

    // Get or create recipe for target
    Recipe* recipe = scheduler_get_recipe(sched, target_name);
    if (!recipe) {
        LOG_ERROR("Failed to create recipe for target: %s", target_name);
        return REBUILD_ERROR_MEMORY;
    }

    // Check if already completed
    if (scheduler_get_completed(sched, target_name)) {
        LOG_INFO("Target already built: %s", target_name);
        return REBUILD_OK;
    }

    // Check cache
    if (scheduler_check_cache(sched, recipe)) {
        LOG_INFO("Using cached result for: %s", target_name);
        return REBUILD_OK;
    }

    // Queue recipe for execution
    queue_push(sched->ready_queue, recipe);

    // Run the scheduler
    return scheduler_run(sched);
}

RebuildError scheduler_run(Scheduler* sched) {
    if (!sched) return REBUILD_ERROR_MEMORY;

    LOG_DEBUG("Starting scheduler event loop");

    // Process ready queue until empty or failure
    while (!queue_is_empty(sched->ready_queue) && !sched->failed) {
        Recipe* recipe = queue_pop(sched->ready_queue);
        if (!recipe) continue;

        // Skip if already complete
        if (recipe->state == RECIPE_COMPLETE) {
            continue;
        }

        // Execute recipe
        scheduler_execute_recipe(sched, recipe);

        // For Phase 1+2 with synchronous execution, we process one at a time
        // In Phase 3+, multiple recipes would run in parallel via thread pool
    }

    // Check if any recipes failed
    if (sched->failed) {
        LOG_ERROR("Build failed: %s", sched->target_error ? sched->target_error : "unknown");
        return REBUILD_ERROR_EXEC;
    }

    // Wait for any pending async operations
    // For Phase 1+2, this is a no-op since we're synchronous
    // In Phase 3+, this would be: uv_run(sched->loop, UV_RUN_DEFAULT);

    LOG_INFO("Build completed successfully");
    return REBUILD_OK;
}
