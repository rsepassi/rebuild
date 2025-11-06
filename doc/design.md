# Rebuild Build System Design & Implementation

## Executive Summary

“Rebuild” is a modern build system combining the correctness guarantees of constructive traces with the flexibility of dynamic dependencies. Built on a C11 core using libuv for async I/O and portability, with UMKA scripting for expressiveness. Features suspending recipe execution, content-addressed storage, and rich tool APIs. The system bootstraps via Make and becomes self-hosting, with all dependencies vendored for reproducibility.

## Core Architecture

### System Properties

**Scheduler**: Suspending (dynamic) - recipes can discover and request dependencies imperatively during execution  
**Rebuilder**: Constructive traces with early cutoff - records dependency hashes and values, stops early when intermediates unchanged  
**Storage**: Two-level content-addressed system using BLAKE2b hashes throughout

### Technology Stack

- **Core Runtime**: C11 - scheduler, storage, trace engine, hashing
- **I/O Layer**: libuv - async I/O, filesystem operations, process spawning (portable across platforms)
- **Recipe Language**: UMKA - embedded scripting with Go-like syntax and built-in fibers
- **Build Files**: UMKA scripts that register targets
- **Tool APIs**: UMKA modules providing high-level interfaces to build tools
- **Bootstrap**: GNU Make + POSIX shell - builds vendored dependencies and rebuild itself

### Bootstrap & Dependencies

**Vendored Libraries**:

```
vendor/
  libuv/          # Async I/O library (vendored)
  umka/           # Embedded scripting language (vendored)
  blake2/         # BLAKE2b reference implementation (vendored)
```

**Bootstrap Process**:

```bash
# Initial bootstrap with Make
$ make bootstrap

# Builds vendored deps, then rebuild itself
# After bootstrap, rebuild can build itself:
$ ./rebuild rebuild
```

## System Design

### Execution Model

```
workspace/
  source/           # Read-only source tree
  outputs/
    //foo/bar/      # Recipe's output directory (auto-tracked)
  tmp/              # Recipe-private temp directory (not cached)
  tools/            # Resolved tool dependencies with APIs
```

**Trust-Based Dependency Model**:

- No sandboxing - recipes can execute arbitrary commands
- Dependencies declared via API calls
- Output directory automatically tracked
- Discovered dependencies (e.g., headers) declared after discovery

### Recipe State Machine

```
PENDING → RUNNING → SUSPENDED → RUNNING → COMPLETE
                  ↘         ↗
                 (await deps)
```

When a recipe calls `depend_on()`, its UMKA fiber suspends, the scheduler processes the dependency, and resumes the fiber when ready.

### Storage Architecture

**BLAKE2b Hashing**:

- 256-bit hashes for all content
- Fast, cryptographically strong
- Used for files, traces, and request keys

**Layout**:

```
storage/
  traces/
    ab/cdef0123...  # Request → Trace mappings
  objects/
    12/3456789a...  # Content-addressed outputs
  tmp/              # Per-build temp directories
```

### Trace System

**Request Key Composition**:

```
RequestKey = BLAKE2b(
  recipe_bytecode    # The UMKA function code
  tool_module_code   # Tool API modules used
  target_name        # Fully qualified target
  config_flags       # Build configuration
  static_inputs[]    # Declared dependencies
)
```

**Trace Structure**:

- List of accessed dependencies with their hashes
- Output directory tree hash
- Performance metrics (CPU/wall time)
- Supports early cutoff on first changed dependency

## Implementation Design

### I/O Architecture with libuv

**All I/O operations are async and portable via libuv**:

