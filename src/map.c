#include "map.h"
#include <string.h>

#define DEFAULT_MAP_CAPACITY 16
#define MAP_MAX_LOAD_FACTOR 0.75

// Simple hash function for strings (FNV-1a)
static uint32_t hash_string(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

// Find entry for key (returns entry slot, may be empty)
static MapEntry* map_find_entry(MapEntry* entries, size_t capacity, const char* key) {
    uint32_t hash = hash_string(key);
    size_t index = hash % capacity;
    MapEntry* tombstone = NULL;

    // Linear probing
    for (size_t i = 0; i < capacity; i++) {
        size_t probe_index = (index + i) % capacity;
        MapEntry* entry = &entries[probe_index];

        if (!entry->occupied) {
            // Found empty slot
            if (!entry->key) {
                // Never used - return tombstone if we found one, else this
                return tombstone ? tombstone : entry;
            } else {
                // Tombstone (deleted entry)
                if (!tombstone) {
                    tombstone = entry;
                }
            }
        } else if (strcmp(entry->key, key) == 0) {
            // Found the key
            return entry;
        }
    }

    // Table is full, return tombstone if found
    return tombstone;
}

// Grow the map when load factor is too high
static RebuildError map_grow(Map* map) {
    size_t new_capacity = map->capacity ? map->capacity * 2 : DEFAULT_MAP_CAPACITY;

    MapEntry* new_entries = rebuild_calloc(new_capacity, sizeof(MapEntry));
    if (!new_entries) {
        return REBUILD_ERROR_MEMORY;
    }

    // Rehash all entries
    size_t new_size = 0;
    for (size_t i = 0; i < map->capacity; i++) {
        MapEntry* entry = &map->entries[i];
        if (entry->occupied && entry->key) {
            MapEntry* dest = map_find_entry(new_entries, new_capacity, entry->key);
            dest->key = entry->key;  // Transfer ownership
            dest->value = entry->value;
            dest->occupied = true;
            new_size++;
        } else if (entry->key) {
            // Tombstone - free the key
            rebuild_free(entry->key);
        }
    }

    // Free old entries array
    if (map->entries) {
        rebuild_free(map->entries);
    }

    map->entries = new_entries;
    map->capacity = new_capacity;
    map->size = new_size;
    map->tombstones = 0;

    return REBUILD_OK;
}

Map* map_create(size_t initial_capacity) {
    Map* map = rebuild_malloc(sizeof(Map));
    if (!map) {
        return NULL;
    }

    if (initial_capacity == 0) {
        initial_capacity = DEFAULT_MAP_CAPACITY;
    }

    // Round up to power of 2 for better performance
    size_t capacity = 1;
    while (capacity < initial_capacity) {
        capacity *= 2;
    }

    map->entries = rebuild_calloc(capacity, sizeof(MapEntry));
    if (!map->entries) {
        rebuild_free(map);
        return NULL;
    }

    map->capacity = capacity;
    map->size = 0;
    map->tombstones = 0;
    return map;
}

void map_free(Map* map, MapValueFreeFn value_free_fn) {
    if (!map) {
        return;
    }

    if (map->entries) {
        for (size_t i = 0; i < map->capacity; i++) {
            MapEntry* entry = &map->entries[i];
            if (entry->key) {
                rebuild_free(entry->key);
            }
            if (entry->occupied && value_free_fn && entry->value) {
                value_free_fn(entry->value);
            }
        }
        rebuild_free(map->entries);
    }

    rebuild_free(map);
}

RebuildError map_set(Map* map, const char* key, void* value) {
    if (!map || !key) {
        return REBUILD_ERROR_MEMORY;
    }

    // Check if we need to grow
    if ((map->size + map->tombstones + 1) > (size_t)(map->capacity * MAP_MAX_LOAD_FACTOR)) {
        RebuildError err = map_grow(map);
        if (err != REBUILD_OK) {
            return err;
        }
    }

    MapEntry* entry = map_find_entry(map->entries, map->capacity, key);
    if (!entry) {
        return REBUILD_ERROR_MEMORY;
    }

    bool is_new_key = !entry->occupied;

    if (is_new_key) {
        // New entry
        entry->key = rebuild_strdup(key);
        if (!entry->key) {
            return REBUILD_ERROR_MEMORY;
        }
        entry->occupied = true;
        map->size++;
    }

    entry->value = value;
    return REBUILD_OK;
}

void* map_get(const Map* map, const char* key) {
    if (!map || !key || map->size == 0) {
        return NULL;
    }

    MapEntry* entry = map_find_entry(map->entries, map->capacity, key);
    if (entry && entry->occupied) {
        return entry->value;
    }

    return NULL;
}

bool map_has(const Map* map, const char* key) {
    if (!map || !key || map->size == 0) {
        return false;
    }

    MapEntry* entry = map_find_entry(map->entries, map->capacity, key);
    return entry && entry->occupied;
}

void* map_remove(Map* map, const char* key) {
    if (!map || !key || map->size == 0) {
        return NULL;
    }

    MapEntry* entry = map_find_entry(map->entries, map->capacity, key);
    if (!entry || !entry->occupied) {
        return NULL;
    }

    void* old_value = entry->value;

    // Mark as tombstone (keep key allocated for probing)
    entry->occupied = false;
    entry->value = NULL;
    map->size--;
    map->tombstones++;

    return old_value;
}

void map_iterate(const Map* map, MapIteratorFn fn, void* user_data) {
    if (!map || !fn) {
        return;
    }

    for (size_t i = 0; i < map->capacity; i++) {
        MapEntry* entry = &map->entries[i];
        if (entry->occupied && entry->key) {
            if (!fn(entry->key, entry->value, user_data)) {
                break; // Stop early
            }
        }
    }
}

void map_clear(Map* map, MapValueFreeFn value_free_fn) {
    if (!map || !map->entries) {
        return;
    }

    for (size_t i = 0; i < map->capacity; i++) {
        MapEntry* entry = &map->entries[i];
        if (entry->key) {
            rebuild_free(entry->key);
            entry->key = NULL;
        }
        if (entry->occupied && value_free_fn && entry->value) {
            value_free_fn(entry->value);
        }
        entry->value = NULL;
        entry->occupied = false;
    }

    map->size = 0;
    map->tombstones = 0;
}
