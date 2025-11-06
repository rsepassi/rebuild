#include "umka_bridge.h"
#include "hash.h"
#include "buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <pthread.h>

// Include UMKA API
#include "umka_api.h"

// Thread-local storage for UMKA context
static pthread_key_t tls_context_key;
static pthread_once_t tls_init_once = PTHREAD_ONCE_INIT;

// Global callbacks for scheduler integration
static UmkaBridgeCallbacks g_callbacks = {0};

// Thread-local storage initialization
static void tls_init(void) {
    pthread_key_create(&tls_context_key, NULL);
}

// Initialize UMKA bridge
RebuildError umka_bridge_init(void) {
    // Initialize thread-local storage
    pthread_once(&tls_init_once, tls_init);

    LOG_DEBUG("UMKA bridge initialized");
    return REBUILD_OK;
}

// Cleanup UMKA bridge
void umka_bridge_cleanup(void) {
    // Nothing to cleanup currently
    LOG_DEBUG("UMKA bridge cleanup complete");
}

// Set thread-local context
void umka_bridge_set_context(Recipe* recipe, Scheduler* scheduler) {
    pthread_once(&tls_init_once, tls_init);

    UmkaContext* ctx = rebuild_malloc(sizeof(UmkaContext));
    ctx->current_recipe = recipe;
    ctx->scheduler = scheduler;
    ctx->umka = NULL;  // Will be set when script is loaded

    pthread_setspecific(tls_context_key, ctx);
    LOG_DEBUG("Set UMKA context for recipe: %s", recipe ? recipe->target_name : "NULL");
}

// Get thread-local context
UmkaContext* umka_bridge_get_context(void) {
    pthread_once(&tls_init_once, tls_init);
    return (UmkaContext*)pthread_getspecific(tls_context_key);
}

// Clear thread-local context
void umka_bridge_clear_context(void) {
    UmkaContext* ctx = umka_bridge_get_context();
    if (ctx) {
        rebuild_free(ctx);
        pthread_setspecific(tls_context_key, NULL);
    }
}

// Set bridge callbacks
void umka_bridge_set_callbacks(const UmkaBridgeCallbacks* callbacks) {
    if (callbacks) {
        g_callbacks = *callbacks;
        LOG_DEBUG("UMKA bridge callbacks configured");
    }
}

