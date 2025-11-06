/**
 * main.c - Entry point for Rebuild build system
 *
 * This is the main entry point for the rebuild CLI. It:
 * 1. Parses command line arguments
 * 2. Initializes all subsystems (storage, tools, UMKA, scheduler)
 * 3. Loads BUILD.um file to register targets
 * 4. Builds the requested target
 * 5. Cleans up and exits
 */

#define _GNU_SOURCE
#include "common.h"
#include "storage.h"
#include "tool.h"
#include "umka_bridge.h"
#include "scheduler.h"
#include "recipe.h"
#include "target.h"
#include "umka_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

// Forward declarations for helper functions
static void print_usage(const char* program_name);
static void print_version(void);
static char* find_build_file(void);

/**
 * Print usage information to stderr
 */
static void print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s [OPTIONS] <target>\n", program_name);
    fprintf(stderr, "\n");
    fprintf(stderr, "Build a target defined in BUILD.um\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -h, --help       Show this help message and exit\n");
    fprintf(stderr, "  --version        Show version information and exit\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Arguments:\n");
    fprintf(stderr, "  target           Name of the target to build\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s my_app        Build 'my_app' target\n", program_name);
    fprintf(stderr, "  %s --help        Show this help\n", program_name);
    fprintf(stderr, "\n");
}

/**
 * Print version information to stdout
 */
static void print_version(void) {
    printf("rebuild version %s\n", REBUILD_VERSION);
    printf("A modern build system with constructive traces\n");
}

/**
 * Find BUILD.um file in current directory or parent directories
 *
 * Searches upward from the current working directory to find BUILD.um.
 * Returns the absolute path to BUILD.um if found, NULL otherwise.
 * Caller must free the returned string.
 */
static char* find_build_file(void) {
    char* cwd = getcwd(NULL, 0);
    if (!cwd) {
        LOG_ERROR("Failed to get current working directory: %s", strerror(errno));
        return NULL;
    }

    char* search_dir = cwd;
    char* build_file_path = NULL;

    // Search upward through directory tree
    while (1) {
        // Construct path to BUILD.um
        size_t path_len = strlen(search_dir) + strlen("/BUILD.um") + 1;
        char* candidate = rebuild_malloc(path_len);
        snprintf(candidate, path_len, "%s/BUILD.um", search_dir);

        // Check if BUILD.um exists at this path
        struct stat st;
        if (stat(candidate, &st) == 0 && S_ISREG(st.st_mode)) {
            LOG_INFO("Found BUILD.um at: %s", candidate);
            build_file_path = candidate;
            break;
        }

        rebuild_free(candidate);

        // Move up to parent directory
        char* parent = dirname(search_dir);

        // Check if we've reached the root
        if (strcmp(parent, search_dir) == 0 || strcmp(parent, "/") == 0) {
            // Reached root without finding BUILD.um
            break;
        }

        // For subsequent iterations, need to make a copy since dirname may modify
        char* parent_copy = rebuild_strdup(parent);
        if (search_dir != cwd) {
            rebuild_free(search_dir);
        }
        search_dir = parent_copy;
    }

    if (search_dir != cwd) {
        rebuild_free(search_dir);
    }
    rebuild_free(cwd);

    return build_file_path;
}

/**
 * Main entry point for rebuild CLI
 */
