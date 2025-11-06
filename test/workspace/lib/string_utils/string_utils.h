#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include "common.h"

// String utility functions
void trim(char* str);
char* concat(const char* a, const char* b, char* dest, int max_len);

#endif // STRING_UTILS_H
