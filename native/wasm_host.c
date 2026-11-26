// =============================================================================
// Onyx Editor ("odit") — Native Wasmer Plugin Host Bridge (bin/odit_wasm_host.so)
// =============================================================================
//
// Uses dlopen(RTLD_LOCAL) to load libwasmer.so so Wasmer's C API symbols never
// collide with Onyx's own embedded OVMwasm runtime symbols.
//
// Exposes plugin lifecycle & guest linear memory helpers to the Onyx host, and
// routes every `odit_host.*` import from loaded `.wasm` plugins back into the
// Onyx host's exported `__odit_host_dispatch(handle, opcode)` procedure.
// =============================================================================

#define ONYX_LIBRARY_NAME odit_wasm_host
#include <onyx_library.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// -----------------------------------------------------------------------------
// Dynamically Loaded Wasmer C API Function Pointers
// -----------------------------------------------------------------------------

typedef struct {
    void *dl;

    wasm_engine_t*   (*engine_new)(void);
    void             (*engine_delete)(wasm_engine_t*);
    wasm_store_t*    (*store_new)(wasm_engine_t*);
    void             (*store_delete)(wasm_store_t*);

    void             (*byte_vec_new)(wasm_byte_vec_t*, size_t, const wasm_byte_t*);
    void             (*byte_vec_delete)(wasm_byte_vec_t*);

    wasm_module_t*   (*module_new)(wasm_store_t*, const wasm_byte_vec_t*);
    void             (*module_delete)(wasm_module_t*);
    void             (*module_imports)(const wasm_module_t*, wasm_importtype_vec_t*);
    void             (*module_exports)(const wasm_module_t*, wasm_exporttype_vec_t*);

    const wasm_name_t*       (*importtype_module)(const wasm_importtype_t*);
    const wasm_name_t*       (*importtype_name)(const wasm_importtype_t*);
    const wasm_externtype_t* (*importtype_type)(const wasm_importtype_t*);
    void                     (*importtype_vec_delete)(wasm_importtype_vec_t*);

    const wasm_name_t*       (*exporttype_name)(const wasm_exporttype_t*);
    void                     (*exporttype_vec_delete)(wasm_exporttype_vec_t*);

    wasm_externkind_t        (*externtype_kind)(const wasm_externtype_t*);
    const wasm_functype_t*   (*externtype_as_functype_const)(const wasm_externtype_t*);
    const wasm_valtype_vec_t* (*functype_results)(const wasm_functype_t*);
    wasm_valkind_t           (*valtype_kind)(const wasm_valtype_t*);

    wasm_func_t*     (*func_new_with_env)(wasm_store_t*, const wasm_functype_t*,
                                          wasm_func_callback_with_env_t, void*, void (*)(void*));
    wasm_extern_t*   (*func_as_extern)(wasm_func_t*);
    wasm_trap_t*     (*func_call)(const wasm_func_t*, const wasm_val_vec_t*, wasm_val_vec_t*);

    wasm_instance_t* (*instance_new)(wasm_store_t*, const wasm_module_t*,
                                     const wasm_extern_vec_t*, wasm_trap_t**);
    void             (*instance_delete)(wasm_instance_t*);
    void             (*instance_exports)(const wasm_instance_t*, wasm_extern_vec_t*);

    void             (*extern_vec_new_uninitialized)(wasm_extern_vec_t*, size_t);
    void             (*extern_vec_delete)(wasm_extern_vec_t*);
    wasm_externkind_t (*extern_kind)(const wasm_extern_t*);
    wasm_func_t*     (*extern_as_func)(wasm_extern_t*);
    wasm_memory_t*   (*extern_as_memory)(wasm_extern_t*);

    byte_t*          (*memory_data)(wasm_memory_t*);
    size_t           (*memory_data_size)(const wasm_memory_t*);

    void             (*trap_message)(const wasm_trap_t*, wasm_message_t*);
    void             (*trap_delete)(wasm_trap_t*);
} WasmerApi;

static WasmerApi g_wasmer = {0};
static wasm_engine_t *g_engine = NULL;
static int g_initialized = 0;

// Scratch memory offset inside Onyx host linear memory:
//   [0 .. 11] : uint64_t args[12]
//   [12]      : uint64_t ret_val
static uint32_t g_host_scratch_offset = 0;
static wasm_func_t *g_host_dispatch_func = NULL;

// Last error message buffer for diagnostics
static char g_last_error[1024] = {0};

static void set_error(const char *msg) {
    if (!msg) {
        g_last_error[0] = '\0';
        return;
    }
    snprintf(g_last_error, sizeof(g_last_error), "%s", msg);
}

#define LOAD_SYM(field, sym_name) do { \
    *(void**)(&g_wasmer.field) = dlsym(g_wasmer.dl, sym_name); \
    if (!g_wasmer.field) { \
        snprintf(g_last_error, sizeof(g_last_error), "Missing libwasmer symbol: %s", sym_name); \
        return 0; \
    } \
} while (0)

