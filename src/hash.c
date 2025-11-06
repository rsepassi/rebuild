#include "hash.h"
#include "../vendor/blake2/blake2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

// Compare two hashes for equality
bool hash_equal(const Hash* a, const Hash* b) {
    if (a == NULL || b == NULL) {
        return false;
    }
    return memcmp(a->bytes, b->bytes, sizeof(a->bytes)) == 0;
}

// Convert hash to hexadecimal string
char* hash_to_hex(const Hash* h) {
    if (h == NULL) {
        return NULL;
    }

    // 32 bytes = 64 hex characters + null terminator
    char* hex = rebuild_malloc(65);

    for (int i = 0; i < 32; i++) {
        sprintf(&hex[i * 2], "%02x", h->bytes[i]);
    }
    hex[64] = '\0';

    return hex;
}

// Parse hexadecimal string into hash
bool hash_from_hex(const char* hex, Hash* out) {
    if (hex == NULL || out == NULL) {
        return false;
    }

    // Check length (should be 64 hex characters)
    size_t len = strlen(hex);
    if (len != 64) {
        return false;
    }

    // Parse each pair of hex digits
    for (int i = 0; i < 32; i++) {
        char byte_str[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};

        // Check if both characters are valid hex digits
        if (!isxdigit((unsigned char)byte_str[0]) ||
            !isxdigit((unsigned char)byte_str[1])) {
            return false;
        }

        // Convert to byte
        char* endptr;
        unsigned long val = strtoul(byte_str, &endptr, 16);
        if (*endptr != '\0') {
            return false;
        }

        out->bytes[i] = (uint8_t)val;
    }

    return true;
}

// Combine two hashes by XORing them
void hash_combine(Hash* dest, const Hash* src) {
    if (dest == NULL || src == NULL) {
        return;
    }

    for (int i = 0; i < 32; i++) {
        dest->bytes[i] ^= src->bytes[i];
    }
}

// Hash file contents using BLAKE2b
bool hash_file(const char* path, Hash* out) {
    if (path == NULL || out == NULL) {
        return false;
    }

    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        LOG_WARN("Failed to open file for hashing: %s", path);
        return false;
    }

    // Initialize BLAKE2b state for 32-byte output
    blake2b_state state;
    if (blake2b_init(&state, 32) != 0) {
        fclose(file);
        LOG_ERROR("Failed to initialize BLAKE2b");
        return false;
    }

    // Read and hash file in chunks
    unsigned char buffer[8192];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (blake2b_update(&state, buffer, bytes_read) != 0) {
            fclose(file);
            LOG_ERROR("Failed to update BLAKE2b hash");
            return false;
        }
    }

    // Check for read errors
    if (ferror(file)) {
        fclose(file);
        LOG_WARN("Error reading file: %s", path);
        return false;
    }

    fclose(file);

    // Finalize hash
    if (blake2b_final(&state, out->bytes, 32) != 0) {
        LOG_ERROR("Failed to finalize BLAKE2b hash");
        return false;
    }

    return true;
}

// Hash arbitrary data using BLAKE2b
void hash_data(const void* data, size_t len, Hash* out) {
    if (data == NULL || out == NULL) {
        return;
    }

    // Use the simple BLAKE2b API for one-shot hashing
    // blake2b(output, output_len, input, input_len, key, key_len)
    if (blake2b(out->bytes, 32, data, len, NULL, 0) != 0) {
        LOG_ERROR("Failed to hash data with BLAKE2b");
        memset(out->bytes, 0, sizeof(out->bytes));
    }
}

// Helper function to compare directory entries for qsort
static int compare_dirent_names(const void* a, const void* b) {
    const struct dirent** da = (const struct dirent**)a;
    const struct dirent** db = (const struct dirent**)b;
    return strcmp((*da)->d_name, (*db)->d_name);
}

// Hash a directory tree recursively
bool hash_tree(const char* path, Hash* out) {
    if (path == NULL || out == NULL) {
        return false;
    }

    // Check if path exists and get its type
    struct stat st;
    if (stat(path, &st) != 0) {
        LOG_WARN("Failed to stat path: %s", path);
        return false;
    }

    // If it's a regular file, just hash the file
    if (S_ISREG(st.st_mode)) {
        return hash_file(path, out);
    }

    // If it's not a directory, we can't hash it
    if (!S_ISDIR(st.st_mode)) {
        LOG_WARN("Path is neither file nor directory: %s", path);
        return false;
    }

    // Open directory
    DIR* dir = opendir(path);
    if (dir == NULL) {
        LOG_WARN("Failed to open directory: %s", path);
        return false;
    }

    // Read all directory entries (excluding . and ..)
    struct dirent** entries = NULL;
    int entry_count = 0;
    int entry_capacity = 64;
    entries = rebuild_malloc(sizeof(struct dirent*) * entry_capacity);

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Resize array if needed
        if (entry_count >= entry_capacity) {
            entry_capacity *= 2;
            entries = rebuild_realloc(entries, sizeof(struct dirent*) * entry_capacity);
        }

        // Copy directory entry
        entries[entry_count] = rebuild_malloc(sizeof(struct dirent));
        memcpy(entries[entry_count], entry, sizeof(struct dirent));
        entry_count++;
    }

    closedir(dir);

    // Sort entries by name for deterministic ordering
    if (entry_count > 0) {
        qsort(entries, entry_count, sizeof(struct dirent*), compare_dirent_names);
    }

    // Initialize result hash to zero
    memset(out->bytes, 0, sizeof(out->bytes));

    // Hash each entry
    for (int i = 0; i < entry_count; i++) {
        // Build full path
        size_t path_len = strlen(path) + strlen(entries[i]->d_name) + 2;
        char* full_path = rebuild_malloc(path_len);
        snprintf(full_path, path_len, "%s/%s", path, entries[i]->d_name);

        // Hash the entry name first (for directory structure)
        Hash name_hash;
        hash_data(entries[i]->d_name, strlen(entries[i]->d_name), &name_hash);
        hash_combine(out, &name_hash);

        // Hash the entry contents (recursively for directories)
        Hash entry_hash;
        if (hash_tree(full_path, &entry_hash)) {
            hash_combine(out, &entry_hash);
        } else {
            LOG_DEBUG("Skipping unhashable entry: %s", full_path);
        }

        rebuild_free(full_path);
        rebuild_free(entries[i]);
    }

    rebuild_free(entries);

    return true;
}