// Helper function to read file contents into a string
static char* read_file_contents(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) {
        LOG_ERROR("Failed to open file: %s", path);
        return NULL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (size < 0) {
        LOG_ERROR("Failed to get file size: %s", path);
        fclose(file);
        return NULL;
    }

    // Allocate buffer for file contents
    char* buffer = rebuild_malloc(size + 1);
    if (!buffer) {
        LOG_ERROR("Failed to allocate buffer for file: %s", path);
        fclose(file);
        return NULL;
    }

    // Read file contents
    size_t read_size = fread(buffer, 1, size, file);
    fclose(file);

    if (read_size != (size_t)size) {
        LOG_ERROR("Failed to read complete file: %s", path);
        rebuild_free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    return buffer;
}

// Load and compile UMKA script
Umka* umka_load_script(const char* path) {
    if (!path) {
        LOG_ERROR("Cannot load UMKA script: NULL path");
        return NULL;
    }

    LOG_DEBUG("Loading UMKA script: %s", path);

    // Allocate UMKA instance
    Umka* umka = umkaAlloc();
    if (!umka) {
        LOG_ERROR("Failed to allocate UMKA instance");
        return NULL;
    }

    // Initialize UMKA with empty source (this is required for external functions to work)
    // We provide an empty string as sourceString and NULL fileName
    // Parameters: fileName, sourceString, stackSize, reserved, argc, argv,
    //            fileSystemEnabled, implLibsEnabled, warningCallback
    if (!umkaInit(umka, NULL, "", 1024 * 1024, NULL, 0, NULL, true, true, NULL)) {
        UmkaError* error = umkaGetError(umka);
        LOG_ERROR("Failed to initialize UMKA: %s (line %d)",
                  error->msg, error->line);
        umkaFree(umka);
        return NULL;
    }

    // Register FFI functions BEFORE loading the module
    if (!umkaAddFunc(umka, "rebuild_depend_on",
                     (UmkaExternFunc)umka_ffi_rebuild_depend_on)) {
        LOG_ERROR("Failed to register rebuild_depend_on FFI function");
        umkaFree(umka);
        return NULL;
    }

    if (!umkaAddFunc(umka, "rebuild_sys",
                     (UmkaExternFunc)umka_ffi_rebuild_sys)) {
        LOG_ERROR("Failed to register rebuild_sys FFI function");
        umkaFree(umka);
        return NULL;
    }

    if (!umkaAddFunc(umka, "rebuild_register_dep",
                     (UmkaExternFunc)umka_ffi_rebuild_register_dep)) {
        LOG_ERROR("Failed to register rebuild_register_dep FFI function");
        umkaFree(umka);
        return NULL;
    }

    if (!umkaAddFunc(umka, "rebuild_glob",
                     (UmkaExternFunc)umka_ffi_rebuild_glob)) {
        LOG_ERROR("Failed to register rebuild_glob FFI function");
        umkaFree(umka);
        return NULL;
    }

    if (!umkaAddFunc(umka, "rebuild_hash_file",
                     (UmkaExternFunc)umka_ffi_rebuild_hash_file)) {
        LOG_ERROR("Failed to register rebuild_hash_file FFI function");
        umkaFree(umka);
        return NULL;
    }

    if (!umkaAddFunc(umka, "rebuild_log_info",
                     (UmkaExternFunc)umka_ffi_rebuild_log_info)) {
        LOG_ERROR("Failed to register rebuild_log_info FFI function");
        umkaFree(umka);
        return NULL;
    }

    if (!umkaAddFunc(umka, "rebuild_log_debug",
                     (UmkaExternFunc)umka_ffi_rebuild_log_debug)) {
        LOG_ERROR("Failed to register rebuild_log_debug FFI function");
        umkaFree(umka);
        return NULL;
    }

    if (!umkaAddFunc(umka, "rebuild_register_target",
                     (UmkaExternFunc)umka_ffi_rebuild_register_target)) {
        LOG_ERROR("Failed to register rebuild_register_target FFI function");
        umkaFree(umka);
        return NULL;
    }

    // Read the file contents
    char* source = read_file_contents(path);
    if (!source) {
        LOG_ERROR("Failed to read UMKA script file: %s", path);
        umkaFree(umka);
        return NULL;
    }

    // Load the module from source string (this is required for external functions)
    if (!umkaAddModule(umka, path, source)) {
        UmkaError* error = umkaGetError(umka);
        LOG_ERROR("Failed to add UMKA module %s: %s (line %d)",
                  path, error->msg, error->line);
        rebuild_free(source);
        umkaFree(umka);
        return NULL;
    }

    // Free the source buffer
    rebuild_free(source);

    // Compile the script
    if (!umkaCompile(umka)) {
        UmkaError* error = umkaGetError(umka);
        LOG_ERROR("Failed to compile UMKA script %s: %s (line %d)",
                  path, error->msg, error->line);
        umkaFree(umka);
        return NULL;
    }

    LOG_INFO("Successfully loaded and compiled UMKA script: %s", path);

    // Store UMKA instance in thread-local context
    UmkaContext* ctx = umka_bridge_get_context();
    if (ctx) {
        ctx->umka = umka;
    }

    return umka;
}

// Get hash of UMKA script
RebuildError umka_get_script_hash(const char* path, Hash* out_hash) {
    if (!path || !out_hash) {
        return REBUILD_ERROR_PARSE;
    }

    if (!hash_file(path, out_hash)) {
        LOG_ERROR("Failed to hash UMKA script: %s", path);
        return REBUILD_ERROR_HASH;
    }

    return REBUILD_OK;
}

// Create fiber for recipe execution
UmkaFiber umka_create_fiber(Umka* umka, const char* function_name) {
    if (!umka || !function_name) {
        LOG_ERROR("Cannot create fiber: NULL umka or function_name");
        return NULL;
    }

    // Get the function from UMKA
    UmkaFuncContext* fn = rebuild_malloc(sizeof(UmkaFuncContext));

    if (!umkaGetFunc(umka, NULL, function_name, fn)) {
        LOG_ERROR("Failed to get UMKA function: %s", function_name);
        rebuild_free(fn);
        return NULL;
    }

    LOG_DEBUG("Created fiber for function: %s", function_name);
    return (UmkaFiber)fn;
}

// Resume fiber execution
UmkaFiberStatus umka_resume_fiber(UmkaFiber fiber) {
    if (!fiber) {
        LOG_ERROR("Cannot resume NULL fiber");
        return UMKA_FIBER_ERROR;
    }

    UmkaContext* ctx = umka_bridge_get_context();
    if (!ctx || !ctx->umka) {
        LOG_ERROR("No UMKA context available for fiber execution");
        return UMKA_FIBER_ERROR;
    }

    UmkaFuncContext* fn = (UmkaFuncContext*)fiber;

    // Call the function
    int result = umkaCall(ctx->umka, fn);

    if (result != 0) {
        UmkaError* error = umkaGetError(ctx->umka);
        LOG_ERROR("UMKA fiber error: %s (line %d)", error->msg, error->line);
        return UMKA_FIBER_ERROR;
    }

    // Check if UMKA is still alive (fiber may have yielded)
    if (!umkaAlive(ctx->umka)) {
        return UMKA_FIBER_COMPLETE;
    }

    return UMKA_FIBER_RUNNING;
}

// Check if fiber is done
bool umka_fiber_is_done(UmkaFiber fiber) {
    if (!fiber) {
        return true;
    }

    UmkaContext* ctx = umka_bridge_get_context();
    if (!ctx || !ctx->umka) {
        return true;
    }

    return !umkaAlive(ctx->umka);
}

// Free fiber
void umka_free_fiber(UmkaFiber fiber) {
    if (fiber) {
        rebuild_free(fiber);
    }
}

//
// FFI Function Implementations
//

// FFI: rebuild_depend_on(target_name: str): str
void umka_ffi_rebuild_depend_on(void* params, void* result) {
    UmkaContext* ctx = umka_bridge_get_context();
    if (!ctx) {
        LOG_ERROR("rebuild_depend_on: No UMKA context");
        return;
    }

    // Get target_name parameter (first parameter, index 0)
    UmkaStackSlot* param_slot = umkaGetParam((UmkaStackSlot*)params, 0);
    const char* target_name = (const char*)param_slot->ptrVal;

    if (!target_name) {
        LOG_ERROR("rebuild_depend_on: NULL target_name");
        return;
    }

    LOG_DEBUG("rebuild_depend_on: %s", target_name);

    // Call scheduler callback to request dependency
    const char* output_path = NULL;
    if (g_callbacks.depend_on) {
        output_path = g_callbacks.depend_on(ctx->scheduler, ctx->current_recipe, target_name);
    }

    // Return output path (or NULL if dependency needs to be built)
    UmkaStackSlot* result_slot = umkaGetResult((UmkaStackSlot*)params, (UmkaStackSlot*)result);
    if (output_path) {
        // Convert C string to UMKA string
        result_slot->ptrVal = umkaMakeStr(ctx->umka, output_path);
    } else {
        result_slot->ptrVal = NULL;
    }
}

// FFI: rebuild_sys(args: []str, argc: int): int
void umka_ffi_rebuild_sys(void* params, void* result) {
    UmkaContext* ctx = umka_bridge_get_context();
    if (!ctx) {
        LOG_ERROR("rebuild_sys: No UMKA context");
        return;
    }

    // Get args array parameter (first parameter)
    UmkaStackSlot* args_slot = umkaGetParam((UmkaStackSlot*)params, 0);
    void* args_array = args_slot->ptrVal;

    // Get argc parameter (second parameter)
    UmkaStackSlot* argc_slot = umkaGetParam((UmkaStackSlot*)params, 1);
    int argc = (int)argc_slot->intVal;

    if (!args_array || argc <= 0) {
        LOG_ERROR("rebuild_sys: Invalid arguments");
        UmkaStackSlot* result_slot = umkaGetResult((UmkaStackSlot*)params, (UmkaStackSlot*)result);
        result_slot->intVal = -1;
        return;
    }

    // Extract command arguments from UMKA dynamic array
    // Note: This is simplified - actual implementation would need to properly
    // extract strings from UMKA's dynamic array structure
    const char** args = rebuild_malloc(sizeof(char*) * (argc + 1));
    for (int i = 0; i < argc; i++) {
        // This is a placeholder - actual array access would use umkaGetDynArrayLen
        // and proper pointer arithmetic based on UMKA's array layout
        args[i] = "placeholder";  // TODO: Extract from UMKA array
    }
    args[argc] = NULL;

    LOG_DEBUG("rebuild_sys: executing command with %d args", argc);

    // Call scheduler callback to execute command
    SysResult sys_result = {0};
    if (g_callbacks.sys) {
        g_callbacks.sys(ctx->scheduler, ctx->current_recipe, args, argc, &sys_result);
    }

    rebuild_free(args);

    // Return exit code
    UmkaStackSlot* result_slot = umkaGetResult((UmkaStackSlot*)params, (UmkaStackSlot*)result);
    result_slot->intVal = sys_result.exit_code;

    // Note: stdout/stderr could be stored in recipe or returned via a struct
    if (sys_result.stdout_output) {
        rebuild_free(sys_result.stdout_output);
    }
    if (sys_result.stderr_output) {
        rebuild_free(sys_result.stderr_output);
    }
}

// FFI: rebuild_register_dep(path: str)
void umka_ffi_rebuild_register_dep(void* params, void* result) {
    UmkaContext* ctx = umka_bridge_get_context();
    if (!ctx || !ctx->current_recipe) {
        LOG_ERROR("rebuild_register_dep: No UMKA context or recipe");
        return;
    }

    // Get path parameter
    UmkaStackSlot* param_slot = umkaGetParam((UmkaStackSlot*)params, 0);
    const char* path = (const char*)param_slot->ptrVal;

    if (!path) {
        LOG_ERROR("rebuild_register_dep: NULL path");
        return;
    }

    LOG_DEBUG("rebuild_register_dep: %s", path);

    // Add dependency to recipe
    RebuildError err = recipe_add_dependency(ctx->current_recipe, path);
    if (err != REBUILD_OK) {
        LOG_ERROR("Failed to register dependency: %s", path);
    }
}

// FFI: rebuild_glob(pattern: str): []str
void umka_ffi_rebuild_glob(void* params, void* result) {
    UmkaContext* ctx = umka_bridge_get_context();
    if (!ctx) {
        LOG_ERROR("rebuild_glob: No UMKA context");
        return;
    }

    // Get pattern parameter
    UmkaStackSlot* param_slot = umkaGetParam((UmkaStackSlot*)params, 0);
    const char* pattern = (const char*)param_slot->ptrVal;

    if (!pattern) {
        LOG_ERROR("rebuild_glob: NULL pattern");
        return;
    }

    LOG_DEBUG("rebuild_glob: %s", pattern);

    // Perform glob operation
    glob_t glob_result;
    int ret = glob(pattern, GLOB_TILDE | GLOB_MARK, NULL, &glob_result);

    if (ret != 0 && ret != GLOB_NOMATCH) {
        LOG_ERROR("rebuild_glob: glob failed for pattern: %s", pattern);
        globfree(&glob_result);
        return;
    }

    // Create UMKA dynamic array for results
    // Note: This is simplified - actual implementation would use umkaMakeDynArray
    // with proper type information
    size_t count = (ret == GLOB_NOMATCH) ? 0 : glob_result.gl_pathc;

    LOG_DEBUG("rebuild_glob: found %zu matches", count);

    // For now, just return empty array - TODO: implement proper array creation
    UmkaStackSlot* result_slot = umkaGetResult((UmkaStackSlot*)params, (UmkaStackSlot*)result);
    result_slot->ptrVal = NULL;  // TODO: Create UMKA array with results

    globfree(&glob_result);
}

// FFI: rebuild_hash_file(path: str): str
void umka_ffi_rebuild_hash_file(void* params, void* result) {
    UmkaContext* ctx = umka_bridge_get_context();
    if (!ctx) {
        LOG_ERROR("rebuild_hash_file: No UMKA context");
        return;
    }

    // Get path parameter
    UmkaStackSlot* param_slot = umkaGetParam((UmkaStackSlot*)params, 0);
    const char* path = (const char*)param_slot->ptrVal;

    if (!path) {
        LOG_ERROR("rebuild_hash_file: NULL path");
        return;
    }

    LOG_DEBUG("rebuild_hash_file: %s", path);

    // Hash the file
    Hash file_hash;
    if (!hash_file(path, &file_hash)) {
        LOG_ERROR("rebuild_hash_file: Failed to hash file: %s", path);
        return;
    }

    // Convert hash to hex string
    char* hex_hash = hash_to_hex(&file_hash);
    if (!hex_hash) {
        LOG_ERROR("rebuild_hash_file: Failed to convert hash to hex");
        return;
    }

    // Return hash as UMKA string
    UmkaStackSlot* result_slot = umkaGetResult((UmkaStackSlot*)params, (UmkaStackSlot*)result);
    result_slot->ptrVal = umkaMakeStr(ctx->umka, hex_hash);

    rebuild_free(hex_hash);
}

// FFI: rebuild_log_info(msg: str)
void umka_ffi_rebuild_log_info(void* params, void* result) {
    // Get message parameter
    UmkaStackSlot* param_slot = umkaGetParam((UmkaStackSlot*)params, 0);
    const char* msg = (const char*)param_slot->ptrVal;

    if (msg) {
        LOG_INFO("%s", msg);
    }
}

// FFI: rebuild_log_debug(msg: str)
void umka_ffi_rebuild_log_debug(void* params, void* result) {
    // Get message parameter
    UmkaStackSlot* param_slot = umkaGetParam((UmkaStackSlot*)params, 0);
    const char* msg = (const char*)param_slot->ptrVal;

    if (msg) {
        LOG_DEBUG("%s", msg);
    }
}

// FFI: rebuild_register_target(name: str, function_name: str)
// Called from BUILD.um files via the target(name, fn) helper
void umka_ffi_rebuild_register_target(void* params, void* result) {
    // Get name parameter (first parameter)
    UmkaStackSlot* name_slot = umkaGetParam((UmkaStackSlot*)params, 0);
    const char* name = (const char*)name_slot->ptrVal;

    // Get function_name parameter (second parameter)
    UmkaStackSlot* fn_slot = umkaGetParam((UmkaStackSlot*)params, 1);
    const char* function_name = (const char*)fn_slot->ptrVal;

    if (!name || !function_name) {
        LOG_ERROR("rebuild_register_target: NULL name or function_name");
        return;
    }

    LOG_DEBUG("rebuild_register_target: %s -> %s", name, function_name);

    // Forward to target registry's FFI handler
    // This is defined in target.c and uses the global g_current_registry
    extern void target_registry_ffi_register(const char* name, const char* function_name);
    target_registry_ffi_register(name, function_name);
}
