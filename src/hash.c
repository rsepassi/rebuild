#include "hash.h"
#include "../vendor/blake2/blake2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
