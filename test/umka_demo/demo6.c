/**
 * demo6 - Try different module names
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "umka_api.h"

void print_message(UmkaStackSlot *params, UmkaStackSlot *result) {
    const char* msg = (const char*)umkaGetParam(params, 0)->ptrVal;
    printf("[C] %s\n", msg);
    (void)result;
}

int main(void) {
    printf("=== Testing Different Module Names ===\n\n");

    Umka* umka = umkaAlloc();
    umkaInit(umka, NULL, "", 1024 * 1024, NULL, 0, NULL, true, false, NULL);
    umkaAddFunc(umka, "print_message", print_message);
    umkaAddModule(umka, "ffi.um", "fn print_message*(msg: str)\n");

    const char* main_source =
        "import \"ffi.um\"\n"
        "fn test_function() { print_message(\"Hello!\") }\n";

    // Try empty string as module name
    printf("Test 1: Add module with empty string \"\"...\n");
    if (!umkaAddModule(umka, "", main_source)) {
        UmkaError* error = umkaGetError(umka);
        printf("  Failed to add: %s\n", error->msg);
        umkaFree(umka);
        return 1;
    }

    if (!umkaCompile(umka)) {
        UmkaError* error = umkaGetError(umka);
        printf("  Failed to compile: %s\n", error->msg);
        umkaFree(umka);
        return 1;
    }

    UmkaFuncContext fn;

    // Try getting with empty string
    printf("  Try umkaGetFunc with \"\"...\n");
    if (umkaGetFunc(umka, "", "test_function", &fn)) {
        printf("  ✓ SUCCESS with \"\"\n");
        umkaCall(umka, &fn);
    } else {
        printf("  ✗ Failed with \"\"\n");
    }

    // Try getting with NULL
    printf("  Try umkaGetFunc with NULL...\n");
    if (umkaGetFunc(umka, NULL, "test_function", &fn)) {
        printf("  ✓ SUCCESS with NULL\n");
        umkaCall(umka, &fn);
    } else {
        printf("  ✗ Failed with NULL\n");
    }

    umkaFree(umka);
    return 0;
}
