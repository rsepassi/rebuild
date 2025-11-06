#include "string_utils.h"
#include <string.h>
#include <stdio.h>

char* concat(const char* a, const char* b, char* dest, int max_len) {
    if (dest == NULL || max_len <= 0) return NULL;

    int len_a = a ? strlen(a) : 0;
    int len_b = b ? strlen(b) : 0;

    if (len_a + len_b + 1 > max_len) {
        // Not enough space
        dest[0] = '\0';
        return dest;
    }

    dest[0] = '\0';
    if (a) strcat(dest, a);
    if (b) strcat(dest, b);

    return dest;
}
