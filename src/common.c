#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

// Logging with timestamp and level
void rebuild_log(const char* level, const char* fmt, ...) {
    // Get current time
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    // Print timestamp and level
    fprintf(stderr, "[%s] %s: ", time_buf, level);

    // Print formatted message
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
    fflush(stderr);
}

// Memory allocation wrappers with error checking

void* rebuild_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    void* ptr = malloc(size);
    if (ptr == NULL) {
        LOG_ERROR("Memory allocation failed: malloc(%zu bytes)", size);
        exit(REBUILD_ERROR_MEMORY);
    }

    return ptr;
}

void* rebuild_calloc(size_t nmemb, size_t size) {
    if (nmemb == 0 || size == 0) {
        return NULL;
    }

    void* ptr = calloc(nmemb, size);
    if (ptr == NULL) {
        LOG_ERROR("Memory allocation failed: calloc(%zu * %zu bytes)", nmemb, size);
        exit(REBUILD_ERROR_MEMORY);
    }

    return ptr;
}

void* rebuild_realloc(void* ptr, size_t size) {
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    void* new_ptr = realloc(ptr, size);
    if (new_ptr == NULL) {
        LOG_ERROR("Memory reallocation failed: realloc(%zu bytes)", size);
        exit(REBUILD_ERROR_MEMORY);
    }

    return new_ptr;
}

char* rebuild_strdup(const char* s) {
    if (s == NULL) {
        return NULL;
    }

    size_t len = strlen(s) + 1;
    char* new_str = rebuild_malloc(len);
    memcpy(new_str, s, len);

    return new_str;
}

void rebuild_free(void* ptr) {
    free(ptr);
}