```c
// File operations
typedef struct {
    uv_fs_t req;
    Hash* hash_result;
    void (*callback)(Hash* hash, void* data);
    void* user_data;
} HashFileRequest;

void hash_file_async(const char* path, void (*cb)(Hash*, void*), void* data) {
    HashFileRequest* req = malloc(sizeof(HashFileRequest));
    req->callback = cb;
    req->user_data = data;
    
    // Open file async
    uv_fs_open(uv_default_loop(), &req->req, path, O_RDONLY, 0, 
               on_file_open_for_hash);
}

// Process spawning
typedef struct {
    uv_process_t process;
    uv_pipe_t stdout_pipe;
    uv_pipe_t stderr_pipe;
    Buffer* stdout_buf;
    Buffer* stderr_buf;
    Recipe* waiting_recipe;
} ProcessRequest;

void spawn_process_async(const char** args, Recipe* recipe) {
    ProcessRequest* req = malloc(sizeof(ProcessRequest));
    req->waiting_recipe = recipe;
    
    // Setup pipes for stdout/stderr capture
    uv_pipe_init(uv_default_loop(), &req->stdout_pipe, 0);
    uv_pipe_init(uv_default_loop(), &req->stderr_pipe, 0);
    
    // Spawn process
    uv_process_options_t options = {0};
    options.args = (char**)args;
    options.stdio_count = 3;
    options.stdio = stdio;
    options.exit_cb = on_process_exit;
    
    uv_spawn(uv_default_loop(), &req->process, &options);
}

// Directory watching for incremental builds
void watch_directory(const char* path) {
    uv_fs_event_t* watcher = malloc(sizeof(uv_fs_event_t));
    uv_fs_event_init(uv_default_loop(), watcher);
    uv_fs_event_start(watcher, on_file_change, path, UV_FS_EVENT_RECURSIVE);
}
```

**Event Loop Integration**:

```c
// Main build loop
int main(int argc, char** argv) {
    // Initialize libuv
    uv_loop_t* loop = uv_default_loop();
    
    // Setup thread pool for CPU-bound work
    uv_cpu_info_t* cpu_infos;
    int cpu_count;
    uv_cpu_info(&cpu_infos, &cpu_count);
    
    // Create worker threads for recipe execution
    for (int i = 0; i < cpu_count; i++) {
        uv_work_t* work = malloc(sizeof(uv_work_t));
        work->data = create_worker_context();
        uv_queue_work(loop, work, worker_thread_main, NULL);
    }
    
    // Start build
    queue_target(argv[1]);
    
    // Run event loop
    return uv_run(loop, UV_RUN_DEFAULT);
}
```

### Core Data Structures

```c
// Hash type for BLAKE2b-256
typedef struct {
    uint8_t bytes[32];
} Hash;

// Recipe execution state
struct RecipeState {
    fiber_t* fiber;         // UMKA fiber handle
    Hash request_key;       // Cache key
    Set* declared_deps;     // Dependencies seen so far
    Set* pending_deps;      // Awaiting these
    char* output_dir;       // outputs/target/
    char* temp_dir;         // tmp/target/
};

// Cached trace
struct Trace {
    Hash request_key;
    Hash* dep_hashes;       // Content hashes
    char** dep_paths;       // For debugging
    Hash output_tree_hash;  // Result hash
    uint64_t cpu_time_ms;
    uint64_t wall_time_ms;
};

// Tool module tracking
struct ToolModule {
    char* name;             // "clang", "python", etc
    char* module_path;      // "tools/clang.um"
    void* instance;         // UMKA object
    Hash module_hash;       // Source code hash
    Hash binary_hash;       // Tool binary hash
};
```

### Scheduler Implementation

**Async Event-Driven Architecture**:

- libuv event loop coordinates all I/O
- Thread pool for CPU-bound UMKA execution
- Non-blocking file operations
- Async process spawning for `sys()` calls

**Execution Flow**:

1. Recipe starts in UMKA fiber (thread pool)
1. Calls `depend_on(target)` → fiber yields
1. Request queued to main loop via uv_async
1. Main loop checks cache (async file I/O)
1. If miss, schedules dependency (non-blocking)
1. When dependency ready, queues resume to thread pool
1. Fiber resumes with result path

**Core Scheduler Loop**:

