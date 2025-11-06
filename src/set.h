#ifndef REBUILD_SET_H
#define REBUILD_SET_H

#include "common.h"

// Hash set for string values
// Uses open addressing with linear probing

typedef struct SetEntry {
    char* value;
    bool occupied;
} SetEntry;

struct Set {
    SetEntry* entries;
    size_t capacity;
    size_t size;       // Number of occupied entries
    size_t tombstones; // Number of deleted entries
};

// Callback for set iteration
// Return false to stop iteration early
typedef bool (*SetIteratorFn)(const char* value, void* user_data);

// Create a new set with initial capacity (0 = default 16)
Set* set_create(size_t initial_capacity);

// Free set and its data
void set_free(Set* set);

// Add a value to the set
// Returns REBUILD_OK on success
// If value already exists, this is a no-op
RebuildError set_add(Set* set, const char* value);

// Check if value exists in set
bool set_has(const Set* set, const char* value);

// Remove value from set
// Returns true if value was present and removed, false otherwise
bool set_remove(Set* set, const char* value);

// Iterate over all entries
// Calls fn for each value
// If fn returns false, iteration stops early
void set_iterate(const Set* set, SetIteratorFn fn, void* user_data);

// Get current size
static inline size_t set_size(const Set* set) {
    return set ? set->size : 0;
}

// Get current capacity
static inline size_t set_capacity(const Set* set) {
    return set ? set->capacity : 0;
}

// Clear all entries
void set_clear(Set* set);

// Copy a set (deep copy)
Set* set_copy(const Set* set);

// Union: add all elements from other to set
RebuildError set_union(Set* set, const Set* other);

// Check if set contains all elements from other
bool set_contains_all(const Set* set, const Set* other);

#endif // REBUILD_SET_H
