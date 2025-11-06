#ifndef REBUILD_MAP_H
#define REBUILD_MAP_H

#include "common.h"

// Hash map with string keys and void* values
// Uses open addressing with linear probing

typedef struct MapEntry {
    char* key;
    void* value;
    bool occupied;
} MapEntry;

struct Map {
    MapEntry* entries;
    size_t capacity;
    size_t size;      // Number of occupied entries
    size_t tombstones; // Number of deleted entries
};

// Callback for map iteration
// Return false to stop iteration early
typedef bool (*MapIteratorFn)(const char* key, void* value, void* user_data);

// Callback for freeing values when map is destroyed
typedef void (*MapValueFreeFn)(void* value);

// Create a new map with initial capacity (0 = default 16)
Map* map_create(size_t initial_capacity);

// Free map and optionally free values
// If value_free_fn is NULL, values are not freed
void map_free(Map* map, MapValueFreeFn value_free_fn);

// Set a key-value pair
// Returns REBUILD_OK on success
// If key exists, old value is replaced (not freed - caller's responsibility)
RebuildError map_set(Map* map, const char* key, void* value);

// Get value for key
// Returns NULL if key not found (also returns NULL for NULL values)
void* map_get(const Map* map, const char* key);

// Check if key exists
bool map_has(const Map* map, const char* key);

// Remove key from map
// Returns the old value (NULL if not found)
// Caller is responsible for freeing the value if needed
void* map_remove(Map* map, const char* key);

// Iterate over all entries
// Calls fn for each key-value pair
// If fn returns false, iteration stops early
void map_iterate(const Map* map, MapIteratorFn fn, void* user_data);

// Get current size
static inline size_t map_size(const Map* map) {
    return map ? map->size : 0;
}

// Get current capacity
static inline size_t map_capacity(const Map* map) {
    return map ? map->capacity : 0;
}

// Clear all entries (does not free values)
void map_clear(Map* map, MapValueFreeFn value_free_fn);

#endif // REBUILD_MAP_H