```c
typedef struct {
    uv_loop_t* loop;
    uv_async_t scheduler_async;  // Thread pool → main loop
    Queue* ready_recipes;         // Ready to run
    Map* waiting_recipes;         // target → Recipe[]
    ThreadPool* workers;          // UMKA execution threads
} Scheduler;

// Main event loop
void scheduler_run(const char* target) {
    Scheduler* sched = scheduler_init();
    
    // Queue initial target
    Recipe* root = create_recipe(target);
    queue_ready(sched, root);
    
    // Process until complete
    uv_run(sched->loop, UV_RUN_DEFAULT);
}

// Handle recipe dependency request (from thread pool)
void on_depend_request(uv_async_t* handle) {
    DependRequest* req = dequeue_depend_request();
    
    // Check cache asynchronously
    check_cache_async(req->target, on_cache_checked, req);
}

// Cache check complete (in main loop)
void on_cache_checked(CacheResult* result, void* data) {
    DependRequest* req = (DependRequest*)data;
    
    if (result->hit) {
        // Resume recipe immediately
        queue_resume(req->recipe, result->path);
    } else {
        // Create dependency recipe
        Recipe* dep = create_recipe(req->target);
        add_waiter(dep, req->recipe);
        queue_ready(dep);
    }
}
```

### C/UMKA Bridge

**FFI Surface with Async Support**:

```c
// Core scheduling - async operations that yield fiber
void* build_depend_on(const char* target) {
    // Yields fiber, returns when dependency ready
    return scheduler_request_dependency(current_fiber(), target);
}

void* build_depend_on_all(const char** targets, int count) {
    // Batched dependency request
    return scheduler_request_dependencies(current_fiber(), targets, count);
}

// Tool management  
void* build_load_tool(const char* name, const char* opts) {
    // Async load tool module and binary
    return tool_manager_load(name, opts);
}

// System execution - async process spawning
int build_sys(const char** args, int argc, void* opts) {
    // Spawn process via libuv, yield fiber until complete
    ProcessResult result;
    uv_process_spawn_async(args, current_fiber(), &result);
    fiber_yield();  // Resume when process completes
    return result.exit_code;
}

// File operations - async I/O
void* build_hash_file(const char* path) {
    // Async file hashing
    Hash hash;
    uv_fs_hash_async(path, current_fiber(), &hash);
    fiber_yield();  // Resume when hash complete
    return hash_to_string(&hash);
}

// Caching
void* build_cached_run(const char* key, void* inputs, void* func) {
    // Check cache with async I/O
    return cache_manager_run(key, inputs, func);
}

// Dependency registration (synchronous - just records)
void build_register_dep(const char* path) {
    recipe_add_dependency(current_recipe(), path);
}
```

**UMKA → libuv Integration**:

```c
// When UMKA calls sys(), we use libuv
typedef struct {
    Recipe* recipe;
    fiber_t* fiber;
    ProcessResult* result;
    uv_process_t process;
    uv_pipe_t stdout_pipe;
    uv_pipe_t stderr_pipe;
    Buffer stdout_buf;
    Buffer stderr_buf;
} SysRequest;

int umka_sys_impl(const char** args, SysOpts* opts) {
    SysRequest* req = malloc(sizeof(SysRequest));
    req->recipe = current_recipe();
    req->fiber = current_fiber();
    
    // Setup process options
    uv_process_options_t options = {0};
    options.args = (char**)args;
    options.cwd = opts->cwd;
    options.env = opts->env;
    
    // Setup pipes for output capture
    uv_pipe_init(uv_default_loop(), &req->stdout_pipe, 0);
    uv_pipe_init(uv_default_loop(), &req->stderr_pipe, 0);
    
    options.stdio_count = 3;
    options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    options.stdio[1].data.stream = (uv_stream_t*)&req->stdout_pipe;
    options.stdio[2].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    options.stdio[2].data.stream = (uv_stream_t*)&req->stderr_pipe;
    
    // Spawn process
    options.exit_cb = on_sys_complete;
    uv_spawn(uv_default_loop(), &req->process, &options);
    
    // Start reading output
    uv_read_start((uv_stream_t*)&req->stdout_pipe, alloc_buffer, on_stdout);
    uv_read_start((uv_stream_t*)&req->stderr_pipe, alloc_buffer, on_stderr);
    
    // Yield fiber until process completes
    fiber_yield();
    
    // Return result after resume
    return req->result->exit_code;
}

void on_sys_complete(uv_process_t* process, int64_t exit_status, 
                    int term_signal) {
    SysRequest* req = container_of(process, SysRequest, process);
    
    req->result->exit_code = exit_status;
    req->result->stdout = buffer_to_string(&req->stdout_buf);
    req->result->stderr = buffer_to_string(&req->stderr_buf);
    
    // Queue fiber resume in thread pool
    queue_fiber_resume(req->fiber, req->result);
    
    free(req);
}
```

