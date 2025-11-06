# Target Registration System - Quick Reference

## Core Data Structures

```c
// Target: represents a buildable target
typedef struct Target {
    char* name;              // "rebuild", "lib:foo", etc.
    char* function_name;     // "target_rebuild", "target_lib_foo", etc.
    void* umka_script;       // UMKA script instance
} Target;

// TargetRegistry: manages all targets
typedef struct TargetRegistry {
    Map* targets;            // name -> Target*
    void* umka;              // UMKA instance
} TargetRegistry;
```

## C API

```c
// Create and destroy
TargetRegistry* target_registry_create(void* umka);
void target_registry_free(TargetRegistry* registry);

// Register and query
RebuildError target_registry_register(TargetRegistry* registry,
                                     const char* name,
                                     const char* function_name,
                                     void* script);
Target* target_registry_get(TargetRegistry* registry, const char* name);
bool target_registry_has(TargetRegistry* registry, const char* name);
char** target_registry_list(TargetRegistry* registry, size_t* count);

// Load BUILD.um files
RebuildError target_registry_load_build_file(TargetRegistry* registry,
                                             const char* path);
```

## UMKA BUILD.um File Format

```umka
// Helper function that calls FFI
fn target(name: str, fn_name: str) {
    rebuild_register_target(name, fn_name)
}

// Required function - called during BUILD.um loading
fn register_targets() {
    target("my-binary", "target_my_binary")
    target("my-library", "target_my_library")
}

// Target implementation functions
fn target_my_binary() {
    rebuild_log_info("Building my-binary...")
    // Build logic here
}

fn target_my_library() {
    rebuild_log_info("Building my-library...")
    // Build logic here
}
```

## Usage Example

```c
#include "target.h"
#include "umka_bridge.h"

int main(void) {
    // Initialize UMKA bridge
    umka_bridge_init();

    // Create registry
    TargetRegistry* registry = target_registry_create(NULL);

    // Load BUILD.um files
    if (target_registry_load_build_file(registry, "BUILD.um") != REBUILD_OK) {
        fprintf(stderr, "Failed to load BUILD.um\n");
        return 1;
    }

    // Check if target exists
    if (!target_registry_has(registry, "rebuild")) {
        fprintf(stderr, "Target 'rebuild' not found\n");
        return 1;
    }

    // Get target
    Target* target = target_registry_get(registry, "rebuild");
    printf("Target: %s -> %s()\n", target->name, target->function_name);

    // List all targets
    size_t count;
    char** names = target_registry_list(registry, &count);
    printf("\nAll targets (%zu):\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("  - %s\n", names[i]);
    }
    rebuild_free(names);

    // Execute target (pseudocode - requires scheduler integration)
    // UmkaFiber fiber = umka_create_fiber(target->umka_script,
    //                                    target->function_name);
    // umka_resume_fiber(fiber);

    // Cleanup
    target_registry_free(registry);
    umka_bridge_cleanup();

    return 0;
}
```

## FFI Function

**C Side (registered with UMKA):**
```c
void umka_ffi_rebuild_register_target(void* params, void* result);
```

**UMKA Side (called from BUILD.um):**
```umka
rebuild_register_target("target-name", "function_name")
```

## Flow Diagram

```
BUILD.um file
    ↓
fn register_targets() called
    ↓
target("name", "fn") called
    ↓
rebuild_register_target("name", "fn") FFI
    ↓
umka_ffi_rebuild_register_target() in C
    ↓
target_registry_ffi_register()
    ↓
target_registry_register()
    ↓
Target added to Map in TargetRegistry
```

## Error Codes

- `REBUILD_OK` - Success
- `REBUILD_ERROR_MEMORY` - Memory allocation failed
- `REBUILD_ERROR_PARSE` - BUILD.um parse/load failed
- `REBUILD_ERROR_EXEC` - Target function execution failed

## Files Modified

1. **New**: `src/target.h` - API declarations
2. **New**: `src/target.c` - Implementation
3. **Modified**: `src/umka_bridge.h` - Added FFI declaration
4. **Modified**: `src/umka_bridge.c` - Added FFI implementation
5. **Example**: `BUILD.um.example` - Sample BUILD.um file

## Next Steps for Integration

1. **In Scheduler**: Create TargetRegistry at startup
2. **In Scheduler**: Load all BUILD.um files in workspace
3. **In Scheduler**: Query registry for requested target
4. **In Scheduler**: Create Recipe from Target
5. **In Scheduler**: Execute target function via UMKA fiber
