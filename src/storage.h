#ifndef REBUILD_STORAGE_H
#define REBUILD_STORAGE_H

#include "common.h"
#include <stdbool.h>

// Storage manages the XDG-based file storage for Rebuild
// Provides content-addressed storage for traces and objects with 2-level sharding
typedef struct Storage {
    char* base_dir;      // Base directory (XDG_DATA_HOME/rebuild or ~/.local/share/rebuild)
    char* traces_dir;    // traces/ - stores trace files by request key
    char* objects_dir;   // objects/ - stores outputs by content hash
    char* tmp_dir;       // tmp/ - temporary build directories
} Storage;

// Initialize storage with XDG directories
// Creates base directory structure and subdirectories
// Returns NULL on error
Storage* storage_init(void);

// Free storage resources
void storage_free(Storage* s);

// Get path for a trace file given its request key
// Returns path like: traces/ab/cdef0123...
// Caller must free the returned string
char* storage_get_trace_path(Storage* s, const Hash* request_key);

// Get path for an object file given its content hash
// Returns path like: objects/12/3456789a...
// Caller must free the returned string
char* storage_get_object_path(Storage* s, const Hash* content_hash);

// Get a unique temporary directory for a build target
// Returns path like: tmp/target_name_XXXXXX
// Creates the directory if it doesn't exist
// Caller must free the returned string
char* storage_get_tmp_dir(Storage* s, const char* target_name);

// Check if a trace exists for the given request key
bool storage_trace_exists(Storage* s, const Hash* request_key);

// Check if an object exists for the given content hash
bool storage_object_exists(Storage* s, const Hash* content_hash);

#endif // REBUILD_STORAGE_H
