# Host-to-WASM Plugin ABI Reference (v1.0.0)

The canonical ABI definition lives in [`abi/abi.onyx`](../abi/abi.onyx). Both the native host (`-r onyx`) and every WASM plugin (`-r custom`) `#load` this exact file.

---

## Core Conventions

1. **Flat Scalar Boundary**: All `#foreign "odit_host"` imports use only `u32`, `i32`, `f32`, and `u64` parameters/returns — zero compiler-specific struct or slice ABI assumptions.
2. **Buffer-Size Query Pattern**: Variable-length host queries use `(out_ptr: u32, out_cap: u32) -> i32`:
   - Pass `out_ptr = 0, out_cap = 0` to query required byte length (`>= 0`).
   - Pass a guest buffer to write UTF-8 / JSON bytes and receive bytes written (`>= 0`).
   - Negative return values are `AbiError` codes (`-1` Unknown, `-2` NotFound, `-3` BufferTooSmall, `-4` InvalidArg, `-5` PermissionDenied, `-6` Busy, `-7` NotReady, `-8` ProcessFailed, `-9` Unsupported).
3. **Hot-Path Binary Structs vs. JSON**:
   - Per-frame data (`FrameContext` 48B, `EditorCursor` 28B, `KeyEvent` 16B, `ProcessEventHeader` 16B) uses fixed 4-byte-aligned POD structs.
   - Structured queries (`PluginManifest`, `DirEntry[]`, `SymbolInfo[]`, `Diagnostic[]`, `DocEntry[]`, `FunctionWatInfo`, `DebugState`) use UTF-8 JSON.
4. **Colors**: Packed `u32` in `0xRRGGBBAA` order (`abi.rgba(r, g, b, a)`). Prefer `sdk.theme_color(.Role)` to match the IDE theme.

---

## Required Plugin Exports (Guest → Host)

When using `sdk/plugin_sdk.onyx`, the first 4 exports are provided automatically. Each plugin only implements the 5 lifecycle hooks:

| Export | Provided By | Purpose |
| :--- | :--- | :--- |
| `_initialize()` | `sdk/plugin_sdk.onyx` | Initializes Onyx guest heap & runtime |
| `odit_abi_version() -> u32` | `sdk/plugin_sdk.onyx` | Returns `0x00010000` (v1.0.0) |
| `odit_alloc(size: u32) -> u32` | `sdk/plugin_sdk.onyx` | Allocates guest memory for host writes |
| `odit_free(ptr: u32)` | `sdk/plugin_sdk.onyx` | Frees guest memory |
| `odit_plugin_manifest(out_ptr, out_cap) -> i32` | Plugin | Writes JSON `PluginManifest` |
| `odit_plugin_init(window_id) -> i32` | Plugin | Init state, register `Ctrl+P` commands |
| `odit_plugin_shutdown(window_id)` | Plugin | Clean up state/textures |
| `odit_plugin_event(window_id, ev_type, ptr, len)` | Plugin | Handle subscribed `EventMask` events |
| `odit_plugin_render(window_id, ctx_ptr)` | Plugin | Render immediate UI for this frame |

---

## Host Import Opcodes (`#foreign "odit_host"`)

### A. Core & Command Palette (`1..19`)
| Opcode | Function | Phase |
| :---: | :--- | :---: |
| `1` | `host_log(level, msg_ptr, msg_len)` | 1 |
| `2` | `host_set_window_title(title_ptr, title_len)` | 1 |
| `3` | `host_request_redraw()` | 1 |
| `4` | `host_notify(severity, msg_ptr, msg_len)` | 1 |
| `5` | `host_register_command(id..., label..., shortcut...) -> u32` | 1 |
| `6` | `host_execute_command(id..., arg...) -> i32` | 1 |
| `7` | `host_spawn_plugin(plugin_id...) -> u32` | 1 |

### B. UI Drawing & Immediate Widgets (`20..49`)
All coordinates are window-local `(0, 0) .. (ctx.width, ctx.height)` and clipped to the pane.

