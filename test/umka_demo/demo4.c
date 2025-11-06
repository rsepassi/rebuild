/**
 * demo4 - Test using NULL as module name in umkaAddModule
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
    printf("=== Testing NULL module name in umkaAddModule ===\n\n");

    Umka* umka = umkaAlloc();
    if (!umka) {
        fprintf(stderr, "Failed to allocate UMKA instance\n");
        return 1;
    }

    // Initialize with empty string
    printf("Step 1: Initialize with empty string...\n");
    if (!umkaInit(umka, NULL, "", 1024 * 1024, NULL, 0, NULL, true, false, NULL)) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to initialize UMKA: %s (line %d)\n", error->msg, error->line);
        umkaFree(umka);
        return 1;
    }
    printf("  ✓ Initialized\n\n");

    // Add external function
    printf("Step 2: Register external function...\n");
    if (!umkaAddFunc(umka, "print_message", print_message)) {
        fprintf(stderr, "Failed to add external function\n");
        umkaFree(umka);
        return 1;
    }
    printf("  ✓ Registered\n\n");

    // Read file
    FILE* f = fopen("test_script.um", "rb");
    if (!f) {
        fprintf(stderr, "Failed to open test_script.um\n");
        umkaFree(umka);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* source = malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);

    // Add as module with NULL name
    printf("Step 3: Add module with NULL as module name...\n");
    if (!umkaAddModule(umka, NULL, source)) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to add module: %s (line %d)\n", error->msg, error->line);
        free(source);
        umkaFree(umka);
        return 1;
    }
    free(source);
    printf("  ✓ Module added with NULL name\n\n");

    // Compile
    printf("Step 4: Compile...\n");
    if (!umkaCompile(umka)) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to compile: %s (line %d)\n", error->msg, error->line);
        umkaFree(umka);
        return 1;
    }
    printf("  ✓ Compiled\n\n");

    // Get function with NULL module name
    printf("Step 5: Try to get function with NULL module name...\n");
    UmkaFuncContext test_fn;
    if (!umkaGetFunc(umka, NULL, "test_function", &test_fn)) {
        printf("  ✗ FAILED\n");
        UmkaError* error = umkaGetError(umka);
        if (error && error->msg) {
            printf("  Error: %s\n", error->msg);
        }
    } else {
        printf("  ✓ SUCCESS!\n\n");

        printf("Step 6: Call the function...\n");
        if (umkaCall(umka, &test_fn) != 0) {
            UmkaError* error = umkaGetError(umka);
            fprintf(stderr, "Error: %s (line %d)\n", error->msg, error->line);
        } else {
            printf("  ✓ Function executed\n");
        }
    }

    umkaFree(umka);
    printf("\n=== Done ===\n");
    return 0;
}
