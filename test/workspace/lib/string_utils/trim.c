#include "string_utils.h"
#include <ctype.h>
#include <string.h>

void trim(char* str) {
    if (str == NULL) return;

    // Trim leading whitespace
    char* start = str;
    while (*start && isspace(*start)) {
        start++;
    }

    // Trim trailing whitespace
    char* end = start + strlen(start) - 1;
    while (end > start && isspace(*end)) {
        end--;
    }

    // Copy trimmed string back
    int len = end - start + 1;
    if (start != str) {
        memmove(str, start, len);
    }
    str[len] = '\0';
}