## API Design

### Core Recipe API

```umka
// Request dependency and get output path
fn depend_on(target: str): str

// Batch dependency request
fn depend_on_all(targets: []str): []str  

// Get tool with API
fn deptool(name: str, opts: ToolOpts = {}): Tool

// Execute system command
fn sys(args: []str, opts: SysOpts = {}): Result

// Register discovered dependency
fn register_dep(path: str)

// Sub-recipe caching
fn cached_run(key: str, inputs: any, fn: fn()): any

// Parse dependency files
fn parse_depfile(path: str): Depfile

// File pattern matching
fn glob(pattern: str): []str
```

### Tool API System

**Tool Resolution**:

```umka
// Get tool with rich API
cc := deptool("clang")

// High-level usage
result := cc.compile("foo.c", {
    includes: ["include/"],
    flags: ["-O2", "-Wall"],
    dep_tracking: true  // Auto-handle depfiles
})

// Or raw access
sys([cc.bin, "-c", "foo.c"])
```

**Tool Module Example**:

```umka
// tools/clang.um - This file is hashed as dependency!
struct ClangCompiler {
    bin: str           // Path to clang binary
    version: str       // Detected version
    target: str        // Target triple
    sysroot: str       // System root
}

fn (c: ClangCompiler) compile(src: str, opts: CompileOpts = {}): CompileResult {
    args := [c.bin, "-c", src]
    
    // Add includes
    for inc in opts.includes {
        args.append("-I" + inc)
    }
    
    // Generate depfile
    depfile := opts.output ?? src.replace_ext(".o") + ".d"
    args.extend(["-MD", "-MF", depfile])
    
    // Add user flags
    args.extend(opts.flags)
    
    // Add output
    output := opts.output ?? src.replace_ext(".o")
    args.extend(["-o", output])
    
    // Execute
    res := sys(args)
    
    // Auto-register discovered dependencies
    if res.ok && opts.dep_tracking {
        deps := parse_depfile(depfile)
        register_deps(deps.headers)
    }
    
    return {
        output: output,
        deps_found: deps.headers,
        result: res
    }
}

fn (c: ClangCompiler) link(objs: []str, opts: LinkOpts = {}): LinkResult {
    args := [c.bin] + objs + ["-o", opts.output]
    
    for lib in opts.libs {
        args.append("-l" + lib)
    }
    
    for path in opts.lib_paths {
        args.append("-L" + path)  
    }
    
    args.extend(opts.flags)
    
    return sys(args)
}

fn (c: ClangCompiler) static_lib(objs: []str, output: str): Result {
    ar := deptool("ar")
    return sys([ar.bin, "rcs", output] + objs)
}
```

### BUILD File Structure

```umka
// BUILD.um
fn register_targets() {
    // Library target
    target("lib:foo", fn() {
        cc := deptool("clang")
        
        // Compile all C files
        srcs := glob("*.c")
        objs := []
        
        for src in srcs {
            result := cc.compile(src, {
                includes: ["include/"],
                flags: ["-O2", "-fPIC"],
                dep_tracking: true
            })
            objs.append(result.output)
        }
        
        // Create static library
        cc.static_lib(objs, "libfoo.a")
        
        // Also create shared library
        cc.link(objs, {
            output: "libfoo.so",
            flags: ["-shared"]
        })
    })
    
    // Binary target
    target("bin:main", fn() {
        cc := deptool("clang")
        lib := depend_on("lib:foo")
        
        main := cc.compile("main.c", {
            includes: ["include/"],
            dep_tracking: true
        })
        
        cc.link([main.output], {
            output: "main",
            libs: ["foo"],
            lib_paths: [lib]
        })
    })
    
    // Test target
    target("test:unit", fn() {
        test_runner := deptool("test-runner")
        bin := depend_on("bin:main")
        
        test_runner.run([
            bin + "/main",
            "--test-mode"
        ])
    })
}
```