static int init_wasmer(void) {
    if (g_initialized) return 1;
    signal(SIGPIPE, SIG_IGN);

    const char *candidates[5];
    char buf1[512] = {0};
    char buf2[512] = {0};
    int n = 0;

    const char *wasmer_dir = getenv("WASMER_DIR");
    if (wasmer_dir && wasmer_dir[0]) {
        snprintf(buf1, sizeof(buf1), "%s/lib/libwasmer.so", wasmer_dir);
        candidates[n++] = buf1;
    }
    const char *home = getenv("HOME");
    if (home && home[0]) {
        snprintf(buf2, sizeof(buf2), "%s/.wasmer/lib/libwasmer.so", home);
        candidates[n++] = buf2;
    }
    candidates[n++] = "libwasmer.so";
    candidates[n++] = "/usr/local/lib/libwasmer.so";
    candidates[n++] = "/usr/lib/libwasmer.so";

    for (int i = 0; i < n; i++) {
        g_wasmer.dl = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
        if (g_wasmer.dl) break;
    }
    if (!g_wasmer.dl) {
        snprintf(g_last_error, sizeof(g_last_error), "Could not dlopen libwasmer.so: %s", dlerror());
        return 0;
    }

    LOAD_SYM(engine_new, "wasm_engine_new");
    LOAD_SYM(engine_delete, "wasm_engine_delete");
    LOAD_SYM(store_new, "wasm_store_new");
    LOAD_SYM(store_delete, "wasm_store_delete");
    LOAD_SYM(byte_vec_new, "wasm_byte_vec_new");
    LOAD_SYM(byte_vec_delete, "wasm_byte_vec_delete");
    LOAD_SYM(module_new, "wasm_module_new");
    LOAD_SYM(module_delete, "wasm_module_delete");
    LOAD_SYM(module_imports, "wasm_module_imports");
    LOAD_SYM(module_exports, "wasm_module_exports");
    LOAD_SYM(importtype_module, "wasm_importtype_module");
    LOAD_SYM(importtype_name, "wasm_importtype_name");
    LOAD_SYM(importtype_type, "wasm_importtype_type");
    LOAD_SYM(importtype_vec_delete, "wasm_importtype_vec_delete");
    LOAD_SYM(exporttype_name, "wasm_exporttype_name");
    LOAD_SYM(exporttype_vec_delete, "wasm_exporttype_vec_delete");
    LOAD_SYM(externtype_kind, "wasm_externtype_kind");
    LOAD_SYM(externtype_as_functype_const, "wasm_externtype_as_functype_const");
    LOAD_SYM(functype_results, "wasm_functype_results");
    LOAD_SYM(valtype_kind, "wasm_valtype_kind");
    LOAD_SYM(func_new_with_env, "wasm_func_new_with_env");
    LOAD_SYM(func_as_extern, "wasm_func_as_extern");
    LOAD_SYM(func_call, "wasm_func_call");
    LOAD_SYM(instance_new, "wasm_instance_new");
    LOAD_SYM(instance_delete, "wasm_instance_delete");
    LOAD_SYM(instance_exports, "wasm_instance_exports");
    LOAD_SYM(extern_vec_new_uninitialized, "wasm_extern_vec_new_uninitialized");
    LOAD_SYM(extern_vec_delete, "wasm_extern_vec_delete");
    LOAD_SYM(extern_kind, "wasm_extern_kind");
    LOAD_SYM(extern_as_func, "wasm_extern_as_func");
    LOAD_SYM(extern_as_memory, "wasm_extern_as_memory");
    LOAD_SYM(memory_data, "wasm_memory_data");
    LOAD_SYM(memory_data_size, "wasm_memory_data_size");
    LOAD_SYM(trap_message, "wasm_trap_message");
    LOAD_SYM(trap_delete, "wasm_trap_delete");

    g_engine = g_wasmer.engine_new();
    if (!g_engine) {
        set_error("wasm_engine_new failed");
        return 0;
    }

    g_initialized = 1;
    return 1;
}

// -----------------------------------------------------------------------------
// Host Import Opcode Registry (matches `src/plugin/host_abi.onyx`)
// -----------------------------------------------------------------------------

typedef struct {
    const char *name;
    uint32_t    opcode;
} HostOpSpec;

static const HostOpSpec g_host_ops[] = {
    // A. Host Core & Command Palette (1..19)
    { "host_log",                    1 },
    { "host_set_window_title",       2 },
    { "host_request_redraw",         3 },
    { "host_notify",                 4 },
    { "host_register_command",       5 },
    { "host_execute_command",        6 },
    { "host_spawn_plugin",           7 },

    // B. UI Drawing Primitives & Immediate Widgets (20..49)
    { "ui_push_clip",                20 },
    { "ui_pop_clip",                 21 },
    { "ui_measure_text",             22 },
    { "ui_get_theme_color",          23 },
    { "ui_draw_rect",                24 },
    { "ui_draw_line",                25 },
    { "ui_draw_arrow",               26 },
    { "ui_draw_circle",              27 },
    { "ui_draw_text",                28 },
    { "ui_pill",                     29 },
    { "ui_button",                   30 },
    { "ui_input",                    31 },
    { "ui_dropdown",                 32 },
    { "ui_scroll_area_begin",        33 },
    { "ui_scroll_area_end",          34 },
    { "ui_table_begin",              35 },
    { "ui_table_row",                36 },
    { "ui_table_end",                37 },
    { "ui_texture_load",             38 },
    { "ui_texture_unload",           39 },
    { "ui_draw_texture",             40 },

    // C. Workspace, Editor, AST/LSP & Docs (50..79)
    { "workspace_get_root",          50 },
    { "workspace_list_dir",          51 },
    { "workspace_read_file",         52 },
    { "workspace_find_files",        53 },
    { "editor_get_active_path",      54 },
    { "editor_get_buffer_len",       55 },
    { "editor_get_buffer",           56 },
    { "editor_get_cursor",           57 },
    { "editor_open_file",            58 },
    { "editor_set_cursor",           59 },
    { "editor_set_gutter_marker",    60 },
    { "editor_clear_gutter_markers", 61 },
    { "ast_get_symbols",             62 },
    { "ast_get_references",          63 },
    { "ast_get_diagnostics",         64 },
    { "docs_search",                 65 },
    { "workspace_write_file",        66 },

    // D. Process & Compiler / WAT (80..89)
    { "proc_spawn",                  80 },
    { "proc_poll_status",            81 },
    { "proc_read_output",            82 },
    { "proc_kill",                   83 },
    { "compiler_build",              84 },
    { "compiler_get_function_wat",   85 },
    { "proc_write_input",            86 },

    // E. Live Debugger (90..99)
    { "debug_toggle_breakpoint",     90 },
    { "debug_get_breakpoints",       91 },
    { "debug_start",                 92 },
    { "debug_command",               93 },
    { "debug_get_state",             94 },

    { NULL, 0 }
};