int main(int argc, char** argv) {
    int exit_code = 0;
    Storage* storage = NULL;
    ToolManager* tool_mgr = NULL;
    Scheduler* scheduler = NULL;
    void* umka = NULL;
    TargetRegistry* registry = NULL;
    char* build_file = NULL;
    char* target_name = NULL;

    // Parse command line arguments
    if (argc < 2) {
        fprintf(stderr, "Error: No target specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    // Handle options
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option: %s\n\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            // First non-option argument is the target
            if (!target_name) {
                target_name = argv[i];
            } else {
                fprintf(stderr, "Error: Multiple targets specified: %s and %s\n",
                        target_name, argv[i]);
                fprintf(stderr, "Only one target can be built at a time\n\n");
                print_usage(argv[0]);
                return 1;
            }
        }
    }

    if (!target_name) {
        fprintf(stderr, "Error: No target specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    LOG_INFO("Rebuild build system v%s", REBUILD_VERSION);
    LOG_INFO("Building target: %s", target_name);

    // Step 1: Initialize storage subsystem
    LOG_DEBUG("Initializing storage...");
    storage = storage_init();
    if (!storage) {
        LOG_ERROR("Failed to initialize storage subsystem");
        exit_code = REBUILD_ERROR_IO;
        goto cleanup;
    }
    LOG_INFO("Storage initialized at: %s", storage->base_dir);

    // Step 2: Initialize tool manager
    LOG_DEBUG("Initializing tool manager...");
    tool_mgr = tool_manager_create();
    if (!tool_mgr) {
        LOG_ERROR("Failed to initialize tool manager");
        exit_code = REBUILD_ERROR_MEMORY;
        goto cleanup;
    }
    LOG_DEBUG("Tool manager initialized");

    // Step 3: Initialize UMKA bridge
    LOG_DEBUG("Initializing UMKA bridge...");
    RebuildError err = umka_bridge_init();
    if (err != REBUILD_OK) {
        LOG_ERROR("Failed to initialize UMKA bridge");
        exit_code = err;
        goto cleanup;
    }
    LOG_DEBUG("UMKA bridge initialized");

    // Step 4: Create scheduler
    LOG_DEBUG("Creating scheduler...");
    scheduler = scheduler_create(storage);
    if (!scheduler) {
        LOG_ERROR("Failed to create scheduler");
        exit_code = REBUILD_ERROR_MEMORY;
        goto cleanup;
    }

    // Set tool manager in scheduler
    scheduler->tools = tool_mgr;
    LOG_DEBUG("Scheduler created");

    // Step 5: Find and load BUILD.um file
    LOG_DEBUG("Searching for BUILD.um...");
    build_file = find_build_file();
    if (!build_file) {
        LOG_ERROR("Could not find BUILD.um in current directory or any parent directory");
        LOG_ERROR("Please create a BUILD.um file to define your build targets");
        exit_code = REBUILD_ERROR_IO;
        goto cleanup;
    }

    LOG_INFO("Loading BUILD.um from: %s", build_file);
    umka = umka_load_script(build_file);
    if (!umka) {
        LOG_ERROR("Failed to load BUILD.um script");
        exit_code = REBUILD_ERROR_PARSE;
        goto cleanup;
    }

    // Store UMKA instance in scheduler for recipe execution
    scheduler->umka = umka;
    LOG_DEBUG("BUILD.um loaded successfully");

    // Create target registry and register targets
    registry = target_registry_create(umka);
    if (!registry) {
        LOG_ERROR("Failed to create target registry");
        exit_code = REBUILD_ERROR_MEMORY;
        goto cleanup;
    }

    // Call register_targets() function from BUILD.um
    // This populates the registry by calling rebuild_register_target() for each target
    UmkaFuncContext register_fn;

    // Get the register_targets function from the main module (NULL module name)
    // The BUILD.um was added with NULL as module name, making it the main module
    if (!umkaGetFunc(umka, NULL, "register_targets", &register_fn)) {
        LOG_ERROR("BUILD.um must define a register_targets() function");
        UmkaError* error = umkaGetError(umka);
        if (error && error->msg) {
            LOG_ERROR("UMKA error: %s", error->msg);
        }
        exit_code = REBUILD_ERROR_PARSE;
        goto cleanup;
    }

    LOG_DEBUG("Found register_targets() function");

    // Set global registry for FFI callbacks
    g_current_registry = registry;

    // Execute register_targets()
    if (umkaCall(umka, &register_fn) != 0) {
        UmkaError* error = umkaGetError(umka);
        LOG_ERROR("Error calling register_targets(): %s (line %d)", error->msg, error->line);
        exit_code = REBUILD_ERROR_EXEC;
        goto cleanup;
    }

    // Store registry in scheduler
    scheduler->registry = registry;
    LOG_INFO("Registered targets successfully");

    // Step 6: Validate that target exists
    // Note: For now, we rely on scheduler_build to validate the target.
    // In a more advanced implementation, we could scan the UMKA script
    // for target definitions here.

    // Step 7: Build the target
    LOG_INFO("Starting build...");
    err = scheduler_build(scheduler, target_name);
    if (err != REBUILD_OK) {
        LOG_ERROR("Failed to initiate build for target: %s", target_name);
        exit_code = err;
        goto cleanup;
    }

    // Step 8: Run the event loop
    LOG_DEBUG("Running scheduler event loop...");
    err = scheduler_run(scheduler);
    if (err != REBUILD_OK) {
        LOG_ERROR("Build failed for target: %s", target_name);
        if (scheduler->target_error) {
            LOG_ERROR("Failed target: %s", scheduler->target_error);
        }
        exit_code = err;
        goto cleanup;
    }

    // Build succeeded
    LOG_INFO("Build succeeded: %s", target_name);
    const char* output_path = scheduler_get_completed(scheduler, target_name);
    if (output_path) {
        LOG_INFO("Output available at: %s", output_path);
    }

cleanup:
    // Step 9: Cleanup all resources
    LOG_DEBUG("Cleaning up...");

    // Clear global registry pointer
    g_current_registry = NULL;

    if (build_file) {
        rebuild_free(build_file);
    }

    // Free UMKA instance if loaded
    // Note: UMKA cleanup is handled by scheduler_free

    // Free scheduler (this also frees recipes, registry, and internal structures)
    if (scheduler) {
        scheduler_free(scheduler);
    }

    // Free tool manager
    if (tool_mgr) {
        tool_manager_free(tool_mgr);
    }

    // Cleanup UMKA bridge
    umka_bridge_cleanup();

    // Free storage (last, as scheduler may need it during cleanup)
    if (storage) {
        storage_free(storage);
    }

    LOG_DEBUG("Cleanup complete");

    // Step 10: Exit with appropriate code
    if (exit_code == 0) {
        LOG_INFO("Rebuild completed successfully");
    } else {
        LOG_ERROR("Rebuild failed with error code: %d", exit_code);
    }

    return exit_code;
}
