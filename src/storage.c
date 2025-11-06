#define _GNU_SOURCE
#include "storage.h"
#include "hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

// Helper function to create a directory if it doesn't exist
// Returns true on success, false on error
static bool ensure_directory(const char* path) {
    struct stat st;

    // Check if directory already exists
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;  // Already exists and is a directory
        } else {
            LOG_ERROR("Path exists but is not a directory: %s", path);
            return false;
        }
    }

    // Create directory with rwxr-xr-x permissions
    if (mkdir(path, 0755) != 0) {
        if (errno != EEXIST) {
            LOG_ERROR("Failed to create directory %s: %s", path, strerror(errno));
            return false;
        }
        // EEXIST is okay, another process might have created it
    }

    return true;
}

// Helper function to create nested directories (like mkdir -p)
static bool ensure_directory_recursive(const char* path) {
    char* tmp = rebuild_strdup(path);
    char* p = NULL;
    size_t len = strlen(tmp);

    // Remove trailing slash if present
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    // Create parent directories
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (!ensure_directory(tmp)) {
                rebuild_free(tmp);
                return false;
            }
            *p = '/';
        }
    }

    // Create the final directory
    bool result = ensure_directory(tmp);
    rebuild_free(tmp);
    return result;
}

// Get XDG data home directory
static char* get_xdg_data_home(void) {
    const char* xdg_data_home = getenv("XDG_DATA_HOME");

    if (xdg_data_home && xdg_data_home[0] == '/') {
        // XDG_DATA_HOME is set and is an absolute path
        return rebuild_strdup(xdg_data_home);
    }

    // Fall back to ~/.local/share
    const char* home = getenv("HOME");
    if (!home) {
        LOG_ERROR("Neither XDG_DATA_HOME nor HOME environment variable is set");
        return NULL;
    }

    char* local_share = NULL;
    if (asprintf(&local_share, "%s/.local/share", home) < 0) {
        LOG_ERROR("Failed to allocate memory for XDG data home path");
        return NULL;
    }

    return local_share;
}

Storage* storage_init(void) {
    Storage* s = rebuild_malloc(sizeof(Storage));
    if (!s) {
        return NULL;
    }

    // Get XDG data home
    char* xdg_data_home = get_xdg_data_home();
    if (!xdg_data_home) {
        rebuild_free(s);
        return NULL;
    }

    // Create base directory path
    if (asprintf(&s->base_dir, "%s/rebuild", xdg_data_home) < 0) {
        LOG_ERROR("Failed to allocate memory for base directory path");
        rebuild_free(xdg_data_home);
        rebuild_free(s);
        return NULL;
    }
    rebuild_free(xdg_data_home);

    // Create subdirectory paths
    if (asprintf(&s->traces_dir, "%s/traces", s->base_dir) < 0 ||
        asprintf(&s->objects_dir, "%s/objects", s->base_dir) < 0 ||
        asprintf(&s->tmp_dir, "%s/tmp", s->base_dir) < 0) {
        LOG_ERROR("Failed to allocate memory for subdirectory paths");
        storage_free(s);
        return NULL;
    }

    // Create directory structure
    if (!ensure_directory_recursive(s->base_dir)) {
        LOG_ERROR("Failed to create base directory: %s", s->base_dir);
        storage_free(s);
        return NULL;
    }

    if (!ensure_directory(s->traces_dir)) {
        LOG_ERROR("Failed to create traces directory: %s", s->traces_dir);
        storage_free(s);
        return NULL;
    }

    if (!ensure_directory(s->objects_dir)) {
        LOG_ERROR("Failed to create objects directory: %s", s->objects_dir);
        storage_free(s);
        return NULL;
    }

    if (!ensure_directory(s->tmp_dir)) {
        LOG_ERROR("Failed to create tmp directory: %s", s->tmp_dir);
        storage_free(s);
        return NULL;
    }

    LOG_DEBUG("Storage initialized at: %s", s->base_dir);
    return s;
}

void storage_free(Storage* s) {
    if (!s) {
        return;
    }

    rebuild_free(s->base_dir);
    rebuild_free(s->traces_dir);
    rebuild_free(s->objects_dir);
    rebuild_free(s->tmp_dir);
    rebuild_free(s);
}