static uint32_t lookup_host_opcode(const char *name, size_t len) {
    for (int i = 0; g_host_ops[i].name != NULL; i++) {
        if (strlen(g_host_ops[i].name) == len && memcmp(g_host_ops[i].name, name, len) == 0) {
            return g_host_ops[i].opcode;
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Plugin Instance Table
// -----------------------------------------------------------------------------

#define MAX_PLUGINS 64

typedef struct {
    uint32_t       handle;
    uint32_t       opcode;
    int            has_ret;
    wasm_valkind_t ret_kind;
} ImportEnv;

typedef struct {
    int in_use;
    wasm_store_t    *store;
    wasm_module_t   *module;
    wasm_instance_t *instance;
    wasm_extern_vec_t exports;
    wasm_memory_t   *memory;

    // Cached plugin exports
    wasm_func_t *fn_initialize;
    wasm_func_t *fn_abi_version;
    wasm_func_t *fn_alloc;
    wasm_func_t *fn_free;
    wasm_func_t *fn_manifest;
    wasm_func_t *fn_init;
    wasm_func_t *fn_shutdown;
    wasm_func_t *fn_event;
    wasm_func_t *fn_render;

    // Import environments to free on unload
    ImportEnv **import_envs;
    size_t      import_env_count;
} PluginSlot;

static PluginSlot g_plugins[MAX_PLUGINS];

static PluginSlot *get_slot(uint32_t handle) {
    if (handle == 0 || handle > MAX_PLUGINS) return NULL;
    PluginSlot *slot = &g_plugins[handle - 1];
    return slot->in_use ? slot : NULL;
}

static void free_import_env(void *env) {
    (void)env; // freed cleanly on unload
}

// -----------------------------------------------------------------------------
// Generic Host Import Trampoline (Wasmer Guest -> C -> Onyx Host Dispatcher)
// -----------------------------------------------------------------------------

static wasm_trap_t* host_import_trampoline(
    void *env_raw,
    const wasm_val_vec_t *params,
    wasm_val_vec_t *results
) {
    ImportEnv *env = (ImportEnv *)env_raw;
    if (!env || !g_host_dispatch_func || g_host_scratch_offset == 0) {
        if (results && results->size > 0) {
            results->data[0].of.i64 = 0;
        }
        return NULL;
    }

    uint64_t *scratch = (uint64_t *)ONYX_PTR(g_host_scratch_offset);
    size_t n = params->size < 12 ? params->size : 12;
    for (size_t i = 0; i < 12; i++) {
        scratch[i] = 0;
    }
    for (size_t i = 0; i < n; i++) {
        const wasm_val_t *v = &params->data[i];
        switch (v->kind) {
            case WASM_I32: scratch[i] = (uint64_t)(uint32_t)v->of.i32; break;
            case WASM_I64: scratch[i] = (uint64_t)v->of.i64; break;
            case WASM_F32: {
                uint32_t bits = 0;
                memcpy(&bits, &v->of.f32, sizeof(bits));
                scratch[i] = (uint64_t)bits;
                break;
            }
            case WASM_F64: {
                uint64_t bits = 0;
                memcpy(&bits, &v->of.f64, sizeof(bits));
                scratch[i] = bits;
                break;
            }
            default: scratch[i] = 0; break;
        }
    }
    scratch[12] = 0;

    wasm_val_t call_args[2] = {
        WASM_I32_VAL((int32_t)env->handle),
        WASM_I32_VAL((int32_t)env->opcode)
    };
    wasm_val_vec_t args_vec = WASM_ARRAY_VEC(call_args);
    wasm_val_vec_t res_vec  = WASM_EMPTY_VEC;

    wasm_trap_t *host_trap = runtime->wasm_func_call(g_host_dispatch_func, &args_vec, &res_vec);
    if (host_trap) {
        runtime->onyx_print_trap(host_trap);
    }

    // Re-fetch ONYX_PTR after callback in case host linear memory grew!
    scratch = (uint64_t *)ONYX_PTR(g_host_scratch_offset);
    if (results && results->size > 0 && env->has_ret) {
        wasm_val_t *r = &results->data[0];
        uint64_t raw = scratch[12];
        r->kind = env->ret_kind;
        switch (env->ret_kind) {
            case WASM_I32: r->of.i32 = (int32_t)(uint32_t)raw; break;
            case WASM_I64: r->of.i64 = (int64_t)raw; break;
            case WASM_F32: {
                uint32_t bits = (uint32_t)raw;
                memcpy(&r->of.f32, &bits, sizeof(bits));
                break;
            }
            case WASM_F64: {
                memcpy(&r->of.f64, &raw, sizeof(raw));
                break;
            }
            default: break;
        }
    }
    return NULL;
}

static void log_wasmer_trap(const char *where, wasm_trap_t *trap) {
    if (!trap) return;
    wasm_message_t msg;
    g_wasmer.trap_message(trap, &msg);
    snprintf(g_last_error, sizeof(g_last_error), "WASM trap in %s: %.*s",
             where, (int)msg.size, msg.data ? msg.data : "");
    fprintf(stderr, "[odit_wasm_host] %s\n", g_last_error);
    g_wasmer.byte_vec_delete(&msg);
    g_wasmer.trap_delete(trap);
}

static int name_eq(const wasm_name_t *n, const char *s) {
    size_t slen = strlen(s);
    return n->size == slen && memcmp(n->data, s, slen) == 0;
}

// -----------------------------------------------------------------------------
// Exported Onyx Native Library Functions
// -----------------------------------------------------------------------------

// wasm_host_init(scratch_ptr: rawptr) -> i32
ONYX_DEF(wasm_host_init, (PTR), (INT)) {
    g_host_scratch_offset = (uint32_t)params->data[0].of.i32;

    if (!init_wasmer()) {
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    wasm_extern_t *ext = runtime->wasm_extern_lookup_by_name(
        runtime->wasm_module, runtime->wasm_instance, "__odit_host_dispatch");
    if (!ext) {
        set_error("Onyx host missing #export \"__odit_host_dispatch\"");
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }
    g_host_dispatch_func = runtime->wasm_extern_as_func(ext);
    if (!g_host_dispatch_func) {
        set_error("__odit_host_dispatch is not a function");
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    results->data[0] = WASM_I32_VAL(1);
    return NULL;
}

// wasm_host_last_error(out_ptr: rawptr, out_cap: i32) -> i32
ONYX_DEF(wasm_host_last_error, (PTR, INT), (INT)) {
    char *dst = (char *)ONYX_PTR(params->data[0].of.i32);
    int cap = params->data[1].of.i32;
    size_t len = strlen(g_last_error);
    if (!dst || cap <= 0) {
        results->data[0] = WASM_I32_VAL((int32_t)len);
        return NULL;
    }
    size_t copy = len < (size_t)cap ? len : (size_t)cap;
    memcpy(dst, g_last_error, copy);
    results->data[0] = WASM_I32_VAL((int32_t)copy);
    return NULL;
}

// wasm_plugin_load(bytes_ptr: rawptr, bytes_len: i32) -> u32 (handle, 0 on error)
ONYX_DEF(wasm_plugin_load, (PTR, INT), (INT)) {
    if (!g_initialized && !init_wasmer()) {
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    const uint8_t *wasm_bytes = (const uint8_t *)ONYX_PTR(params->data[0].of.i32);
    int32_t wasm_len = params->data[1].of.i32;
    if (!wasm_bytes || wasm_len <= 0) {
        set_error("Empty WASM buffer");
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    uint32_t handle = 0;
    for (uint32_t i = 0; i < MAX_PLUGINS; i++) {
        if (!g_plugins[i].in_use) {
            handle = i + 1;
            break;
        }
    }
    if (handle == 0) {
        set_error("Maximum plugin instances reached");
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    PluginSlot *slot = &g_plugins[handle - 1];
    memset(slot, 0, sizeof(*slot));

    slot->store = g_wasmer.store_new(g_engine);
    if (!slot->store) {
        set_error("wasm_store_new failed");
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    wasm_byte_vec_t binary;
    g_wasmer.byte_vec_new(&binary, (size_t)wasm_len, (const wasm_byte_t *)wasm_bytes);
    slot->module = g_wasmer.module_new(slot->store, &binary);
    g_wasmer.byte_vec_delete(&binary);

    if (!slot->module) {
        set_error("Failed to compile WASM module");
        g_wasmer.store_delete(slot->store);
        memset(slot, 0, sizeof(*slot));
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    // Bind imports from module "odit_host"
    wasm_importtype_vec_t import_types;
    g_wasmer.module_imports(slot->module, &import_types);

    wasm_extern_vec_t imports;
    g_wasmer.extern_vec_new_uninitialized(&imports, import_types.size);
    slot->import_envs = (ImportEnv **)calloc(import_types.size ? import_types.size : 1, sizeof(ImportEnv *));
    slot->import_env_count = import_types.size;

    int import_ok = 1;
    for (size_t i = 0; i < import_types.size; i++) {
        const wasm_importtype_t *it = import_types.data[i];
        const wasm_name_t *mod_name = g_wasmer.importtype_module(it);
        const wasm_name_t *imp_name = g_wasmer.importtype_name(it);
        const wasm_externtype_t *et = g_wasmer.importtype_type(it);

        if (name_eq(mod_name, "host") && g_wasmer.externtype_kind(et) == WASM_EXTERN_FUNC) {
            // Sandboxed Onyx `-r js` runtime stubs (print_str -> host_log, exit/time -> no-op)
            uint32_t hop = name_eq(imp_name, "print_str") ? 1 : 3;
            const wasm_functype_t *ft = g_wasmer.externtype_as_functype_const(et);
            const wasm_valtype_vec_t *res_types = g_wasmer.functype_results(ft);
            ImportEnv *env = (ImportEnv *)malloc(sizeof(ImportEnv));
            env->handle = handle;
            env->opcode = hop;
            env->has_ret = (res_types && res_types->size > 0) ? 1 : 0;
            env->ret_kind = env->has_ret ? g_wasmer.valtype_kind(res_types->data[0]) : WASM_I32;
            slot->import_envs[i] = env;
            wasm_func_t *fn = g_wasmer.func_new_with_env(slot->store, ft, host_import_trampoline, env, free_import_env);
            imports.data[i] = g_wasmer.func_as_extern(fn);
            continue;
        }

        if (!name_eq(mod_name, "odit_host") || g_wasmer.externtype_kind(et) != WASM_EXTERN_FUNC) {
            snprintf(g_last_error, sizeof(g_last_error),
                     "Unsupported plugin import: %.*s.%.*s (only odit_host functions allowed)",
                     (int)mod_name->size, mod_name->data,
                     (int)imp_name->size, imp_name->data);
            import_ok = 0;
            break;
        }

        uint32_t opcode = lookup_host_opcode(imp_name->data, imp_name->size);
        if (opcode == 0) {
            snprintf(g_last_error, sizeof(g_last_error),
                     "Unknown odit_host import: %.*s", (int)imp_name->size, imp_name->data);
            import_ok = 0;
            break;
        }

        const wasm_functype_t *ft = g_wasmer.externtype_as_functype_const(et);
        const wasm_valtype_vec_t *res_types = g_wasmer.functype_results(ft);

        ImportEnv *env = (ImportEnv *)malloc(sizeof(ImportEnv));
        env->handle = handle;
        env->opcode = opcode;
        env->has_ret = (res_types && res_types->size > 0) ? 1 : 0;
        env->ret_kind = env->has_ret ? g_wasmer.valtype_kind(res_types->data[0]) : WASM_I32;
        slot->import_envs[i] = env;

        wasm_func_t *fn = g_wasmer.func_new_with_env(slot->store, ft, host_import_trampoline, env, free_import_env);
        imports.data[i] = g_wasmer.func_as_extern(fn);
    }
    g_wasmer.importtype_vec_delete(&import_types);

    if (!import_ok) {
        for (size_t i = 0; i < slot->import_env_count; i++) free(slot->import_envs[i]);
        free(slot->import_envs);
        g_wasmer.module_delete(slot->module);
        g_wasmer.store_delete(slot->store);
        memset(slot, 0, sizeof(*slot));
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    wasm_trap_t *trap = NULL;
    slot->instance = g_wasmer.instance_new(slot->store, slot->module, &imports, &trap);
    if (!slot->instance || trap) {
        log_wasmer_trap("instance_new", trap);
        for (size_t i = 0; i < slot->import_env_count; i++) free(slot->import_envs[i]);
        free(slot->import_envs);
        g_wasmer.module_delete(slot->module);
        g_wasmer.store_delete(slot->store);
        memset(slot, 0, sizeof(*slot));
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    // Resolve exports
    wasm_exporttype_vec_t export_types;
    g_wasmer.module_exports(slot->module, &export_types);
    g_wasmer.instance_exports(slot->instance, &slot->exports);

    for (size_t i = 0; i < export_types.size && i < slot->exports.size; i++) {
        const wasm_name_t *ename = g_wasmer.exporttype_name(export_types.data[i]);
        wasm_extern_t *ext = slot->exports.data[i];
        wasm_externkind_t kind = g_wasmer.extern_kind(ext);

        if (kind == WASM_EXTERN_MEMORY && (name_eq(ename, "memory") || name_eq(ename, "cm32p2_memory") || slot->memory == NULL)) {
            slot->memory = g_wasmer.extern_as_memory(ext);
        } else if (kind == WASM_EXTERN_FUNC) {
            wasm_func_t *f = g_wasmer.extern_as_func(ext);
            if      (name_eq(ename, "_initialize"))          slot->fn_initialize  = f;
            else if (name_eq(ename, "odit_abi_version"))     slot->fn_abi_version = f;
            else if (name_eq(ename, "odit_alloc"))           slot->fn_alloc       = f;
            else if (name_eq(ename, "odit_free"))            slot->fn_free        = f;
            else if (name_eq(ename, "odit_plugin_manifest")) slot->fn_manifest    = f;
            else if (name_eq(ename, "odit_plugin_init"))     slot->fn_init        = f;
            else if (name_eq(ename, "odit_plugin_shutdown")) slot->fn_shutdown    = f;
            else if (name_eq(ename, "odit_plugin_event"))    slot->fn_event       = f;
            else if (name_eq(ename, "odit_plugin_render"))   slot->fn_render      = f;
        }
    }
    g_wasmer.exporttype_vec_delete(&export_types);

    if (!slot->memory || !slot->fn_alloc || !slot->fn_free || !slot->fn_render) {
        set_error("Plugin missing required exports (memory, odit_alloc, odit_free, odit_plugin_render)");
        g_wasmer.extern_vec_delete(&slot->exports);
        g_wasmer.instance_delete(slot->instance);
        for (size_t i = 0; i < slot->import_env_count; i++) free(slot->import_envs[i]);
        free(slot->import_envs);
        g_wasmer.module_delete(slot->module);
        g_wasmer.store_delete(slot->store);
        memset(slot, 0, sizeof(*slot));
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    slot->in_use = 1;

    // Run Onyx guest runtime initializer if exported
    if (slot->fn_initialize) {
        wasm_val_vec_t empty = WASM_EMPTY_VEC;
        wasm_trap_t *itrap = g_wasmer.func_call(slot->fn_initialize, &empty, &empty);
        if (itrap) {
            log_wasmer_trap("_initialize", itrap);
        }
    }

    set_error("");
    results->data[0] = WASM_I32_VAL((int32_t)handle);
    return NULL;
}

// wasm_plugin_unload(handle: u32) -> void
ONYX_DEF(wasm_plugin_unload, (INT), ()) {
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    if (!slot) return NULL;

    g_wasmer.extern_vec_delete(&slot->exports);
    if (slot->instance) g_wasmer.instance_delete(slot->instance);
    if (slot->module)   g_wasmer.module_delete(slot->module);
    if (slot->store)    g_wasmer.store_delete(slot->store);
    for (size_t i = 0; i < slot->import_env_count; i++) free(slot->import_envs[i]);
    free(slot->import_envs);
    memset(slot, 0, sizeof(*slot));
    return NULL;
}

// wasm_plugin_abi_version(handle: u32) -> u32
ONYX_DEF(wasm_plugin_abi_version, (INT), (INT)) {
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    if (!slot || !slot->fn_abi_version) {
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }
    wasm_val_t res[1] = { WASM_INIT_VAL };
    wasm_val_vec_t args_vec = WASM_EMPTY_VEC;
    wasm_val_vec_t res_vec  = WASM_ARRAY_VEC(res);
    wasm_trap_t *trap = g_wasmer.func_call(slot->fn_abi_version, &args_vec, &res_vec);
    if (trap) {
        log_wasmer_trap("odit_abi_version", trap);
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }
    results->data[0] = WASM_I32_VAL(res[0].of.i32);
    return NULL;
}

// wasm_plugin_alloc(handle: u32, size: u32) -> u32
ONYX_DEF(wasm_plugin_alloc, (INT, INT), (INT)) {
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    if (!slot || !slot->fn_alloc) {
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }
    wasm_val_t args[1] = { WASM_I32_VAL(params->data[1].of.i32) };
    wasm_val_t res[1]  = { WASM_INIT_VAL };
    wasm_val_vec_t args_vec = WASM_ARRAY_VEC(args);
    wasm_val_vec_t res_vec  = WASM_ARRAY_VEC(res);
    wasm_trap_t *trap = g_wasmer.func_call(slot->fn_alloc, &args_vec, &res_vec);
    if (trap) {
        log_wasmer_trap("odit_alloc", trap);
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }
    results->data[0] = WASM_I32_VAL(res[0].of.i32);
    return NULL;
}

// wasm_plugin_free(handle: u32, ptr: u32) -> void
ONYX_DEF(wasm_plugin_free, (INT, INT), ()) {
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    if (!slot || !slot->fn_free) return NULL;
    wasm_val_t args[1] = { WASM_I32_VAL(params->data[1].of.i32) };
    wasm_val_vec_t args_vec = WASM_ARRAY_VEC(args);
    wasm_val_vec_t res_vec  = WASM_EMPTY_VEC;
    wasm_trap_t *trap = g_wasmer.func_call(slot->fn_free, &args_vec, &res_vec);
    if (trap) log_wasmer_trap("odit_free", trap);
    return NULL;
}

// wasm_plugin_manifest(handle: u32, guest_out_ptr: u32, cap: u32) -> i32
ONYX_DEF(wasm_plugin_manifest, (INT, INT, INT), (INT)) {
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    if (!slot || !slot->fn_manifest) {
        results->data[0] = WASM_I32_VAL(-1);
        return NULL;
    }
    wasm_val_t args[2] = {
        WASM_I32_VAL(params->data[1].of.i32),
        WASM_I32_VAL(params->data[2].of.i32)
    };
    wasm_val_t res[1] = { WASM_INIT_VAL };
    wasm_val_vec_t args_vec = WASM_ARRAY_VEC(args);
    wasm_val_vec_t res_vec  = WASM_ARRAY_VEC(res);
    wasm_trap_t *trap = g_wasmer.func_call(slot->fn_manifest, &args_vec, &res_vec);
    if (trap) {
        log_wasmer_trap("odit_plugin_manifest", trap);
        results->data[0] = WASM_I32_VAL(-1);
        return NULL;
    }
    results->data[0] = WASM_I32_VAL(res[0].of.i32);
    return NULL;
}

// wasm_plugin_init(handle: u32, window_id: u32) -> i32
ONYX_DEF(wasm_plugin_init, (INT, INT), (INT)) {
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    if (!slot || !slot->fn_init) {
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }
    wasm_val_t args[1] = { WASM_I32_VAL(params->data[1].of.i32) };
    wasm_val_t res[1]  = { WASM_INIT_VAL };
    wasm_val_vec_t args_vec = WASM_ARRAY_VEC(args);
    wasm_val_vec_t res_vec  = WASM_ARRAY_VEC(res);
    wasm_trap_t *trap = g_wasmer.func_call(slot->fn_init, &args_vec, &res_vec);
    if (trap) {
        log_wasmer_trap("odit_plugin_init", trap);
        results->data[0] = WASM_I32_VAL(-1);
        return NULL;
    }
    results->data[0] = WASM_I32_VAL(res[0].of.i32);
    return NULL;
}

// wasm_plugin_shutdown(handle: u32, window_id: u32) -> void
ONYX_DEF(wasm_plugin_shutdown, (INT, INT), ()) {
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    if (!slot || !slot->fn_shutdown) return NULL;
    wasm_val_t args[1] = { WASM_I32_VAL(params->data[1].of.i32) };
    wasm_val_vec_t args_vec = WASM_ARRAY_VEC(args);
    wasm_val_vec_t res_vec  = WASM_EMPTY_VEC;
    wasm_trap_t *trap = g_wasmer.func_call(slot->fn_shutdown, &args_vec, &res_vec);
    if (trap) log_wasmer_trap("odit_plugin_shutdown", trap);
    return NULL;
}

// wasm_plugin_event(handle: u32, window_id: u32, event_type: u32, payload_ptr: u32, payload_len: u32) -> void
ONYX_DEF(wasm_plugin_event, (INT, INT, INT, INT, INT), ()) {
    set_error("");
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    if (!slot || !slot->fn_event) return NULL;
    wasm_val_t args[4] = {
        WASM_I32_VAL(params->data[1].of.i32),
        WASM_I32_VAL(params->data[2].of.i32),
        WASM_I32_VAL(params->data[3].of.i32),
        WASM_I32_VAL(params->data[4].of.i32)
    };
    wasm_val_vec_t args_vec = WASM_ARRAY_VEC(args);
    wasm_val_vec_t res_vec  = WASM_EMPTY_VEC;
    wasm_trap_t *trap = g_wasmer.func_call(slot->fn_event, &args_vec, &res_vec);
    if (trap) log_wasmer_trap("odit_plugin_event", trap);
    return NULL;
}

// wasm_plugin_render(handle: u32, window_id: u32, ctx_guest_ptr: u32) -> void
ONYX_DEF(wasm_plugin_render, (INT, INT, INT), ()) {
    set_error("");
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    if (!slot || !slot->fn_render) return NULL;
    wasm_val_t args[2] = {
        WASM_I32_VAL(params->data[1].of.i32),
        WASM_I32_VAL(params->data[2].of.i32)
    };
    wasm_val_vec_t args_vec = WASM_ARRAY_VEC(args);
    wasm_val_vec_t res_vec  = WASM_EMPTY_VEC;
    wasm_trap_t *trap = g_wasmer.func_call(slot->fn_render, &args_vec, &res_vec);
    if (trap) log_wasmer_trap("odit_plugin_render", trap);
    return NULL;
}

// wasm_plugin_mem_read(handle: u32, guest_ptr: u32, host_dst: rawptr, len: u32) -> i32
ONYX_DEF(wasm_plugin_mem_read, (INT, INT, PTR, INT), (INT)) {
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    uint32_t gptr = (uint32_t)params->data[1].of.i32;
    void *dst = ONYX_PTR(params->data[2].of.i32);
    uint32_t len = (uint32_t)params->data[3].of.i32;

    if (!slot || !slot->memory || !dst) {
        results->data[0] = WASM_I32_VAL(-4);
        return NULL;
    }
    if (len == 0) {
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }
    size_t mem_sz = g_wasmer.memory_data_size(slot->memory);
    if ((size_t)gptr > mem_sz || (size_t)len > mem_sz - (size_t)gptr) {
        results->data[0] = WASM_I32_VAL(-5);
        return NULL;
    }
    byte_t *base = g_wasmer.memory_data(slot->memory);
    memcpy(dst, base + gptr, len);
    results->data[0] = WASM_I32_VAL((int32_t)len);
    return NULL;
}

// wasm_plugin_mem_write(handle: u32, guest_ptr: u32, host_src: rawptr, len: u32) -> i32
ONYX_DEF(wasm_plugin_mem_write, (INT, INT, PTR, INT), (INT)) {
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    uint32_t gptr = (uint32_t)params->data[1].of.i32;
    const void *src = ONYX_PTR(params->data[2].of.i32);
    uint32_t len = (uint32_t)params->data[3].of.i32;

    if (!slot || !slot->memory || !src) {
        results->data[0] = WASM_I32_VAL(-4);
        return NULL;
    }
    if (len == 0) {
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }
    size_t mem_sz = g_wasmer.memory_data_size(slot->memory);
    if ((size_t)gptr > mem_sz || (size_t)len > mem_sz - (size_t)gptr) {
        results->data[0] = WASM_I32_VAL(-5);
        return NULL;
    }
    byte_t *base = g_wasmer.memory_data(slot->memory);
    memcpy(base + gptr, src, len);
    results->data[0] = WASM_I32_VAL((int32_t)len);
    return NULL;
}

// wasm_plugin_mem_size(handle: u32) -> u32
ONYX_DEF(wasm_plugin_mem_size, (INT), (INT)) {
    PluginSlot *slot = get_slot((uint32_t)params->data[0].of.i32);
    if (!slot || !slot->memory) {
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }
    results->data[0] = WASM_I32_VAL((int32_t)g_wasmer.memory_data_size(slot->memory));
    return NULL;
}

// -----------------------------------------------------------------------------
// Non-Blocking Child Process Manager (LSP, Compiler, Build & Run)
// -----------------------------------------------------------------------------
// Sets O_NONBLOCK ONLY on parent-side stdout/stderr read ends so child stdin
// remains blocking (required by `onyx lsp` and other stdio servers).

#define MAX_PROCS 32

typedef struct {
    int   in_use;
    pid_t pid;
    int   stdin_fd;
    int   stdout_fd;
    int   stderr_fd;
    int   state;      // 1=Running, 2=Exited, 3=Failed, 4=Killed
    int   exit_code;
} NativeProc;

static NativeProc g_procs[MAX_PROCS];

static NativeProc *get_proc(uint32_t handle) {
    if (handle == 0 || handle > MAX_PROCS) return NULL;
    NativeProc *p = &g_procs[handle - 1];
    return p->in_use ? p : NULL;
}

static void proc_reap_nonblocking(NativeProc *p) {
    if (!p || p->state != 1 || p->pid <= 0) return;
    int status = 0;
    pid_t r = waitpid(p->pid, &status, WNOHANG);
    if (r == p->pid) {
        if (WIFEXITED(status)) {
            p->state = 2;
            p->exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            p->state = 4;
            p->exit_code = -WTERMSIG(status);
        } else {
            p->state = 2;
            p->exit_code = 0;
        }
    }
}

// native_proc_spawn(cmd_ptr, cmd_len, args_ptr, args_len, cwd_ptr, cwd_len) -> u32
// `args` is '\0'- or '\n'-separated argument strings (excluding argv[0]).
ONYX_DEF(native_proc_spawn, (PTR, INT, PTR, INT, PTR, INT), (INT)) {
    signal(SIGPIPE, SIG_IGN);

    const char *cmd_p  = (const char *)ONYX_PTR(params->data[0].of.i32);
    int cmd_len        = params->data[1].of.i32;
    const char *args_p = (const char *)ONYX_PTR(params->data[2].of.i32);
    int args_len       = params->data[3].of.i32;
    const char *cwd_p  = (const char *)ONYX_PTR(params->data[4].of.i32);
    int cwd_len        = params->data[5].of.i32;

    if (!cmd_p || cmd_len <= 0) {
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    uint32_t handle = 0;
    for (uint32_t i = 0; i < MAX_PROCS; i++) {
        if (!g_procs[i].in_use) { handle = i + 1; break; }
    }
    if (handle == 0) {
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    char *cmd = (char *)malloc((size_t)cmd_len + 1);
    memcpy(cmd, cmd_p, (size_t)cmd_len);
    cmd[cmd_len] = '\0';

    char *cwd = NULL;
    if (cwd_p && cwd_len > 0) {
        cwd = (char *)malloc((size_t)cwd_len + 1);
        memcpy(cwd, cwd_p, (size_t)cwd_len);
        cwd[cwd_len] = '\0';
    }

    char *argv[64];
    int argc = 0;
    argv[argc++] = cmd;
    char *args_copy = NULL;
    if (args_p && args_len > 0) {
        args_copy = (char *)malloc((size_t)args_len + 1);
        memcpy(args_copy, args_p, (size_t)args_len);
        args_copy[args_len] = '\0';
        char *cur = args_copy;
        char *end = args_copy + args_len;
        while (cur < end && argc < 63) {
            char *sep = cur;
            while (sep < end && *sep != '\n' && *sep != '\0') sep++;
            *sep = '\0';
            if (sep > cur) argv[argc++] = cur;
            cur = sep + 1;
        }
    }
    argv[argc] = NULL;

    int in_pipe[2], out_pipe[2], err_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        free(cmd); free(cwd); free(args_copy);
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        free(cmd); free(cwd); free(args_copy);
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }

    if (pid == 0) {
        // Child process: keep stdin/stdout/stderr blocking!
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        if (cwd && cwd[0]) {
            if (chdir(cwd) != 0) _exit(126);
        }
        execvp(cmd, argv);
        _exit(127);
    }

    // Parent process: close unused ends, set O_NONBLOCK ONLY on stdout/stderr read ends
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);
    fcntl(out_pipe[0], F_SETFL, fcntl(out_pipe[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, fcntl(err_pipe[0], F_GETFL, 0) | O_NONBLOCK);

    NativeProc *p = &g_procs[handle - 1];
    p->in_use    = 1;
    p->pid       = pid;
    p->stdin_fd  = in_pipe[1];
    p->stdout_fd = out_pipe[0];
    p->stderr_fd = err_pipe[0];
    p->state     = 1;
    p->exit_code = 0;

    free(cmd); free(cwd); free(args_copy);
    results->data[0] = WASM_I32_VAL((int32_t)handle);
    return NULL;
}

// native_proc_write(handle, src_ptr, src_len) -> i32
ONYX_DEF(native_proc_write, (INT, PTR, INT), (INT)) {
    NativeProc *p = get_proc((uint32_t)params->data[0].of.i32);
    const void *src = ONYX_PTR(params->data[1].of.i32);
    int len = params->data[2].of.i32;
    if (!p || p->stdin_fd < 0 || !src || len <= 0) {
        results->data[0] = WASM_I32_VAL(-1);
        return NULL;
    }
    ssize_t w = write(p->stdin_fd, src, (size_t)len);
    results->data[0] = WASM_I32_VAL(w < 0 ? -1 : (int32_t)w);
    return NULL;
}

// native_proc_read(handle, stream_mask, out_ptr, out_cap) -> i32
// stream_mask: 1=stdout, 2=stderr. Returns >0 bytes, 0 if EAGAIN, -1 on EOF.
ONYX_DEF(native_proc_read, (INT, INT, PTR, INT), (INT)) {
    NativeProc *p = get_proc((uint32_t)params->data[0].of.i32);
    int mask = params->data[1].of.i32;
    void *dst = ONYX_PTR(params->data[2].of.i32);
    int cap = params->data[3].of.i32;
    if (!p || !dst || cap <= 0) {
        results->data[0] = WASM_I32_VAL(-1);
        return NULL;
    }
    int fd = (mask == 2) ? p->stderr_fd : p->stdout_fd;
    if (fd < 0) {
        results->data[0] = WASM_I32_VAL(-1);
        return NULL;
    }
    ssize_t r = read(fd, dst, (size_t)cap);
    if (r > 0) {
        results->data[0] = WASM_I32_VAL((int32_t)r);
    } else if (r == 0) {
        proc_reap_nonblocking(p);
        results->data[0] = WASM_I32_VAL(-1);
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        results->data[0] = WASM_I32_VAL(0);
    } else {
        results->data[0] = WASM_I32_VAL(-1);
    }
    return NULL;
}

// native_proc_poll(handle, out_exit_code_ptr) -> u32 (ProcState)
ONYX_DEF(native_proc_poll, (INT, PTR), (INT)) {
    NativeProc *p = get_proc((uint32_t)params->data[0].of.i32);
    int32_t *out_code = (int32_t *)ONYX_PTR(params->data[1].of.i32);
    if (!p) {
        results->data[0] = WASM_I32_VAL(0);
        return NULL;
    }
    proc_reap_nonblocking(p);
    if (out_code) *out_code = p->exit_code;
    results->data[0] = WASM_I32_VAL(p->state);
    return NULL;
}

// native_proc_kill(handle) -> i32
ONYX_DEF(native_proc_kill, (INT), (INT)) {
    NativeProc *p = get_proc((uint32_t)params->data[0].of.i32);
    if (!p) {
        results->data[0] = WASM_I32_VAL(-1);
        return NULL;
    }
    if (p->stdin_fd  >= 0) { close(p->stdin_fd);  p->stdin_fd  = -1; }
    if (p->stdout_fd >= 0) { close(p->stdout_fd); p->stdout_fd = -1; }
    if (p->stderr_fd >= 0) { close(p->stderr_fd); p->stderr_fd = -1; }
    if (p->state == 1 && p->pid > 0) {
        kill(p->pid, SIGTERM);
        int status = 0;
        waitpid(p->pid, &status, 0);
        p->state = 4;
    }
    p->in_use = 0;
    results->data[0] = WASM_I32_VAL(0);
    return NULL;
}

ONYX_LIBRARY {
    ONYX_FUNC(wasm_host_init)
    ONYX_FUNC(wasm_host_last_error)
    ONYX_FUNC(wasm_plugin_load)
    ONYX_FUNC(wasm_plugin_unload)
    ONYX_FUNC(wasm_plugin_abi_version)
    ONYX_FUNC(wasm_plugin_alloc)
    ONYX_FUNC(wasm_plugin_free)
    ONYX_FUNC(wasm_plugin_manifest)
    ONYX_FUNC(wasm_plugin_init)
    ONYX_FUNC(wasm_plugin_shutdown)
    ONYX_FUNC(wasm_plugin_event)
    ONYX_FUNC(wasm_plugin_render)
    ONYX_FUNC(wasm_plugin_mem_read)
    ONYX_FUNC(wasm_plugin_mem_write)
    ONYX_FUNC(wasm_plugin_mem_size)
    ONYX_FUNC(native_proc_spawn)
    ONYX_FUNC(native_proc_write)
    ONYX_FUNC(native_proc_read)
    ONYX_FUNC(native_proc_poll)
    ONYX_FUNC(native_proc_kill)
    NULL
};
