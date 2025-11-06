#ifndef REBUILD_TOOL_H
#define REBUILD_TOOL_H

#include "common.h"

// Tool module - represents a build tool with its API
// The tool's UMKA API module code is hashed as part of the request key
struct ToolModule {
    char* name;           // Tool name (e.g., "clang", "ar")
    char* binary_path;    // Absolute path to tool binary
    char* module_path;    // Path to UMKA API module (e.g., "tools/clang.um")
    Hash binary_hash;     // Hash of the tool binary
    Hash module_hash;     // Hash of the UMKA API module source
};

// Tool manager - maintains loaded tools
typedef struct ToolManager {
    Map* tools;           // name -> ToolModule*
    char** search_paths;  // PATH directories to search
    size_t path_count;    // Number of PATH directories
} ToolManager;

// Create a new tool manager
// Initializes with system PATH if available
ToolManager* tool_manager_create(void);

// Free tool manager and all loaded tools
void tool_manager_free(ToolManager* mgr);

// Find a tool binary in PATH
// Returns newly allocated path string, or NULL if not found
// Caller must free the returned string
char* tool_manager_find_tool(ToolManager* mgr, const char* name);

// Load a tool and compute hashes
// This finds the tool binary, hashes it, and locates/hashes the UMKA module
// Returns NULL if tool cannot be found or loaded
// The returned ToolModule is owned by the manager (don't free it directly)
ToolModule* tool_manager_load_tool(ToolManager* mgr, const char* name);

// Get a previously loaded tool
// Returns NULL if tool not loaded
ToolModule* tool_manager_get_tool(ToolManager* mgr, const char* name);

// Free a single tool module
void tool_module_free(ToolModule* tool);

#endif // REBUILD_TOOL_H
