#include "buffer.h"
#include <string.h>

#define DEFAULT_BUFFER_CAPACITY 64

// Ensure buffer has at least min_capacity
static RebuildError buffer_ensure_capacity(Buffer* buf, size_t min_capacity) {
    if (!buf) {
        return REBUILD_ERROR_MEMORY;
    }

    if (buf->capacity >= min_capacity) {
        return REBUILD_OK;
    }

    // Grow by 1.5x or to min_capacity, whichever is larger
    size_t new_capacity = buf->capacity ? buf->capacity : DEFAULT_BUFFER_CAPACITY;
    while (new_capacity < min_capacity) {
        new_capacity = new_capacity + (new_capacity >> 1); // capacity * 1.5
    }

    char* new_data = rebuild_realloc(buf->data, new_capacity);
    if (!new_data) {
        return REBUILD_ERROR_MEMORY;
    }

    buf->data = new_data;
    buf->capacity = new_capacity;
    return REBUILD_OK;
}

Buffer* buffer_create(size_t initial_capacity) {
    Buffer* buf = rebuild_malloc(sizeof(Buffer));
    if (!buf) {
        return NULL;
    }

    if (initial_capacity == 0) {
        initial_capacity = DEFAULT_BUFFER_CAPACITY;
    }

    buf->data = rebuild_malloc(initial_capacity);
    if (!buf->data) {
        rebuild_free(buf);
        return NULL;
    }

    buf->size = 0;
    buf->capacity = initial_capacity;
    return buf;
}

void buffer_free(Buffer* buf) {
    if (!buf) {
        return;
    }

    if (buf->data) {
        rebuild_free(buf->data);
    }
    rebuild_free(buf);
}

RebuildError buffer_append(Buffer* buf, const void* data, size_t len) {
    if (!buf || !data) {
        return REBUILD_ERROR_MEMORY;
    }

    if (len == 0) {
        return REBUILD_OK;
    }

    // Ensure we have space
    RebuildError err = buffer_ensure_capacity(buf, buf->size + len);
    if (err != REBUILD_OK) {
        return err;
    }

    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    return REBUILD_OK;
}

RebuildError buffer_append_str(Buffer* buf, const char* str) {
    if (!str) {
        return REBUILD_ERROR_MEMORY;
    }

    return buffer_append(buf, str, strlen(str));
}

RebuildError buffer_append_char(Buffer* buf, char c) {
    return buffer_append(buf, &c, 1);
}

void buffer_clear(Buffer* buf) {
    if (buf) {
        buf->size = 0;
    }
}

char* buffer_to_string(const Buffer* buf) {
    if (!buf) {
        return NULL;
    }

    // Allocate space for data + null terminator
    char* str = rebuild_malloc(buf->size + 1);
    if (!str) {
        return NULL;
    }

    if (buf->size > 0) {
        memcpy(str, buf->data, buf->size);
    }
    str[buf->size] = '\0';
    return str;
}
