#include "math.h"
#include <stdio.h>

int add(int a, int b) {
    #if DEBUG_MODE
    printf("DEBUG: add(%d, %d)\n", a, b);
    #endif
    return a + b;
}
