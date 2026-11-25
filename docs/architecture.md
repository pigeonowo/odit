# Architecture Overview

This document explains how **Onyx Editor (`odit`)** runs native Onyx + Raylib on the host while dynamically loading, sandboxing, and rendering WebAssembly (`.wasm`) plugins as IDE windows.

---

## High-Level Data Flow

```text
┌─────────────────────────────────────────────────────────────────────────┐
│                    Native Host (`src/main.onyx`)                        │
│              Compiled & run via Onyx OVMwasm Runtime                    │
│                                                                         │
│  ┌──────────────┐  ┌────────────────┐  ┌──────────────┐  ┌───────────┐ │
│  │ Code Editor  │  │ Window Manager │  │ Ctrl+P       │  │ Host UI   │ │
│  │ (src/editor) │  │ (src/wm)       │  │ Palette      │  │ Renderer  │ │
│  └──────┬───────┘  └───────┬────────┘  └──────┬───────┘  └─────┬─────┘ │
│         └──────────────────┴─────────┬────────┴────────────────┘       │
│                                      ▼                                  │
│         ┌─────────────────────────────────────────────────────┐        │
│         │  Plugin Loader & Host ABI (`src/plugin/`)           │        │
│         │  - Calls `odit_wasm_host.so` foreign functions      │        │
│         │  - Exports `__odit_host_dispatch(handle, opcode)`   │        │
│         └──────────────────────────┬──────────────────────────┘        │
└────────────────────────────────────┼────────────────────────────────────┘
                                     │ Onyx C Library FFI (`onyx_library.h`)
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│             Native C Wasmer Bridge (`bin/odit_wasm_host.so`)            │
│                 Built from `native/wasm_host.c`                         │
│  - Loads `libwasmer.so` via `dlopen(..., RTLD_LOCAL)`                   │
│  - Compiles & instantiates plugin `.wasm` modules                       │
│  - Binds all `"odit_host"` imports to one generic trampoline            │
│  - Bounds-checks every guest linear memory read/write                   │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │ Isolated Wasmer Store + Linear Memory
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│        Sandboxed WASM Plugins (`build/plugins/*.wasm`)                  │
│   Compiled with `onyx build -r custom -DComponent_Build --wasm-mvp`     │
│   Zero OS/WASI imports — only imports from module `"odit_host"`         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Key Architectural Decisions

### 1. Why `dlopen(RTLD_LOCAL)` for `libwasmer.so`?
The native Onyx host itself runs inside Onyx's **OVMwasm** runtime, which exports standard WebAssembly C API symbols (`wasm_engine_new`, `wasm_store_new`, `wasm_func_call`, etc.). Linking `odit_wasm_host.so` directly with `-lwasmer` on Linux would cause ELF global symbol interposition where Wasmer calls resolve to OVMwasm symbols and crash.

Loading `~/.wasmer/lib/libwasmer.so` via `dlopen(path, RTLD_NOW | RTLD_LOCAL)` and resolving function pointers with `dlsym` completely isolates the plugin runtime from the host runtime. Lookup order in `native/wasm_host.c`:
1. `$WASMER_DIR/lib/libwasmer.so`
2. `$HOME/.wasmer/lib/libwasmer.so`
3. `libwasmer.so` (standard dynamic linker search paths)

### 2. Single Generic Trampoline + Host Dispatch Table
Rather than writing 50 repetitive C wrapper functions in `native/wasm_host.c`, every `"odit_host"` import maps to an integer `opcode` in `g_host_ops[]` and shares `host_import_trampoline`:
1. Packs up to 12 WASM parameters (`i32`/`u32`/`f32`/`i64`/`f64`) into a fixed scratch buffer (`g_host_scratch: [13] u64`) inside the Onyx host's linear memory.
2. Invokes the Onyx host's exported `#export "__odit_host_dispatch"` via `runtime->wasm_func_call`.
3. Re-fetches `ONYX_PTR(g_host_scratch_offset)` after the call (protecting against host `memory.grow` relocation!).
4. Sets `results->data[0].kind = env->ret_kind` (required by Wasmer's C API) and writes `scratch[12]` back to the plugin.

Adding a new ABI call across Phases 2–4 requires only:
1. Declaring it in `abi/abi.onyx` (`#foreign "odit_host"`).
2. Adding one `{ "fn_name", opcode }` entry in `native/wasm_host.c`.
3. Adding one `case opcode` handler in `src/plugin/host_abi.onyx`.

### 3. Plugin Sandboxing & Compilation Flags
Onyx plugins compile with:
```bash
onyx build -r custom -DComponent_Build --wasm-mvp -o build/plugins/<name>.wasm plugins/<name>/main.onyx
```
- `-r custom -DComponent_Build` omits OS/WASI imports while keeping `core.conv`, `core.string`, `core.encoding.json`, `Array`, and `Map` available.
- `sdk/plugin_sdk.onyx` explicitly exports `_initialize`, `odit_abi_version`, `odit_alloc`, and `odit_free`.
- Note that `-DComponent_Build` names the exported WASM linear memory `"cm32p2_memory"`. `native/wasm_host.c` accepts `"memory"`, `"cm32p2_memory"`, or any exported memory instance.
- `native/wasm_host.c` rejects any plugin that attempts to import from a module other than `"odit_host"`.

### 4. Window Manager & Immediate UI Renderer
- **`src/wm/layout.onyx`**: Full draggable, dockable, and floating window manager managing the top bar, bottom status bar, center Core Code Editor, and four pane modes: **LeftSide** (vertical stack), **RightGrid** (1- or 2-col tiled grid), **Bottom** (horizontal strip), and **Floating** (z-ordered, shadow-elevated, corner-resizable overlays). Supports title-bar drag-and-drop with live dock/swap/float preview overlays, active splitters for every occupied dock zone, non-occluding hit-testing (`wm_content_pane_at`), and automatic persistence in `odit-layout.json`.
- **`src/ui/renderer.onyx`**: Translates plugin-local coordinates `(0, 0) .. (w, h)` to screen coordinates, maintains an intersected 16-deep Raylib scissor clip stack (`BeginScissorMode`), tracks focused plugin text inputs and scroll offsets, and renders all `ui_*` primitives and immediate widgets. Supports a `headless` mode for automated `--self-test` render & interaction tests.
- **Per-frame memory hygiene**: `src/main.onyx` calls `alloc.clear_temp_allocator()` at the start of every frame and every plugin calls `sdk.begin_frame()`. All Raylib string conversions (`theme.to_temp_cstr`), formatting (`tprintf`), and plugin memory reads use `context.temp_allocator`.

### 5. Core Code Editor (`src/editor/`)
- **`buffer.onyx`**: Line-oriented `[..] [..] u8` buffer supporting fast edits, undo snapshots (up to 64 levels), `EditorCursor` synchronization, file load/save, and plugin-driven gutter markers (`GutterMarker`).
- **`lexer.onyx`**: Zero-allocation per-line Onyx syntax tokenizer highlighting keywords, `#` directives, `@` tags, types, strings, numbers, comments, functions, and operators.
- **`view.onyx`**: Supports modal Normal/Insert editing (`h/j/k/l`, `w/b`, `0/$`, `g/G`, `i/a/o/O`, `x`, `dd`, `u`) alongside desktop shortcuts (`Ctrl+S`, `Ctrl+Z`, arrows, Home/End, PgUp/PgDn, mouse click/scroll). Modal mode can be toggled at runtime via `Ctrl+P`.
