#ifndef REBUILD_BUFFER_H
#define REBUILD_BUFFER_H

#include "common.h"

// Dynamic buffer for byte/string accumulation
struct Buffer {
    char* data;
    size_t size;      // Current used size
    size_t capacity;  // Allocated capacity
};

// Create a new buffer with initial capacity (0 = default 64 bytes)
Buffer* buffer_create(size_t initial_capacity);

// Free buffer and its data
void buffer_free(Buffer* buf);

// Append raw data to buffer
RebuildError buffer_append(Buffer* buf, const void* data, size_t len);

// Append null-terminated string to buffer
RebuildError buffer_append_str(Buffer* buf, const char* str);

// Append a single character
RebuildError buffer_append_char(Buffer* buf, char c);

// Clear buffer (reset size to 0, keep capacity)
void buffer_clear(Buffer* buf);

// Convert buffer to null-terminated string (caller must free)
// Returns NULL on allocation failure
char* buffer_to_string(const Buffer* buf);

// Get current size
static inline size_t buffer_size(const Buffer* buf) {
    return buf ? buf->size : 0;
}

// Get current capacity
static inline size_t buffer_capacity(const Buffer* buf) {
    return buf ? buf->capacity : 0;
}

// Get raw data pointer (not null-terminated unless explicitly added)
static inline const char* buffer_data(const Buffer* buf) {
    return buf ? buf->data : NULL;
}

#endif // REBUILD_BUFFER_H