// Helper function to build a sharded path
// Given a hex string, returns a path like: base/ab/cdef0123...
static char* build_sharded_path(const char* base_dir, const char* hex_hash) {
    char* path = NULL;

    // First two characters are the first level directory
    char level1[3] = {hex_hash[0], hex_hash[1], '\0'};

    // Rest of the hash is the filename
    const char* filename = hex_hash + 2;

    // Build the path: base_dir/ab/cdef...
    if (asprintf(&path, "%s/%s/%s", base_dir, level1, filename) < 0) {
        LOG_ERROR("Failed to allocate memory for sharded path");
        return NULL;
    }

    return path;
}

// Helper function to ensure the parent directory of a sharded path exists
static bool ensure_shard_directory(const char* base_dir, const char* hex_hash) {
    char level1[3] = {hex_hash[0], hex_hash[1], '\0'};
    char* shard_dir = NULL;

    if (asprintf(&shard_dir, "%s/%s", base_dir, level1) < 0) {
        LOG_ERROR("Failed to allocate memory for shard directory path");
        return false;
    }

    bool result = ensure_directory(shard_dir);
    rebuild_free(shard_dir);
    return result;
}

char* storage_get_trace_path(Storage* s, const Hash* request_key) {
    if (!s || !request_key) {
        LOG_ERROR("Invalid arguments to storage_get_trace_path");
        return NULL;
    }

    // Convert hash to hex string
    char* hex = hash_to_hex(request_key);
    if (!hex) {
        return NULL;
    }

    // Build sharded path
    char* path = build_sharded_path(s->traces_dir, hex);
    if (!path) {
        rebuild_free(hex);
        return NULL;
    }

    // Ensure the shard directory exists
    if (!ensure_shard_directory(s->traces_dir, hex)) {
        rebuild_free(hex);
        rebuild_free(path);
        return NULL;
    }

    rebuild_free(hex);
    return path;
}

char* storage_get_object_path(Storage* s, const Hash* content_hash) {
    if (!s || !content_hash) {
        LOG_ERROR("Invalid arguments to storage_get_object_path");
        return NULL;
    }

    // Convert hash to hex string
    char* hex = hash_to_hex(content_hash);
    if (!hex) {
        return NULL;
    }

    // Build sharded path
    char* path = build_sharded_path(s->objects_dir, hex);
    if (!path) {
        rebuild_free(hex);
        return NULL;
    }

    // Ensure the shard directory exists
    if (!ensure_shard_directory(s->objects_dir, hex)) {
        rebuild_free(hex);
        rebuild_free(path);
        return NULL;
    }

    rebuild_free(hex);
    return path;
}

char* storage_get_tmp_dir(Storage* s, const char* target_name) {
    if (!s || !target_name) {
        LOG_ERROR("Invalid arguments to storage_get_tmp_dir");
        return NULL;
    }

    // Create a unique temporary directory name
    // Format: tmp/target_name_TIMESTAMP_PID
    pid_t pid = getpid();
    time_t now = time(NULL);

    char* tmp_path = NULL;
    if (asprintf(&tmp_path, "%s/%s_%ld_%d", s->tmp_dir, target_name,
                 (long)now, (int)pid) < 0) {
        LOG_ERROR("Failed to allocate memory for temporary directory path");
        return NULL;
    }

    // Create the directory
    if (!ensure_directory(tmp_path)) {
        LOG_ERROR("Failed to create temporary directory: %s", tmp_path);
        rebuild_free(tmp_path);
        return NULL;
    }

    return tmp_path;
}

bool storage_trace_exists(Storage* s, const Hash* request_key) {
    if (!s || !request_key) {
        LOG_ERROR("Invalid arguments to storage_trace_exists");
        return false;
    }

    // Get the trace path
    char* path = storage_get_trace_path(s, request_key);
    if (!path) {
        return false;
    }

    // Check if file exists
    struct stat st;
    bool exists = (stat(path, &st) == 0 && S_ISREG(st.st_mode));

    rebuild_free(path);
    return exists;
}

bool storage_object_exists(Storage* s, const Hash* content_hash) {
    if (!s || !content_hash) {
        LOG_ERROR("Invalid arguments to storage_object_exists");
        return false;
    }

    // Get the object path
    char* path = storage_get_object_path(s, content_hash);
    if (!path) {
        return false;
    }

    // Check if file exists (or directory for tree objects)
    struct stat st;
    bool exists = (stat(path, &st) == 0);

    rebuild_free(path);
    return exists;
}
