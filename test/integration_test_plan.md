# Rebuild Integration Test Plan

## Overview

This document specifies a comprehensive integration test for the Rebuild build system. The test exercises the core features of Rebuild including basic compilation, dependency tracking, caching, dynamic dependencies, and header dependency detection.

## Test Workspace Structure

The test workspace will be located at `test/workspace/basic_c_project/` and will contain the following files:

```
test/workspace/basic_c_project/
├── BUILD.um                    # Build definition
├── src/
│   ├── math/
│   │   ├── math_lib.c         # Library implementation
│   │   └── math_lib.h         # Library header
│   ├── utils/
│   │   ├── string_utils.c     # Utility library
│   │   └── string_utils.h     # Utility header
│   └── app/
│       ├── main.c             # Main application (depends on math library)
│       └── config.h           # Configuration header
└── include/
    └── common.h               # Common header used across project
```

## Source File Contents

### 1. `include/common.h`
```c
#ifndef COMMON_H
#define COMMON_H

#define PROJECT_VERSION "1.0.0"
#define DEBUG_ENABLED 0

#endif // COMMON_H
```

### 2. `src/math/math_lib.h`
```c
#ifndef MATH_LIB_H
#define MATH_LIB_H

#include "common.h"

int add(int a, int b);
int multiply(int a, int b);
double divide(double a, double b);

#endif // MATH_LIB_H
```

### 3. `src/math/math_lib.c`
```c
#include "math_lib.h"
#include <stdio.h>

int add(int a, int b) {
    #if DEBUG_ENABLED
    printf("Adding %d + %d\n", a, b);
    #endif
    return a + b;
}

int multiply(int a, int b) {
    #if DEBUG_ENABLED
    printf("Multiplying %d * %d\n", a, b);
    #endif
    return a * b;
}

double divide(double a, double b) {
    if (b == 0.0) {
        return 0.0;
    }
    return a / b;
}
```

### 4. `src/utils/string_utils.h`
```c
#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include "common.h"

void print_banner(const char* text);
int string_length(const char* str);

#endif // STRING_UTILS_H
```

### 5. `src/utils/string_utils.c`
```c
#include "string_utils.h"
#include <stdio.h>

void print_banner(const char* text) {
    printf("===================\n");
    printf("%s\n", text);
    printf("===================\n");
}

int string_length(const char* str) {
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}
```

### 6. `src/app/config.h`
```c
#ifndef CONFIG_H
#define CONFIG_H

#define APP_NAME "Math Calculator"
#define MAX_OPERATIONS 100

#endif // CONFIG_H
```

### 7. `src/app/main.c`
```c
#include <stdio.h>
#include "math_lib.h"
#include "string_utils.h"
#include "config.h"

int main(int argc, char** argv) {
    print_banner(APP_NAME " v" PROJECT_VERSION);

    printf("Testing math operations:\n");
    printf("5 + 3 = %d\n", add(5, 3));
    printf("5 * 3 = %d\n", multiply(5, 3));
    printf("10 / 2 = %.2f\n", divide(10.0, 2.0));

    return 0;
}
```

## BUILD.um File

The BUILD.um file demonstrates proper use of the Rebuild API including tool usage, dependency tracking, and dynamic dependencies:

