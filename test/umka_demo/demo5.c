/**
 * demo5 - Test the exact pattern we're using in rebuild:
 * 1. umkaInit with empty string
 * 2. umkaAddFunc for external functions
 * 3. umkaAddModule with FFI declarations
 * 4. umkaAddModule with NULL name + import statement
 * 5. umkaGetFunc with NULL name
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "umka_api.h"

// External function
void print_message(UmkaStackSlot *params, UmkaStackSlot *result) {
    const char* msg = (const char*)umkaGetParam(params, 0)->ptrVal;
    printf("[C] %s\n", msg);
    (void)result;
}

int main(void) {
    printf("=== Testing Rebuild Pattern ===\n\n");

    Umka* umka = umkaAlloc();

    // Step 1: Init with empty string
    printf("1. umkaInit with empty string...\n");
    if (!umkaInit(umka, NULL, "", 1024 * 1024, NULL, 0, NULL, true, false, NULL)) {
        fprintf(stderr, "Failed to init\n");
        return 1;
    }

    // Step 2: Register external function
    printf("2. umkaAddFunc...\n");
    if (!umkaAddFunc(umka, "print_message", print_message)) {
        fprintf(stderr, "Failed to add func\n");
        return 1;
    }

    // Step 3: Add FFI module
    printf("3. umkaAddModule with FFI declarations...\n");
    const char* ffi_module = "fn print_message*(msg: str)\n";
    if (!umkaAddModule(umka, "ffi.um", ffi_module)) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to add FFI module: %s\n", error->msg);
        return 1;
    }

    // Step 4: Add main module with NULL name and import
    printf("4. umkaAddModule with NULL name + import...\n");
    const char* main_source =
        "import \"ffi.um\"\n"
        "\n"
        "fn test_function() {\n"
        "    print_message(\"Hello from test_function!\")\n"
        "}\n";

    if (!umkaAddModule(umka, NULL, main_source)) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to add main module: %s\n", error->msg);
        return 1;
    }

    // Step 5: Compile
    printf("5. umkaCompile...\n");
    if (!umkaCompile(umka)) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to compile: %s\n", error->msg);
        return 1;
    }

    // Step 6: Get function with NULL
    printf("6. umkaGetFunc with NULL module name...\n");
    UmkaFuncContext fn;
    if (!umkaGetFunc(umka, NULL, "test_function", &fn)) {
        printf("✗ FAILED\n");
        return 1;
    }
    printf("✓ SUCCESS\n\n");

    // Step 7: Call function
    printf("7. umkaCall...\n");
    if (umkaCall(umka, &fn) != 0) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to call: %s\n", error->msg);
        return 1;
    }

    umkaFree(umka);
    printf("\n=== Success! ===\n");
    return 0;
}