## Standard Tool APIs

### Python Integration

```umka
// tools/python.um
struct PythonEnv {
    bin: str
    version: str
    site_packages: str
}

fn (p: PythonEnv) run(script: str, opts: PyOpts = {}): Result {
    script_path := depend_on(script)
    
    // Handle requirements
    if opts.requirements {
        reqs := depend_on(opts.requirements)
        sys([p.bin, "-m", "pip", "install", "-r", reqs])
    }
    
    // Run script
    return sys([p.bin, script_path] + opts.args, {
        env: opts.env
    })
}

fn (p: PythonEnv) module(name: str, opts: PyOpts = {}): Result {
    return sys([p.bin, "-m", name] + opts.args)
}
```

### CMake Integration

```umka
// tools/cmake.um
struct CMake {
    bin: str
    generator: str
}

fn (c: CMake) configure(src: str, opts: CMakeOpts = {}): Result {
    src_path := depend_on(src)
    
    args := [c.bin, src_path]
    args.extend(["-G", c.generator])
    
    // Add definitions
    for k, v in opts.defines {
        args.append("-D" + k + "=" + v)
    }
    
    // Register CMakeLists.txt files
    cmakelists := glob(src_path + "/**/CMakeLists.txt")
    register_deps(cmakelists)
    
    return sys(args)
}

fn (c: CMake) build(target: str = "", opts: BuildOpts = {}): Result {
    args := [c.bin, "--build", "."]
    
    if target {
        args.extend(["--target", target])
    }
    
    if opts.parallel {
        args.extend(["-j", str(opts.parallel)])
    }
    
    return sys(args)
}
```

### Package Config

```umka
// tools/pkg_config.um
struct PkgConfig {
    bin: str
    search_paths: []str
}

fn (p: PkgConfig) get(pkg: str): PkgInfo {
    // Get compile flags
    cflags_res := sys([p.bin, "--cflags", pkg])
    if !cflags_res.ok {
        fail("Package not found: " + pkg)
    }
    
    // Get link flags
    libs_res := sys([p.bin, "--libs", pkg])
    
    // Find and register .pc file
    pc_path_res := sys([p.bin, "--variable=pcfiledir", pkg])
    pc_file := pc_path_res.stdout.trim() + "/" + pkg + ".pc"
    register_dep(pc_file)
    
    return {
        cflags: split_args(cflags_res.stdout),
        libs: split_args(libs_res.stdout),
        version: sys([p.bin, "--modversion", pkg]).stdout.trim()
    }
}

fn (p: PkgConfig) available(pkg: str): bool {
    res := sys([p.bin, "--exists", pkg])
    return res.ok
}
```

## Higher-Level Build Patterns

### Language Builders

```umka
// builders/c_binary.um
fn c_binary(name: str, srcs: []str, opts: CBinaryOpts = {}) {
    cc := deptool(opts.compiler ?? "clang")
    
    // Compile sources
    objs := []
    for src in srcs {
        result := cc.compile(src, {
            includes: opts.includes,
            flags: opts.cflags,
            defines: opts.defines,
            dep_tracking: true
        })
        objs.append(result.output)
    }
    
    // Link binary
    cc.link(objs, {
        output: name,
        libs: opts.libs,
        lib_paths: opts.lib_paths,
        flags: opts.ldflags
    })
}

fn c_library(name: str, srcs: []str, opts: CLibraryOpts = {}) {
    cc := deptool(opts.compiler ?? "clang")
    
    // Compile with PIC for shared library
    objs := []
    for src in srcs {
        result := cc.compile(src, {
            includes: opts.includes,
            flags: opts.cflags + ["-fPIC"],
            defines: opts.defines,
            dep_tracking: true
        })
        objs.append(result.output)
    }
    
    // Create outputs based on type
    if opts.static {
        cc.static_lib(objs, "lib" + name + ".a")
    }
    
    if opts.shared {
        cc.link(objs, {
            output: "lib" + name + ".so",
            flags: ["-shared"] + opts.ldflags
        })
    }
}
```

### Test Runners