```umka
// BUILD.um - Build definition for basic C project integration test

fn register_targets() {
    // Target 1: Math library (static library)
    target("lib:math", fn() {
        cc := deptool("clang")

        // Compile math_lib.c with dependency tracking
        math_obj := cc.compile("src/math/math_lib.c", {
            includes: ["include/", "src/math/"],
            flags: ["-O2", "-Wall", "-Wextra", "-fPIC"],
            dep_tracking: true,  // Auto-register header dependencies
            output: "math_lib.o"
        })

        // Create static library
        ar := deptool("ar")
        result := sys([ar.bin, "rcs", "libmath.a", math_obj.output])

        if !result.ok {
            fail("Failed to create static library: " + result.stderr)
        }
    })

    // Target 2: String utilities library (static library)
    target("lib:string_utils", fn() {
        cc := deptool("clang")

        // Compile string_utils.c
        utils_obj := cc.compile("src/utils/string_utils.c", {
            includes: ["include/", "src/utils/"],
            flags: ["-O2", "-Wall", "-Wextra", "-fPIC"],
            dep_tracking: true,
            output: "string_utils.o"
        })

        // Create static library
        ar := deptool("ar")
        sys([ar.bin, "rcs", "libstring_utils.a", utils_obj.output])
    })

    // Target 3: Main application (depends on both libraries)
    target("bin:app", fn() {
        cc := deptool("clang")

        // Use depend_on() to dynamically link against library targets
        math_lib_path := depend_on("lib:math")
        utils_lib_path := depend_on("lib:string_utils")

        // Compile main.c
        main_obj := cc.compile("src/app/main.c", {
            includes: ["include/", "src/math/", "src/utils/", "src/app/"],
            flags: ["-O2", "-Wall", "-Wextra"],
            dep_tracking: true,
            output: "main.o"
        })

        // Link the final binary
        cc.link([main_obj.output], {
            output: "calculator",
            lib_paths: [math_lib_path, utils_lib_path],
            libs: ["math", "string_utils"],
            flags: []
        })
    })

    // Target 4: Run the application (depends on bin:app)
    target("run", fn() {
        app_path := depend_on("bin:app")

        // Execute the binary
        result := sys([app_path + "/calculator"])

        if !result.ok {
            fail("Application failed: " + result.stderr)
        }

        // Print output
        print(result.stdout)
    })

    // Target 5: All (convenience target)
    target("all", fn() {
        depend_on("bin:app")
        print("Build complete!")
    })
}
```

## Test Execution Steps and Assertions

### Test Phase 1: Initial Clean Build

**Action**: Execute `rebuild bin:app` from clean state (no cached artifacts)

**Expected Behavior**:
1. All targets should be built from scratch
2. Build order should be:
   - `lib:math` (no dependencies)
   - `lib:string_utils` (no dependencies, can run in parallel with lib:math)
   - `bin:app` (depends on both libraries)
3. Traces should be created for each target in `storage/traces/`
4. Output files should be in `outputs/lib:math/`, `outputs/lib:string_utils/`, and `outputs/bin:app/`

**Assertions**:
- File exists: `outputs/lib:math/libmath.a`
- File exists: `outputs/lib:string_utils/libstring_utils.a`
- File exists: `outputs/bin:app/calculator`
- Binary is executable: `test -x outputs/bin:app/calculator`
- Trace files created for all three targets
- No errors or warnings in build output
- Total rebuild count: 3 targets

**Verification Command**:
```bash
./rebuild bin:app
./outputs/bin:app/calculator
```

**Expected Output**:
```
===================
Math Calculator v1.0.0
===================
Testing math operations:
5 + 3 = 8
5 * 3 = 15
10 / 2 = 5.00
```

---

### Test Phase 2: No-Op Rebuild (Caching Test)

**Action**: Execute `rebuild bin:app` again without any source changes

**Expected Behavior**:
1. All targets should be cache hits
2. No compilation or linking should occur
3. Traces should be validated:
   - Request keys match
   - All dependency hashes match cached values
4. Early cutoff should occur on first dependency hash match

**Assertions**:
- Build completes in <1% of original build time
- Zero targets rebuilt (all cache hits)
- No compiler or linker invocations
- Output: "bin:app: up-to-date (cached)"
- Trace validation succeeds for all targets

**Metrics to Verify**:
- Cache hit rate: 100%
- Files hashed: ~10 (source files + headers, early cutoff on first match)
- Processes spawned: 0
- Wall time: <100ms

---

### Test Phase 3: Source File Modification (Dependency Tracking)

**Action**: Modify `src/math/math_lib.c` - change the `add` function implementation

```c
int add(int a, int b) {
    // Changed implementation
    return a + b + 0;  // Add comment or trivial change
}
```

Then execute: `rebuild bin:app`

**Expected Behavior**:
1. Only affected targets should rebuild:
   - `lib:math` rebuilds (source changed)
   - `lib:string_utils` does NOT rebuild (no dependency on math_lib.c)
   - `bin:app` rebuilds (depends on lib:math)
2. Trace for `lib:math` is invalidated (source hash changed)
3. Trace for `lib:string_utils` remains valid
4. Trace for `bin:app` is invalidated (dependency lib:math changed)

