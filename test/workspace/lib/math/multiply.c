#include "math.h"
#include <stdio.h>

int multiply(int a, int b) {
    #if DEBUG_MODE
    printf("DEBUG: multiply(%d, %d)\n", a, b);
    #endif
    return a * b;
}
