#ifndef REBUILD_COMMON_H
#define REBUILD_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// BLAKE2b 256-bit hash
typedef struct {
    uint8_t bytes[32];
} Hash;

// Forward declarations
typedef struct Buffer Buffer;
typedef struct Map Map;
typedef struct Set Set;
typedef struct Trace Trace;
typedef struct Recipe Recipe;
typedef struct Scheduler Scheduler;
typedef struct ToolModule ToolModule;
typedef struct Storage Storage;

// Common utility macros
#define REBUILD_VERSION "0.1.0"
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// Error codes
typedef enum {
    REBUILD_OK = 0,
    REBUILD_ERROR_IO = 1,
    REBUILD_ERROR_MEMORY = 2,
    REBUILD_ERROR_PARSE = 3,
    REBUILD_ERROR_EXEC = 4,
    REBUILD_ERROR_HASH = 5,
    REBUILD_ERROR_TRACE = 6,
} RebuildError;

// Logging
void rebuild_log(const char* level, const char* fmt, ...);
#define LOG_DEBUG(...) rebuild_log("DEBUG", __VA_ARGS__)
#define LOG_INFO(...) rebuild_log("INFO", __VA_ARGS__)
#define LOG_WARN(...) rebuild_log("WARN", __VA_ARGS__)
#define LOG_ERROR(...) rebuild_log("ERROR", __VA_ARGS__)

// Memory allocation wrappers (with error checking)
void* rebuild_malloc(size_t size);
void* rebuild_calloc(size_t nmemb, size_t size);
void* rebuild_realloc(void* ptr, size_t size);
char* rebuild_strdup(const char* s);
void rebuild_free(void* ptr);

#endif // REBUILD_COMMON_H
