# Vendored Dependencies

This directory contains vendored third-party dependencies for the Rebuild build system. All dependencies are copied directly into the repository (not git submodules) to ensure reproducible builds.

## Dependencies

### libuv v1.48.0
**Location**: `vendor/libuv/`
**Source**: https://github.com/libuv/libuv/releases/tag/v1.48.0
**License**: MIT License
**Purpose**: Cross-platform asynchronous I/O library

libuv provides:
- Event loop for async operations
- File system operations
- Process spawning
- Network I/O
- Thread pool for CPU-bound work

### UMKA v1.5
**Location**: `vendor/umka/`
**Source**: https://github.com/vtereshkov/umka-lang/releases/tag/v1.5
**License**: BSD-2-Clause License
**Purpose**: Embedded scripting language with Go-like syntax

UMKA provides:
- Lightweight embeddable interpreter
- Fiber-based coroutines for suspending execution
- C FFI for integrating with Rebuild
- Fast compilation and execution

### BLAKE2 (reference implementation)
**Location**: `vendor/blake2/`
**Source**: https://github.com/BLAKE2/BLAKE2 (master branch, ref/ directory)
**License**: CC0 1.0 Universal (Public Domain)
**Purpose**: Fast cryptographic hash function

BLAKE2 provides:
- BLAKE2b-256 hashing for content addressing
- Faster than SHA-2, more secure than MD5/SHA-1
- Reference implementation in portable C

## Building

Vendored dependencies are built by the bootstrap build system:

```bash
make -C bootstrap
```

This will:
1. Build libuv using its autoconf-based build system
2. Build UMKA using its Makefile
3. Compile BLAKE2 reference implementation
4. Link everything into the rebuild binary

## Updating Dependencies

To update a vendored dependency:

1. Download the new version
2. Replace the contents of the vendor directory
3. Update this README.md with the new version number
4. Test that everything compiles and passes tests
5. Commit the changes

Example:
```bash
cd /tmp
wget https://github.com/libuv/libuv/archive/refs/tags/vX.Y.Z.tar.gz
tar -xzf vX.Y.Z.tar.gz
rm -rf /path/to/rebuild/vendor/libuv/*
cp -r libuv-X.Y.Z/* /path/to/rebuild/vendor/libuv/
# Update this README.md
# Test build
# Commit
```

## Licenses

All vendored dependencies use permissive open-source licenses:
- libuv: MIT License (allows commercial use, modification, distribution)
- UMKA: BSD-2-Clause License (allows commercial use, modification, distribution)
- BLAKE2: CC0 1.0 (Public Domain, no restrictions)

See individual LICENSE files in each vendor directory for full license text.
