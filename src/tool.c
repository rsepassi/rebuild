#include "tool.h"
#include "map.h"
#include "hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

// Helper: Check if a file exists and is executable
static bool is_executable(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }

    // Check if it's a regular file and executable
    return S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR);
}

// Helper: Parse PATH environment variable
static char** parse_path_env(size_t* out_count) {
    const char* path_env = getenv("PATH");
    if (!path_env) {
        *out_count = 0;
        return NULL;
    }

    // Count number of paths (separated by ':')
    size_t count = 1;
    for (const char* p = path_env; *p; p++) {
        if (*p == ':') {
            count++;
        }
    }

    // Allocate array for paths
    char** paths = rebuild_calloc(count, sizeof(char*));

    // Copy and tokenize
    char* path_copy = rebuild_strdup(path_env);
    char* token = strtok(path_copy, ":");
    size_t i = 0;

    while (token && i < count) {
        paths[i++] = rebuild_strdup(token);
        token = strtok(NULL, ":");
    }

    rebuild_free(path_copy);
    *out_count = i;

    return paths;
}

// Helper: Construct UMKA module path from tool name
static char* get_module_path(const char* tool_name) {
    // Module path is tools/<name>.um
    size_t len = strlen("tools/") + strlen(tool_name) + strlen(".um") + 1;
    char* module_path = rebuild_malloc(len);
    snprintf(module_path, len, "tools/%s.um", tool_name);
    return module_path;
}

// Create a new tool manager
ToolManager* tool_manager_create(void) {
    ToolManager* mgr = rebuild_malloc(sizeof(ToolManager));

    mgr->tools = map_create(16);
    mgr->search_paths = parse_path_env(&mgr->path_count);

    LOG_DEBUG("Tool manager created with %zu PATH directories", mgr->path_count);

    return mgr;
}

// Free tool manager and all loaded tools
void tool_manager_free(ToolManager* mgr) {
    if (!mgr) {
        return;
    }

    // Free all loaded tools
    map_free(mgr->tools, (MapValueFreeFn)tool_module_free);

    // Free search paths
    for (size_t i = 0; i < mgr->path_count; i++) {
        rebuild_free(mgr->search_paths[i]);
    }
    rebuild_free(mgr->search_paths);

    rebuild_free(mgr);
}

// Find a tool binary in PATH
char* tool_manager_find_tool(ToolManager* mgr, const char* name) {
    if (!mgr || !name) {
        return NULL;
    }

    // If name contains a slash, treat it as an absolute/relative path
    if (strchr(name, '/')) {
        if (is_executable(name)) {
            return rebuild_strdup(name);
        }
        return NULL;
    }

    // Search in PATH directories
    for (size_t i = 0; i < mgr->path_count; i++) {
        size_t path_len = strlen(mgr->search_paths[i]) + strlen(name) + 2;
        char* full_path = rebuild_malloc(path_len);
        snprintf(full_path, path_len, "%s/%s", mgr->search_paths[i], name);

        if (is_executable(full_path)) {
            LOG_DEBUG("Found tool '%s' at %s", name, full_path);
            return full_path;
        }

        rebuild_free(full_path);
    }

    LOG_WARN("Tool '%s' not found in PATH", name);
    return NULL;
}

// Load a tool and compute hashes
ToolModule* tool_manager_load_tool(ToolManager* mgr, const char* name) {
    if (!mgr || !name) {
        return NULL;
    }

    // Check if already loaded
    ToolModule* existing = map_get(mgr->tools, name);
    if (existing) {
        LOG_DEBUG("Tool '%s' already loaded", name);
        return existing;
    }

    // Find the tool binary
    char* binary_path = tool_manager_find_tool(mgr, name);
    if (!binary_path) {
        LOG_ERROR("Failed to find tool binary: %s", name);
        return NULL;
    }

    // Create tool module
    ToolModule* tool = rebuild_calloc(1, sizeof(ToolModule));
    tool->name = rebuild_strdup(name);
    tool->binary_path = binary_path;

    // Hash the binary
    if (!hash_file(binary_path, &tool->binary_hash)) {
        LOG_ERROR("Failed to hash tool binary: %s", binary_path);
        tool_module_free(tool);
        return NULL;
    }

    // Get module path
    tool->module_path = get_module_path(name);

    // Hash the module (if it exists)
    // It's OK if the module doesn't exist - tool might not have a UMKA API
    if (access(tool->module_path, R_OK) == 0) {
        if (!hash_file(tool->module_path, &tool->module_hash)) {
            LOG_WARN("Failed to hash tool module: %s", tool->module_path);
            // Continue anyway - hash will be zero
        } else {
            LOG_DEBUG("Hashed tool module: %s", tool->module_path);
        }
    } else {
        LOG_DEBUG("No UMKA module found for tool '%s' (expected at %s)",
                  name, tool->module_path);
        // Module hash remains zero-initialized
    }

    // Store in map
    map_set(mgr->tools, name, tool);

    LOG_INFO("Loaded tool '%s' from %s", name, binary_path);

    return tool;
}

// Get a previously loaded tool
ToolModule* tool_manager_get_tool(ToolManager* mgr, const char* name) {
    if (!mgr || !name) {
        return NULL;
    }

    return (ToolModule*)map_get(mgr->tools, name);
}

// Free a single tool module
void tool_module_free(ToolModule* tool) {
    if (!tool) {
        return;
    }

    rebuild_free(tool->name);
    rebuild_free(tool->binary_path);
    rebuild_free(tool->module_path);
    rebuild_free(tool);
}