```umka
// builders/test.um
fn test_suite(name: str, tests: []str, opts: TestOpts = {}) {
    runner := deptool(opts.runner ?? "test-runner")
    
    results := []
    for test in tests {
        test_bin := depend_on(test)
        
        result := runner.run(test_bin, {
            timeout: opts.timeout ?? 30,
            env: opts.env,
            args: opts.args
        })
        
        results.append(result)
        
        if !result.ok && !opts.continue_on_failure {
            fail("Test failed: " + test)
        }
    }
    
    // Generate report
    generate_test_report(name, results)
}
```

## Debugging & Observability

### Trace Inspection

```umka
// Query why something rebuilt
fn explain_rebuild(target: str): Explanation {
    trace := get_last_trace(target)
    
    changes := []
    for dep in trace.deps {
        current := hash_file(dep.path)
        if current != dep.hash {
            changes.append({
                path: dep.path,
                old_hash: dep.hash,
                new_hash: current
            })
        }
    }
    
    return {target: target, changes: changes}
}
```

### Build Profiling

```umka
// Profile build performance
fn profile_build(targets: []str): Profile {
    start := time_now()
    
    // Track per-recipe metrics
    metrics := {}
    
    for target in targets {
        recipe_start := time_now()
        depend_on(target)
        recipe_time := time_since(recipe_start)
        
        metrics[target] = {
            wall_time: recipe_time,
            cache_hit: was_cached(target),
            dependencies: count_dependencies(target)
        }
    }
    
    return {
        total_time: time_since(start),
        recipes: metrics,
        parallelism: calculate_parallelism(metrics)
    }
}
```

## Configuration

### Build Configuration

```umka
// config.um
struct BuildConfig {
    // Optimization level
    opt_level: str = "2"
    
    // Debug info
    debug: bool = false
    
    // Target architecture
    target: str = host_arch()
    
    // Toolchain selection
    toolchain: str = "system"
    
    // Feature flags
    features: Set[str] = {}
}

// Configs affect request keys
fn with_config(cfg: BuildConfig, fn: fn()) {
    set_build_config(cfg)
    fn()
}
```

### Cross-Compilation

```umka
// Cross-compilation support
fn cross_build(target_arch: str) {
    cfg := BuildConfig{
        target: target_arch,
        toolchain: "cross-" + target_arch
    }
    
    with_config(cfg, fn() {
        depend_on("//app:main")
    })
}
```

## Cache Management

### Cache Key Computation

The request key incorporates:

1. Recipe function bytecode
1. Tool module source code (APIs can affect behavior)
1. Target name and configuration
1. Static dependencies
1. Tool binary hashes

This ensures any change that could affect output triggers rebuild.

### Early Cutoff Optimization

Traces validate dependencies in order, stopping at first mismatch:

```c
bool trace_valid(Trace* trace) {
    for (int i = 0; i < trace->dep_count; i++) {
        Hash current = hash_path(trace->dep_paths[i]);
        if (!hash_equal(current, trace->dep_hashes[i])) {
            return false;  // Early exit
        }
    }
    return true;
}
```

### Storage Management

- Traces stored by request key
- Output trees stored by content hash
- Automatic garbage collection of unreferenced objects
- Temp directories cleared per build

## Error Handling

### Recipe Failures

- Output and temp directories preserved for debugging
- Full stdout/stderr captured from `sys()` calls
- Stack traces include UMKA and dependency chain
- Deterministic error messages

### Non-Determinism Detection

Optional mode to detect non-deterministic builds:

```umka
fn verify_deterministic(target: str) {
    // Build twice with same inputs
    out1 := depend_on(target)
    clear_cache(target)
    out2 := depend_on(target)
    
    // Compare outputs
    if hash_tree(out1) != hash_tree(out2) {
        report_non_determinism(target, out1, out2)
    }
}
```

## Migration Path

### Phase 1: Core System

- Basic scheduler with suspend/resume
- Memory-only caching
- Simple file dependencies
- Manual dependency declaration

### Phase 2: Persistence

- On-disk trace storage
- Content-addressed outputs
- Tool module system
- Standard tool APIs

### Phase 3: Advanced Features

- Glob patterns
- Directory tree dependencies
- Remote caching
- Distributed builds

### Phase 4: Optimization