| Opcode | Function | Phase |
| :---: | :--- | :---: |
| `20–21` | `ui_push_clip` / `ui_pop_clip` | 1 |
| `22–23` | `ui_measure_text` / `ui_get_theme_color` | 1 |
| `24–28` | `ui_draw_rect`, `ui_draw_line`, `ui_draw_arrow`, `ui_draw_circle`, `ui_draw_text` | 1 |
| `29–32` | `ui_pill`, `ui_button`, `ui_input`, `ui_dropdown` | 1 |
| `33–34` | `ui_scroll_area_begin` / `ui_scroll_area_end` | 1 |
| `35–37` | `ui_table_begin` / `ui_table_row` / `ui_table_end` | 1 |
| `38–40` | `ui_texture_load`, `ui_texture_unload`, `ui_draw_texture` | 1 |

### C. Workspace, Editor, AST/LSP & Docs (`50..79`)
| Opcode | Function | Phase |
| :---: | :--- | :---: |
| `50–52` | `workspace_get_root`, `workspace_list_dir`, `workspace_read_file` | 1 |
| `53` | `workspace_find_files(ext...)` | 3 |
| `54–61` | `editor_get_active_path`, `editor_get_buffer_len`, `editor_get_buffer`, `editor_get_cursor`, `editor_open_file`, `editor_set_cursor`, `editor_set_gutter_marker`, `editor_clear_gutter_markers` | 1 |
| `62–64` | `ast_get_symbols`, `ast_get_references`, `ast_get_diagnostics` | 2 |
| `65` | `docs_search` | 3 |

### D. Process, Compiler & WAT (`80..89`)
| Opcode | Function | Phase |
| :---: | :--- | :---: |
| `80–83` | `proc_spawn`, `proc_poll_status`, `proc_read_output`, `proc_kill` | 3 |
| `84–85` | `compiler_build`, `compiler_get_function_wat` | 3 |

### E. Live Debugger (`90..99`)
| Opcode | Function | Phase |
| :---: | :--- | :---: |
| `90–94` | `debug_toggle_breakpoint`, `debug_get_breakpoints`, `debug_start`, `debug_command`, `debug_get_state` | 4 |

---

## Authoring a New WASM Plugin

1. Create `plugins/<my_plugin>/main.onyx`:
   ```onyx
   #load "../../abi/abi.onyx"
   #load "../../sdk/plugin_sdk.onyx"

   use odit.abi
   use odit.sdk

   MANIFEST :: """{"id":"odit.my_plugin","title":"My Plugin","version":"0.1.0","default_slot":0,"default_width":360,"default_height":240,"event_mask":3,"allow_multiple":false}"""

   #export "odit_plugin_manifest" plugin_manifest
   plugin_manifest :: (out_ptr, out_cap: u32) -> i32 => sdk.write_out_str(MANIFEST, out_ptr, out_cap);

   #export "odit_plugin_init" plugin_init
   plugin_init :: (window_id: u32) -> i32 {
       sdk.register_command("my_plugin.hello", "My Plugin: Say Hello", "");
       return 0;
   }

   #export "odit_plugin_shutdown" plugin_shutdown
   plugin_shutdown :: (window_id: u32) -> void {}

   #export "odit_plugin_event" plugin_event
   plugin_event :: (window_id, ev_type, ptr, len: u32) -> void {}

   #export "odit_plugin_render" plugin_render
   plugin_render :: (window_id, ctx_ptr: u32) -> void {
       ctx := cast(&abi.FrameContext) ctx_ptr;
       sdk.draw_text(12.0, 12.0, "Hello from WASM!", 14.0, sdk.theme_color(.TextPrimary));
   }
   ```
2. Run `nu build.nu plugins` (auto-discovers every `plugins/*/main.onyx` and outputs `build/plugins/<my_plugin>.wasm`).
3. Inside `odit`, press `Ctrl+P` → `Plugin: Open my_plugin.wasm` (or `Plugins: Reload All .wasm Plugins`).
