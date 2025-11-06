#include "set.h"
#include <string.h>

#define DEFAULT_SET_CAPACITY 16
#define SET_MAX_LOAD_FACTOR 0.75

// Simple hash function for strings (FNV-1a)
static uint32_t hash_string(const char* str) {
    uint32_t hash = 2166136261u;
    while (*str) {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

// Find entry for value (returns entry slot, may be empty)
static SetEntry* set_find_entry(SetEntry* entries, size_t capacity, const char* value) {
    uint32_t hash = hash_string(value);
    size_t index = hash % capacity;
    SetEntry* tombstone = NULL;

    // Linear probing
    for (size_t i = 0; i < capacity; i++) {
        size_t probe_index = (index + i) % capacity;
        SetEntry* entry = &entries[probe_index];

        if (!entry->occupied) {
            // Found empty slot
            if (!entry->value) {
                // Never used - return tombstone if we found one, else this
                return tombstone ? tombstone : entry;
            } else {
                // Tombstone (deleted entry)
                if (!tombstone) {
                    tombstone = entry;
                }
            }
        } else if (strcmp(entry->value, value) == 0) {
            // Found the value
            return entry;
        }
    }

    // Table is full, return tombstone if found
    return tombstone;
}

// Grow the set when load factor is too high
static RebuildError set_grow(Set* set) {
    size_t new_capacity = set->capacity ? set->capacity * 2 : DEFAULT_SET_CAPACITY;

    SetEntry* new_entries = rebuild_calloc(new_capacity, sizeof(SetEntry));
    if (!new_entries) {
        return REBUILD_ERROR_MEMORY;
    }

    // Rehash all entries
    size_t new_size = 0;
    for (size_t i = 0; i < set->capacity; i++) {
        SetEntry* entry = &set->entries[i];
        if (entry->occupied && entry->value) {
            SetEntry* dest = set_find_entry(new_entries, new_capacity, entry->value);
            dest->value = entry->value;  // Transfer ownership
            dest->occupied = true;
            new_size++;
        } else if (entry->value) {
            // Tombstone - free the value
            rebuild_free(entry->value);
        }
    }

    // Free old entries array
    if (set->entries) {
        rebuild_free(set->entries);
    }

    set->entries = new_entries;
    set->capacity = new_capacity;
    set->size = new_size;
    set->tombstones = 0;

    return REBUILD_OK;
}

Set* set_create(size_t initial_capacity) {
    Set* set = rebuild_malloc(sizeof(Set));
    if (!set) {
        return NULL;
    }

    if (initial_capacity == 0) {
        initial_capacity = DEFAULT_SET_CAPACITY;
    }

    // Round up to power of 2 for better performance
    size_t capacity = 1;
    while (capacity < initial_capacity) {
        capacity *= 2;
    }

    set->entries = rebuild_calloc(capacity, sizeof(SetEntry));
    if (!set->entries) {
        rebuild_free(set);
        return NULL;
    }

    set->capacity = capacity;
    set->size = 0;
    set->tombstones = 0;
    return set;
}

void set_free(Set* set) {
    if (!set) {
        return;
    }

    if (set->entries) {
        for (size_t i = 0; i < set->capacity; i++) {
            SetEntry* entry = &set->entries[i];
            if (entry->value) {
                rebuild_free(entry->value);
            }
        }
        rebuild_free(set->entries);
    }

    rebuild_free(set);
}

RebuildError set_add(Set* set, const char* value) {
    if (!set || !value) {
        return REBUILD_ERROR_MEMORY;
    }

    // Check if we need to grow
    if ((set->size + set->tombstones + 1) > (size_t)(set->capacity * SET_MAX_LOAD_FACTOR)) {
        RebuildError err = set_grow(set);
        if (err != REBUILD_OK) {
            return err;
        }
    }

    SetEntry* entry = set_find_entry(set->entries, set->capacity, value);
    if (!entry) {
        return REBUILD_ERROR_MEMORY;
    }

    if (entry->occupied) {
        // Value already exists
        return REBUILD_OK;
    }

    // New entry
    entry->value = rebuild_strdup(value);
    if (!entry->value) {
        return REBUILD_ERROR_MEMORY;
    }
    entry->occupied = true;
    set->size++;

    return REBUILD_OK;
}

bool set_has(const Set* set, const char* value) {
    if (!set || !value || set->size == 0) {
        return false;
    }

    SetEntry* entry = set_find_entry(set->entries, set->capacity, value);
    return entry && entry->occupied;
}

bool set_remove(Set* set, const char* value) {
    if (!set || !value || set->size == 0) {
        return false;
    }

    SetEntry* entry = set_find_entry(set->entries, set->capacity, value);
    if (!entry || !entry->occupied) {
        return false;
    }

    // Mark as tombstone (keep value allocated for probing)
    entry->occupied = false;
    set->size--;
    set->tombstones++;

    return true;
}

void set_iterate(const Set* set, SetIteratorFn fn, void* user_data) {
    if (!set || !fn) {
        return;
    }

    for (size_t i = 0; i < set->capacity; i++) {
        SetEntry* entry = &set->entries[i];
        if (entry->occupied && entry->value) {
            if (!fn(entry->value, user_data)) {
                break; // Stop early
            }
        }
    }
}

void set_clear(Set* set) {
    if (!set || !set->entries) {
        return;
    }

    for (size_t i = 0; i < set->capacity; i++) {
        SetEntry* entry = &set->entries[i];
        if (entry->value) {
            rebuild_free(entry->value);
            entry->value = NULL;
        }
        entry->occupied = false;
    }

    set->size = 0;
    set->tombstones = 0;
}

Set* set_copy(const Set* set) {
    if (!set) {
        return NULL;
    }

    Set* copy = set_create(set->capacity);
    if (!copy) {
        return NULL;
    }

    for (size_t i = 0; i < set->capacity; i++) {
        SetEntry* entry = &set->entries[i];
        if (entry->occupied && entry->value) {
            RebuildError err = set_add(copy, entry->value);
            if (err != REBUILD_OK) {
                set_free(copy);
                return NULL;
            }
        }
    }

    return copy;
}

RebuildError set_union(Set* set, const Set* other) {
    if (!set || !other) {
        return REBUILD_ERROR_MEMORY;
    }

    for (size_t i = 0; i < other->capacity; i++) {
        SetEntry* entry = &other->entries[i];
        if (entry->occupied && entry->value) {
            RebuildError err = set_add(set, entry->value);
            if (err != REBUILD_OK) {
                return err;
            }
        }
    }

    return REBUILD_OK;
}

bool set_contains_all(const Set* set, const Set* other) {
    if (!set || !other) {
        return false;
    }

    // Empty set is subset of any set
    if (other->size == 0) {
        return true;
    }

    // If other has more elements, it can't be a subset
    if (other->size > set->size) {
        return false;
    }

    for (size_t i = 0; i < other->capacity; i++) {
        SetEntry* entry = &other->entries[i];
        if (entry->occupied && entry->value) {
            if (!set_has(set, entry->value)) {
                return false;
            }
        }
    }

    return true;
}
