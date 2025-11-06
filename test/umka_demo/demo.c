/**
 * Minimal UMKA demo to test umkaGetFunc() usage
 *
 * This demonstrates the MODULE NAME issue:
 * When using umkaInit() with empty string + umkaAddModule(),
 * you must use the module name (not NULL) in umkaGetFunc()
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

// Helper function to read file contents
char* read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* buffer = malloc(size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    size_t read_size = fread(buffer, 1, size, file);
    fclose(file);

    if (read_size != (size_t)size) {
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    return buffer;
}

int main(void) {
    printf("=== UMKA umkaGetFunc() Module Name Demo ===\n\n");

    // This is the pattern used by rebuild:
    // 1. Init with empty string
    // 2. Add external functions
    // 3. Add module from file
    // 4. Compile
    // 5. Get function - BUT WITH WHAT MODULE NAME?

    Umka* umka = umkaAlloc();
    if (!umka) {
        fprintf(stderr, "Failed to allocate UMKA instance\n");
        return 1;
    }

    // Initialize with empty string (NO main module file)
    printf("Step 1: Initialize UMKA with empty string...\n");
    if (!umkaInit(umka, NULL, "", 1024 * 1024, NULL, 0, NULL, true, false, NULL)) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to initialize UMKA: %s (line %d)\n", error->msg, error->line);
        umkaFree(umka);
        return 1;
    }
    printf("  ✓ Initialized\n\n");

    // Add external function
    printf("Step 2: Register external C function 'print_message'...\n");
    if (!umkaAddFunc(umka, "print_message", print_message)) {
        fprintf(stderr, "Failed to add external function\n");
        umkaFree(umka);
        return 1;
    }
    printf("  ✓ Registered\n\n");

    // Read file and add as module
    printf("Step 3: Load test_script.um as a module...\n");
    char* source = read_file("test_script.um");
    if (!source) {
        fprintf(stderr, "Failed to read script file\n");
        umkaFree(umka);
        return 1;
    }

    const char* module_name = "test_script.um";
    if (!umkaAddModule(umka, module_name, source)) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to add module: %s (line %d)\n", error->msg, error->line);
        free(source);
        umkaFree(umka);
        return 1;
    }
    free(source);
    printf("  ✓ Module '%s' added\n\n", module_name);

    // Compile
    printf("Step 4: Compile...\n");
    if (!umkaCompile(umka)) {
        UmkaError* error = umkaGetError(umka);
        fprintf(stderr, "Failed to compile: %s (line %d)\n", error->msg, error->line);
        umkaFree(umka);
        return 1;
    }
    printf("  ✓ Compiled successfully\n\n");

    // Now try to get the function
    printf("Step 5: Try to get 'test_function' with different module names...\n");
    printf("--------------------------------------------------------\n\n");

    UmkaFuncContext test_fn;

    // Try 1: NULL module name (main module - should FAIL)
    printf("Try 1: umkaGetFunc(umka, NULL, \"test_function\", &fn)\n");
    if (!umkaGetFunc(umka, NULL, "test_function", &test_fn)) {
        printf("  ✗ FAILED (as expected) - function not in main module\n\n");
    } else {
        printf("  ✓ SUCCESS - this is unexpected!\n\n");
    }

    // Try 2: With the module name (should SUCCEED)
    printf("Try 2: umkaGetFunc(umka, \"%s\", \"test_function\", &fn)\n", module_name);
    if (!umkaGetFunc(umka, module_name, "test_function", &test_fn)) {
        printf("  ✗ FAILED - this should have worked!\n");
        UmkaError* error = umkaGetError(umka);
        if (error && error->msg) {
            printf("  Error: %s\n", error->msg);
        }
    } else {
        printf("  ✓ SUCCESS - Got the function!\n\n");

        // Call the function
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
    printf("\nKEY FINDING:\n");
    printf("When using umkaInit(umka, NULL, \"\", ...) + umkaAddModule(umka, name, source),\n");
    printf("you MUST use umkaGetFunc(umka, name, fn_name, ...) with the MODULE NAME,\n");
    printf("NOT NULL!\n");

    return 0;
}