**Assertions**:
- Rebuild count: 2 targets (`lib:math`, `bin:app`)
- Cache hit count: 1 target (`lib:string_utils`)
- New trace created for `lib:math`
- New trace created for `bin:app`
- Trace for `lib:string_utils` unchanged
- Final binary still works correctly

**Incremental Build Metrics**:
- Targets rebuilt: 2/3 (66% cache hit)
- Files recompiled: 1 (math_lib.c)
- Objects relinked: 1 (libmath.a)
- Binaries relinked: 1 (calculator)

---

### Test Phase 4: Header File Modification (Header Dependency Test)

**Action**: Modify `include/common.h` - change DEBUG_ENABLED flag

```c
#define DEBUG_ENABLED 1  // Changed from 0 to 1
```

Then execute: `rebuild bin:app`

**Expected Behavior**:
1. All targets that include `common.h` should rebuild:
   - `lib:math` rebuilds (includes common.h via math_lib.h)
   - `lib:string_utils` rebuilds (includes common.h via string_utils.h)
   - `bin:app` rebuilds (includes common.h transitively)
2. Header dependencies discovered via depfile tracking
3. All registered header dependencies cause trace invalidation

**Assertions**:
- Rebuild count: 3 targets (all rebuild)
- All source files recompiled
- Depfiles correctly tracked header dependencies
- Each target's trace includes `include/common.h` as dependency
- New behavior: debug output appears when running calculator

**Header Dependency Verification**:
- Trace for `lib:math` lists dependencies: `math_lib.c`, `math_lib.h`, `common.h`
- Trace for `lib:string_utils` lists: `string_utils.c`, `string_utils.h`, `common.h`
- Trace for `bin:app` lists: `main.c`, `math_lib.h`, `string_utils.h`, `config.h`, `common.h`

**Running Modified Binary**:
```bash
./outputs/bin:app/calculator
```

**Expected Output** (with DEBUG_ENABLED=1):
```
===================
Math Calculator v1.0.0
===================
Testing math operations:
Adding 5 + 3
5 + 3 = 8
Multiplying 5 * 3
5 * 3 = 15
10 / 2 = 5.00
```

---

### Test Phase 5: Dynamic Dependency Test (depend_on Verification)

**Action**: Modify BUILD.um to add a new target that uses depend_on()

Add this target to BUILD.um:
```umka
target("test:verify", fn() {
    app_path := depend_on("bin:app")

    // Verify the binary was built correctly
    result := sys([app_path + "/calculator"])

    if !result.ok {
        fail("Verification failed")
    }

    // Check output contains expected text
    if !contains(result.stdout, "Math Calculator") {
        fail("Output missing expected banner")
    }

    if !contains(result.stdout, "5 + 3 = 8") {
        fail("Output missing expected calculation")
    }

    print("Verification passed!")
})
```

Then execute: `rebuild test:verify`

**Expected Behavior**:
1. `test:verify` executes
2. Calls `depend_on("bin:app")` which suspends execution
3. Scheduler checks cache for `bin:app` (should be cache hit from previous test)
4. `bin:app` path returned to `test:verify` which resumes
5. Test runs the binary and verifies output
6. Test passes

**Assertions**:
- `test:verify` target executes successfully
- `bin:app` is served from cache (no rebuild)
- Test output shows "Verification passed!"
- Fiber suspension/resumption works correctly
- Dynamic dependency `bin:app` is recorded in trace for `test:verify`

**Trace Verification**:
- Trace for `test:verify` includes dependency on `bin:app` output tree hash
- Changing `bin:app` invalidates `test:verify` trace

---

### Test Phase 6: Tool API Test (Clang Tool Verification)

**Action**: Add a target that explicitly uses multiple tool API methods

Add to BUILD.um:
```umka
target("lib:combined", fn() {
    cc := deptool("clang")

    // Compile both math and string_utils objects
    math_obj := cc.compile("src/math/math_lib.c", {
        includes: ["include/", "src/math/"],
        flags: ["-O2", "-fPIC"],
        dep_tracking: true
    })

    utils_obj := cc.compile("src/utils/string_utils.c", {
        includes: ["include/", "src/utils/"],
        flags: ["-O2", "-fPIC"],
        dep_tracking: true
    })

    // Create combined static library using tool API
    cc.static_lib([math_obj.output, utils_obj.output], "libcombined.a")
})
```

