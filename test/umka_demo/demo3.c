/**
 * demo3 - Following the exact pattern from 3dcam example
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "umka_api.h"

// External function that can be called from UMKA
void print_message(UmkaStackSlot *params, UmkaStackSlot *result) {
    const char* msg = (const char*)umkaGetParam(params, 0)->ptrVal;
    printf("[C] Message from UMKA: %s\n", msg);
    (void)result; // Unused
}

int main(void) {
    printf("=== UMKA Following 3dcam Pattern ===\n\n");

    Umka* umka = umkaAlloc();
    if (!umka) {
        fprintf(stderr, "Failed to allocate UMKA instance\n");
        return 1;
    }

    // Pattern from 3dcam:
    // 1. umkaInit with filename (loads main module)
    // 2. umkaAddFunc (register external functions)
    // 3. umkaAddModule with FFI declarations
    // 4. umkaCompile
    // 5. umkaGetFunc with NULL module name

    printf("Step 1: Initialize with main file (loads but doesn't compile yet)...\n");
    if (!umkaInit(umka, "test_main.um", NULL, 1024 * 1024, NULL, 0, NULL, true, false, NULL)) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to initialize UMKA: %s (line %d)\n", error->msg, error->line);
        umkaFree(umka);
        return 1;
    }
    printf("  ✓ Loaded test_main.um\n\n");

    printf("Step 2: Register external C functions...\n");
    if (!umkaAddFunc(umka, "print_message", print_message)) {
        fprintf(stderr, "Failed to add external function\n");
        umkaFree(umka);
        return 1;
    }
    printf("  ✓ Registered print_message\n\n");

    printf("Step 3: Add FFI module with external function declarations...\n");
    // IMPORTANT: Use * to export the function so it can be imported
    if (!umkaAddModule(umka, "ffi.um", "fn print_message*(msg: str)")) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to add FFI module: %s (line %d)\n", error->msg, error->line);
        umkaFree(umka);
        return 1;
    }
    printf("  ✓ Added ffi.um module\n\n");

    printf("Step 4: Compile...\n");
    if (!umkaCompile(umka)) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to compile: %s (line %d)\n", error->msg, error->line);
        umkaFree(umka);
        return 1;
    }
    printf("  ✓ Compiled successfully\n\n");

    printf("Step 5: Get function with NULL module name (main module)...\n");
    UmkaFuncContext test_fn;
    if (!umkaGetFunc(umka, NULL, "test_function", &test_fn)) {
        printf("  ✗ FAILED\n");
        UmkaError* error = umkaGetError(umka);
        if (error && error->msg) {
            printf("  Error: %s\n", error->msg);
        }
    } else {
        printf("  ✓ SUCCESS - Got the function!\n\n");

        printf("Step 6: Call the function...\n");
        if (umkaCall(umka, &test_fn) != 0) {
            UmkaError* error = umkaGetError(umka);
            fprintf(stderr, "Error calling function: %s (line %d)\n", error->msg, error->line);
        } else {
            printf("  ✓ Function executed successfully\n");
        }
    }

    umkaFree(umka);

    printf("\n=== Demo Complete ===\n");
    return 0;
}