- Incremental linking
- Parallel trace validation
- Speculative execution
- Cloud storage backend

## Bootstrap & Self-Hosting

### Initial Bootstrap via Make

```makefile
# Makefile - Bootstrap build system
CC = cc
CFLAGS = -O2 -Wall -Ivendor/libuv/include -Ivendor/umka/src

# Vendored library builds
vendor/libuv/libuv.a:
	cd vendor/libuv && ./autogen.sh && ./configure && make
	
vendor/umka/libumka.a:
	cd vendor/umka && make static

vendor/blake2/blake2b.o: vendor/blake2/blake2b.c
	$(CC) $(CFLAGS) -c $< -o $@

# Main rebuild binary
REBUILD_SRCS = src/main.c src/scheduler.c src/storage.c src/trace.c
REBUILD_OBJS = $(REBUILD_SRCS:.c=.o)

rebuild: $(REBUILD_OBJS) vendor/libuv/libuv.a vendor/umka/libumka.a vendor/blake2/blake2b.o
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lm

# Bootstrap target
bootstrap: rebuild
	@echo "Bootstrap complete. Now rebuild can build itself:"
	@echo "  ./rebuild //rebuild"

clean:
	rm -f rebuild $(REBUILD_OBJS)
	cd vendor/libuv && make clean
	cd vendor/umka && make clean
```

### Self-Hosted Build

Once bootstrapped, rebuild builds itself using UMKA recipes:

```umka
// BUILD.um in root directory
fn register_targets() {
    // The rebuild binary itself
    target("rebuild", fn() {
        cc := deptool("clang")
        
        // Build vendored libuv
        libuv := cached_run("libuv", {}, fn() {
            // Configure and build libuv
            src := "vendor/libuv"
            sys(["./autogen.sh"], {cwd: src})
            sys(["./configure", "--disable-shared"], {cwd: src})
            sys(["make", "-j8"], {cwd: src})
            return src + "/.libs/libuv.a"
        })
        
        // Build vendored UMKA
        umka := cached_run("umka", {}, fn() {
            src := "vendor/umka"
            sys(["make", "static", "-j8"], {cwd: src})
            return src + "/libumka.a"
        })
        
        // Build BLAKE2b
        blake2 := cc.compile("vendor/blake2/blake2b.c", {
            flags: ["-O3"]
        })
        
        // Build rebuild sources
        srcs := glob("src/*.c")
        objs := []
        
        for src in srcs {
            result := cc.compile(src, {
                includes: [
                    "vendor/libuv/include",
                    "vendor/umka/src",
                    "vendor/blake2"
                ],
                flags: ["-O2", "-Wall"],
                dep_tracking: true
            })
            objs.append(result.output)
        }
        
        // Link everything
        cc.link(objs + [libuv, umka, blake2.output], {
            output: "rebuild",
            libs: ["pthread", "m", "dl"],
            flags: ["-rdynamic"]  // For UMKA FFI
        })
    })
}
```

### Vendored Dependencies Structure

```
rebuild/
  Makefile           # Bootstrap makefile
  BUILD.um           # Self-hosted build definition
  
  src/               # Rebuild source code
    main.c           # Entry point & arg parsing
    scheduler.c      # Recipe scheduling with libuv
    storage.c        # Content-addressed storage
    trace.c          # Trace validation & caching
    bridge.c         # UMKA FFI bindings
    
  vendor/            # Vendored dependencies (in git)
    libuv/           # Full libuv source tree
      autogen.sh
      configure
      src/
      include/
      
    umka/            # Full UMKA source tree  
      src/
      Makefile
      
    blake2/          # Just the needed files
      blake2b.c
      blake2b.h
      
  tools/             # Tool API modules
    clang.um
    python.um
    cmake.um
    
  builders/          # High-level builders
    c_binary.um
    c_library.um
    test.um
```

### Platform Portability via libuv

**File Operations**:

```c
// Portable file hashing
void hash_file_portable(const char* path, Hash* out_hash) {
    uv_fs_t open_req;
    int fd = uv_fs_open(NULL, &open_req, path, O_RDONLY, 0, NULL);
    
    if (fd < 0) {
        // Handle error
        return;
    }
    
    blake2b_state state;
    blake2b_init(&state, 32);
    
    char buffer[8192];
    uv_buf_t iov = uv_buf_init(buffer, sizeof(buffer));
    uv_fs_t read_req;
    
    while (1) {
        int nread = uv_fs_read(NULL, &read_req, fd, &iov, 1, -1, NULL);
        if (nread <= 0) break;
        blake2b_update(&state, buffer, nread);
    }
    
    blake2b_final(&state, out_hash->bytes, 32);
    
    uv_fs_close(NULL, &open_req, fd, NULL);
}

// Portable process execution
int run_command_portable(const char** args, ProcessResult* result) {
    uv_process_options_t options = {0};
    options.args = (char**)args;
    options.exit_cb = on_exit;
    
    // Capture output portably
    uv_pipe_t stdout_pipe, stderr_pipe;
    uv_pipe_init(uv_default_loop(), &stdout_pipe, 0);
    uv_pipe_init(uv_default_loop(), &stderr_pipe, 0);
    
    options.stdio_count = 3;
    options.stdio = stdio;
    options.stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    options.stdio[1].data.stream = (uv_stream_t*)&stdout_pipe;
    options.stdio[2].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    options.stdio[2].data.stream = (uv_stream_t*)&stderr_pipe;
    
    uv_process_t process;
    int r = uv_spawn(uv_default_loop(), &process, &options);
    
    // Wait for completion
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    
    return process.exit_signal ? -1 : process.exit_status;
}
```

### Build Performance via Async I/O

**Parallel File Hashing**:

```c
// Hash multiple files concurrently
typedef struct {
    int count;
    int completed;
    Hash* hashes;
    void (*callback)(Hash* combined);
} MultiHashRequest;

void hash_files_parallel(const char** paths, int count, 
                        void (*cb)(Hash*)) {
    MultiHashRequest* req = malloc(sizeof(MultiHashRequest));
    req->count = count;
    req->completed = 0;
    req->hashes = calloc(count, sizeof(Hash));
    req->callback = cb;
    
    for (int i = 0; i < count; i++) {
        hash_file_async(paths[i], on_single_hash_done, req);
    }
}

void on_single_hash_done(Hash* hash, void* data) {
    MultiHashRequest* req = (MultiHashRequest*)data;
    // ... handle completion, combine hashes when all done
}
```

**Non-Blocking Recipe Execution**:

```c
// Recipes never block the event loop
void execute_recipe(Recipe* recipe) {
    // Queue to thread pool for UMKA execution
    uv_work_t* work = malloc(sizeof(uv_work_t));
    work->data = recipe;
    
    uv_queue_work(uv_default_loop(), work,
                  run_recipe_thread,      // Runs in thread pool
                  on_recipe_complete);    // Runs in main loop
}

void run_recipe_thread(uv_work_t* work) {
    Recipe* recipe = (Recipe*)work->data;
    // Run UMKA fiber - this can block
    umka_resume_fiber(recipe->fiber);
}

void on_recipe_complete(uv_work_t* work, int status) {
    Recipe* recipe = (Recipe*)work->data;
    // Back in main thread - handle completion
    notify_dependents(recipe);
}
```

## Summary

“Rebuild” delivers:

- **Correctness**: Constructive traces ensure sound incremental builds
- **Flexibility**: Dynamic dependencies via suspending execution
- **Ergonomics**: Rich tool APIs instead of raw commands
- **Performance**: Async I/O via libuv, parallel execution
- **Portability**: libuv provides consistent behavior across platforms
- **Self-Hosting**: Bootstraps with Make, then builds itself

The key innovations:

1. **Tool API modules as dependencies** - when you improve how you use a tool, affected targets automatically rebuild
1. **Async everything** - libuv ensures no blocking I/O, maximizing parallelism
1. **Trust-based simplicity** - no complex sandboxing, recipes declare dependencies
1. **Vendored stability** - all dependencies included, ensuring reproducible builds

The system bootstraps itself through Make initially, then becomes self-hosting where “rebuild” can build itself using its own build rules written in UMKA. This proves the system’s capability while keeping the bootstrap process simple and portable.