Then execute: `rebuild lib:combined`

**Expected Behavior**:
1. Clang tool is loaded via `deptool("clang")`
2. Tool binary path is resolved (system clang or configured path)
3. Tool module `tools/clang.um` is loaded and its hash included in trace
4. Multiple API methods used: `compile()` and `static_lib()`
5. Depfile tracking automatically registers header dependencies
6. Combined library created successfully

**Assertions**:
- Tool loaded: clang found and version detected
- Tool API methods work: `compile()` succeeds
- Tool API methods work: `static_lib()` succeeds
- Depfiles parsed automatically
- Header dependencies registered
- Output file exists: `outputs/lib:combined/libcombined.a`
- Trace includes tool module hash

**Tool Module Hash Verification**:
- Modify `tools/clang.um` (if it exists) or tool API behavior
- Rebuild should occur even if source files unchanged
- Proves tool API code is part of cache key

---

### Test Phase 7: Parallel Execution Test

**Action**: Execute `rebuild all` with multiple parallel jobs

Execute: `rebuild -j4 all` (assuming -j flag for parallelism)

**Expected Behavior**:
1. Independent targets execute in parallel:
   - `lib:math` and `lib:string_utils` can run simultaneously
2. Dependent target waits:
   - `bin:app` waits for both libraries to complete
3. Scheduler efficiently uses CPU cores
4. No race conditions in cache access or file writes

**Assertions**:
- Build completes successfully with parallelism
- Wall time is less than sequential build time
- No file corruption
- No trace corruption
- Deterministic output (same result as sequential build)

**Parallelism Metrics**:
- CPU utilization: >150% (on 4-core system)
- Parallel tasks: 2 (lib:math and lib:string_utils)
- Sequential tasks: 1 (bin:app after libraries)
- Speedup: ~1.8-2x compared to sequential

---

### Test Phase 8: Intermediate Header Modification (Selective Rebuild)

**Action**: Modify `src/app/config.h` - change MAX_OPERATIONS

```c
#define MAX_OPERATIONS 200  // Changed from 100 to 200
```

Then execute: `rebuild bin:app`

