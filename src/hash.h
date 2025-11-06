#ifndef REBUILD_HASH_H
#define REBUILD_HASH_H

#include "common.h"
#include <stdbool.h>
#include <stddef.h>

// Hash comparison - returns true if hashes are equal
bool hash_equal(const Hash* a, const Hash* b);

// Convert hash to hex string (64 characters + null terminator)
// Caller must free the returned string
char* hash_to_hex(const Hash* h);

// Parse hex string into hash
// Returns true on success, false if hex string is invalid
bool hash_from_hex(const char* hex, Hash* out);

// Combine two hashes (XOR operation for simple combining)
// Result is stored in dest
void hash_combine(Hash* dest, const Hash* src);

// Hash a file's contents
// Returns true on success, false on I/O error
bool hash_file(const char* path, Hash* out);

// Hash arbitrary data
void hash_data(const void* data, size_t len, Hash* out);

// Hash a directory tree recursively
// Computes a hash that includes all file contents and directory structure
// Returns true on success, false on I/O error
bool hash_tree(const char* path, Hash* out);

#endif // REBUILD_HASH_H
