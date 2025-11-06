# Rebuild - TODO List

## Critical - Blocking Functionality

### 1. Vendor Dependencies
- [ ] Copy libuv source code into vendor/libuv/
- [ ] Copy UMKA source code into vendor/umka/
- [ ] Copy BLAKE2 implementation into vendor/blake2/
- [ ] Verify all vendored code compiles cleanly
- [ ] Document vendored versions in vendor/README.md

### 2. Fix UMKA Array Passing (rebuild_sys)
**Issue**: `rebuild_sys()` receives empty arrays from UMKA (argc=0, data=NULL)
- [ ] Debug UMKA array parameter extraction
- [ ] Test with simple UMKA array examples
- [ ] Fix FFI parameter handling in src/umka_bridge.c:380-445
- [ ] Add integration test for sys() function
- [ ] Verify command execution works end-to-end

### 3. Build System Organization
- [ ] Move bootstrap build to bootstrap/ directory
  - [ ] bootstrap/Makefile - builds vendored deps + rebuild
  - [ ] bootstrap/build.sh - simple build script
  - [ ] Separate from main build/ output directory
- [ ] Ensure all build artifacts go to build/ directory
  - [ ] Object files: build/*.o
  - [ ] Dependency files: build/*.d
  - [ ] Final binary: build/rebuild (then copied to root)
- [ ] Update .gitignore for new structure
- [ ] Clean separation: bootstrap/ (make-based) vs build/ (rebuild outputs)

## Core Features

### 4. Glob Pattern Support
Currently stubbed in src/umka_bridge.c:506
- [ ] Implement single-star glob: `src/*.c`
  - Match single directory level
  - Support multiple wildcards: `foo/*/bar/*.c`
- [ ] Implement double-star glob: `src/**/*.c`
  - Recursive directory traversal
  - Match across any depth
- [ ] Integrate with existing glob() FFI function
- [ ] Add tests for glob patterns
- [ ] Handle edge cases (symlinks, permissions, missing dirs)

### 5. Directory Tree Dependencies
Design concept: `depend_on_tree("src/")` tracks entire directory
- [ ] Design API: `rebuild_depend_on_tree(path: str): str`
- [ ] Implement directory hashing (recursive)
- [ ] Track directory mtimes vs content hashes (performance trade-off)
- [ ] Register directory dependencies in trace system
- [ ] Update trace validation for directory changes
- [ ] Add FFI function to umka_bridge.c
- [ ] Test with integration workspace

## Self-Hosting Path

### 6. Bootstrap Build
Located in bootstrap/ directory
- [ ] Create bootstrap/Makefile
  - Builds vendor dependencies (libuv, umka, blake2)
  - Compiles rebuild C sources
  - Links into bootstrap/rebuild binary
- [ ] Create bootstrap/build.sh wrapper script
- [ ] Test bootstrap build from clean state
- [ ] Document bootstrap process in doc/bootstrap.md

### 7. Self-Hosting BUILD.um
Enhance BUILD.um to build rebuild itself
- [ ] Add targets for vendored dependencies
  - target("vendor:libuv") - builds libuv
  - target("vendor:umka") - builds umka
  - target("vendor:blake2") - builds blake2
- [ ] Add target for rebuild binary
  - Depends on vendor libs
  - Compiles all src/*.c files
  - Links everything together
- [ ] Test: `bootstrap/rebuild rebuild` builds using self-hosted rules
- [ ] Verify outputs go to build/ directory

### 8. Self-Hosting Verification
The "three builds" test:
- [ ] Build 1: `make -C bootstrap` → `bootstrap/rebuild`
- [ ] Build 2: `bootstrap/rebuild rebuild` → `build/rebuild`
- [ ] Build 3: `build/rebuild rebuild` → `build/rebuild2`
- [ ] Verify: `build/rebuild` and `build/rebuild2` are identical (bit-for-bit)
- [ ] Document self-hosting in doc/self-hosting.md

## Integration & Testing

### 9. Integration Tests
Using test/workspace/
- [ ] Ensure sys() tests pass (blocked by #2)
- [ ] Add glob pattern tests
- [ ] Add directory tree dependency tests
- [ ] Test cache invalidation scenarios
- [ ] Test parallel builds (multiple targets)
- [ ] Run full integration test suite from doc/integration_test_plan.md

### 10. Tool System Verification
- [ ] Verify tools/clang.um works with real builds
- [ ] Verify tools/ar.um works with real builds
- [ ] Add depfile parsing and registration
- [ ] Test tool binary hash invalidation
- [ ] Test tool module hash invalidation

## Documentation

### 11. User Documentation
- [ ] doc/quickstart.md - Getting started guide
- [ ] doc/bootstrap.md - Bootstrap build process
- [ ] doc/self-hosting.md - Self-hosting verification
- [ ] doc/build-files.md - Writing BUILD.um files
- [ ] doc/api-reference.md - FFI function reference
- [ ] README.md - Project overview and build instructions

### 12. Developer Documentation
- [ ] doc/architecture.md - System architecture overview
- [ ] doc/trace-format.md - Binary trace format spec
- [ ] doc/storage-layout.md - Content-addressed storage
- [ ] doc/contributing.md - How to contribute

## Future Enhancements (Post-MVP)

### Phase 3 Features
- [ ] Remote caching support
- [ ] Distributed builds
- [ ] Build profiling and visualization
- [ ] Incremental linking
- [ ] Watch mode for continuous builds

### Performance Optimizations
- [ ] Parallel trace validation
- [ ] Speculative execution
- [ ] Build graph visualization
- [ ] Cache size management and GC

### Additional Tool APIs
- [ ] tools/python.um
- [ ] tools/cmake.um
- [ ] tools/pkg_config.um
- [ ] tools/protobuf.um

---

## Priority Order

**Week 1 - Get it Working**:
1. Vendor dependencies (#1)
2. Bootstrap build structure (#3, #6)
3. Fix sys() array passing (#2)
4. Verify basic self-hosting (#7, #8)

**Week 2 - Core Features**:
5. Glob patterns (#4)
6. Integration tests (#9)
7. Directory tree dependencies (#5)
8. Tool system verification (#10)

**Week 3 - Polish**:
9. Documentation (#11, #12)
10. Self-hosting verification and testing
11. Performance testing and optimization

---

## Current Blockers

🔴 **BLOCKING**: No vendor dependencies → cannot build
🔴 **BLOCKING**: sys() doesn't work → cannot execute commands
🟡 **Important**: Build artifacts scattered → need clean separation

## Success Criteria

✅ Bootstrap build works from clean checkout
✅ sys() executes commands correctly
✅ Glob patterns work for common cases (*.c, **/*.h)
✅ Self-hosting: build → rebuild → rebuild → identical binaries
✅ Integration tests pass
✅ Basic documentation complete