**Expected Behavior**:
1. Only `bin:app` rebuilds (only target that includes config.h)
2. Libraries do NOT rebuild (they don't include config.h)
3. Demonstrates fine-grained dependency tracking

**Assertions**:
- Rebuild count: 1 target (`bin:app` only)
- Cache hits: 2 targets (`lib:math`, `lib:string_utils`)
- File recompiled: `main.c` only
- Optimal incremental behavior achieved

**Selective Rebuild Verification**:
- Trace for `lib:math` still valid (no config.h dependency)
- Trace for `lib:string_utils` still valid (no config.h dependency)
- Trace for `bin:app` invalidated (includes config.h)

---

### Test Phase 9: Clean and Rebuild (Trace Persistence Test)

**Action**: Clear outputs directory but keep traces

```bash
rm -rf outputs/
rebuild bin:app
```

**Expected Behavior**:
1. Traces still exist in `storage/traces/`
2. Trace validation checks source file hashes
3. If sources unchanged, traces are valid
4. Outputs regenerated from storage or rebuild
5. Content-addressed storage allows recreation

**Assertions**:
- Traces survive output deletion
- Source hashes still match trace records
- Outputs correctly regenerated
- Final binary identical to previous build (deterministic)

**Content-Addressed Storage Verification**:
- Outputs stored in `storage/objects/` by content hash
- Deleting `outputs/` just removes symlinks
- Traces point to content-addressed outputs
- Outputs can be restored from storage

---

### Test Phase 10: Full Clean Build (Complete Reset)

**Action**: Clear all cached state

```bash
rm -rf storage/ outputs/ tmp/
rebuild bin:app
```

**Expected Behavior**:
1. Complete rebuild from scratch
2. All traces regenerated
3. All outputs rebuilt
4. Identical to Test Phase 1 behavior

**Assertions**:
- Rebuild count: 3 targets
- Build time similar to Phase 1
- All traces created fresh
- Final binary works correctly
- Deterministic output (identical to Phase 1 binary)

---

## Summary of Test Coverage

### Features Tested

1. **Basic C Compilation** ✓
   - Compiling source files to object files
   - Creating static libraries with ar
   - Linking binaries

2. **Dependency Tracking** ✓
   - Source file changes trigger rebuilds
   - Unchanged files use cache
   - Transitive dependencies handled correctly

3. **Tool API Usage** ✓
   - Loading tools with `deptool()`
   - Using high-level API methods (compile, link, static_lib)
   - Tool module hashing affects cache keys

4. **Caching** ✓
   - Cache hits when nothing changed
   - Cache invalidation on source changes
   - Trace-based validation
   - Early cutoff optimization

5. **Dynamic Dependencies** ✓
   - `depend_on()` suspends and resumes fibers
   - Dependencies resolved at runtime
   - Correct build order maintained
   - Parallel execution of independent targets

6. **Header Dependencies** ✓
   - Depfile tracking discovers headers
   - Header changes trigger recompilation
   - Transitive header dependencies tracked
   - Fine-grained dependency precision

### Expected Test Outcomes

- **Correctness**: All builds produce working binaries
- **Incrementality**: Minimal rebuilds on changes
- **Performance**: Fast cache hits, parallel execution
- **Determinism**: Identical outputs on repeated builds
- **Robustness**: Handles various change scenarios

### Test Metrics to Collect

1. **Build Times**:
   - Clean build time
   - Cached rebuild time (should be <1% of clean)
   - Incremental rebuild time

2. **Cache Statistics**:
   - Cache hit rate per scenario
   - Trace validation time
   - Storage space used

3. **Dependency Tracking**:
   - Number of dependencies discovered per target
   - Accuracy of incremental rebuilds
   - False rebuild rate (should be 0%)

4. **Parallelism**:
   - Number of parallel tasks
   - CPU utilization
   - Speedup factor

## Test Execution Environment

### Prerequisites

- Rebuild build system compiled and available
- Clang compiler installed (or configured toolchain)
- ar tool available (for static libraries)
- BLAKE2b library linked
- libuv and UMKA properly initialized

### Test Runner Script

The test should be automated with a script that:

1. Sets up the workspace
2. Creates all source files
3. Executes each test phase
4. Validates assertions
5. Collects metrics
6. Reports pass/fail status

### Success Criteria

The integration test passes if:

- All 10 test phases complete successfully
- All assertions pass
- No errors or warnings during builds
- Final binaries execute correctly
- Cache hit rates meet expectations (>95% on no-op rebuilds)
- Incremental rebuilds are precise (no over-rebuilding)
- Parallel execution is safe (no race conditions)

## Future Test Enhancements

1. **Cross-Compilation Test**: Build for different target architectures
2. **Large Project Test**: Scale to 100+ source files
3. **Stress Test**: Concurrent builds with high parallelism
4. **Failure Handling Test**: Verify error messages and recovery
5. **Remote Caching Test**: Distributed cache verification
6. **Non-Determinism Detection**: Verify reproducible builds
7. **Watch Mode Test**: File system watching and auto-rebuild

## Appendix: File Sizes and Build Metrics

### Expected File Sizes

- `math_lib.o`: ~2-3 KB
- `string_utils.o`: ~2-3 KB
- `main.o`: ~3-4 KB
- `libmath.a`: ~3-4 KB
- `libstring_utils.a`: ~3-4 KB
- `calculator` binary: ~20-30 KB (depends on linking)

### Expected Build Times (Reference Machine: 4-core CPU)

- Clean build: 1-2 seconds
- Cached rebuild: 10-50 milliseconds
- Incremental rebuild (1 source file): 200-500 milliseconds
- Header-triggered rebuild: 1-1.5 seconds

### Expected Cache Characteristics

- Trace file size: ~500 bytes - 2 KB each
- Object storage: ~30-40 KB total
- Hash computation: ~1-2 ms per file
- Trace validation: ~5-10 ms per target

---

## Conclusion

This integration test comprehensively validates the core functionality of the Rebuild build system. It ensures that the system correctly handles:

- Basic compilation workflows
- Incremental rebuilds with precise dependency tracking
- Efficient caching and trace validation
- Dynamic dependency resolution
- Header dependency tracking via depfiles
- Tool API integration
- Parallel execution safety

The test is designed to be reproducible, automated, and extensible for future features.
