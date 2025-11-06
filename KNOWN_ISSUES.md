# Known Issues

## rebuild_sys() Array Passing Issue

**Status**: Under Investigation
**Severity**: Medium (workaround available)
**Affects**: Phase 1/2 implementation

### Description

The `rebuild_sys()` FFI function, which allows UMKA scripts to execute system commands, currently has an issue receiving string arrays from UMKA.

### Symptoms

When calling `rebuild_sys(args)` where `args` is a `[]str` array, the C FFI implementation receives:
- `argc = 0`
- `args_array->data = NULL`
- Array structure fields contain unexpected values

### Technical Details

The issue appears to be in how UMKA passes dynamic arrays to C FFI functions. The array structure is received but the `data` field is not properly initialized. Debugging shows:
- Array pointer is valid
- `umkaGetDynArrayLen()` returns 0
- Array `data` field is NULL
- Type and itemSize fields contain unexpected values

### Workarounds

1. Use alternative approaches that don't require `sys()`:
   - For simple builds, create stub targets that track dependencies
   - Use `rebuild_glob()` to find files (this works correctly)
   - Use `rebuild_depend_on()` for dependency management

2. Future: Implement tool modules that don't require array passing

### Root Cause Investigation

Possible causes being investigated:
1. UMKA may not allocate array data until first use
2. Array literal syntax (`[]str{"a", "b"}`) may not work as expected
3. The FFI parameter extraction may need a different approach
4. Array creation with `make()` also exhibits the same issue

### Impact

- Cannot execute external commands from UMKA scripts
- Does not affect core build system functionality:
  - ✅ Dependency tracking works
  - ✅ Caching works
  - ✅ Target registration works
  - ✅ Other FFI functions (glob, depend_on, log_*) work

### Next Steps

1. Review UMKA documentation for array passing conventions
2. Check if UMKA examples have working array parameter examples
3. Consider alternative FFI signatures that don't use arrays
4. May need to contact UMKA maintainers for guidance

### Related Files

- `src/umka_bridge.c:380-445` - rebuild_sys implementation
- `BUILD.um:25-40` - test_sys target (currently fails)
- `test/workspace/BUILD.um` - integration test (uses workaround)
