# Host-to-WASM Plugin ABI Reference (v1.0.0)

The canonical ABI definition lives in [`abi/abi.onyx`](../abi/abi.onyx). Both the native host (`-r onyx`) and every WASM plugin (`-r js --wasm-mvp` or `-r custom`) `#load` this exact file.

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
4. **Colors**: Packed `u32` in `0xRRGGBBAA` order (`abi.rgba(r, g, b, a)`). Prefer `sdk.theme_color(.Role)` to match the active IDE theme.
5. **Guest Memory Hygiene (`sdk/plugin_sdk.onyx`)**:
   - Call `sdk.begin_frame()` at the start of `odit_plugin_render` to clear `context.temp_allocator`.
   - Always use `sdk.free_str` / `sdk.copy_str` instead of raw `str.free` so empty (`null`) or static strings never trigger a WASM out-of-bounds trap.

---

## Required Plugin Exports (Guest → Host)

When using the declarative decorators in [`sdk/plugin_sdk.onyx`](../sdk/plugin_sdk.onyx) (`@sdk.Plugin.{...}` + `@sdk.PluginInit`/`Shutdown`/`Event`/`Render`), **all 9 WASM exports are generated automatically** (legacy manual `#export "odit_plugin_*"` plugins are also supported automatically whenever `main.plugin_manifest` is defined):

| Export | Provided By | Purpose |
| :--- | :--- | :--- |
| `_initialize()` | Onyx `-r js` / `sdk` | Initializes Onyx guest heap & runtime |
| `odit_abi_version() -> u32` | `sdk/plugin_sdk.onyx` | Returns `0x00010000` (v1.0.0) |
| `odit_alloc(size: u32) -> u32` | `sdk/plugin_sdk.onyx` | Allocates guest memory for host writes |
| `odit_free(ptr: u32)` | `sdk/plugin_sdk.onyx` | Frees guest memory |
| `odit_plugin_manifest(out_ptr, out_cap) -> i32` | Auto (`@sdk.Plugin`) or Manual | Writes JSON `PluginManifest` |
| `odit_plugin_init(window_id) -> i32` | Auto (`@sdk.PluginInit`) or Manual | Init state, register `Ctrl+P` commands |
| `odit_plugin_shutdown(window_id)` | Auto (`@sdk.PluginShutdown`) or Manual | Clean up state/textures/processes |
| `odit_plugin_event(window_id, ev_type, ptr, len)` | Auto (`@sdk.PluginEvent`) or Manual | Handle subscribed `EventMask` events |
| `odit_plugin_render(window_id, ctx_ptr)` | Auto (`@sdk.PluginRender`) or Manual | Render immediate UI for this frame |

### Modern SDK Primitives (`odit.sdk`)

- **Typed `sdk.Event` union:** `.ActiveFileChanged(str)`, `.BufferModified(BufferModifiedEvent)`, `.CursorMoved(abi.EditorCursor)`, `.DiagnosticsUpdated(str)`, `.SymbolsUpdated(str)`, `.Command(str)` (extracted command ID), `.WorkspaceChanged(str)`, `.KeyInput(abi.KeyEvent)`, `.Raw(RawEvent)`.
- **Scoped Trailing `Code`-Block Macros:**
  - `sdk.card(x, y, w, h, title) { ... }` — themed surface card with optional header
  - `sdk.on_click(id, x, y, w, h, label, style) { ... }` — button + inline click block
  - `sdk.scroll_area(id, x, y, w, h, content_h, &scroll_y) { ... }` — auto-matched scroll begin/end
  - `sdk.clipped(x, y, w, h) { ... }` — auto-matched scissor clip push/pop
  - `sdk.table(id, x, y, w, h, cols, headers, widths) { ... }` + `sdk.table_row(...)`
  - `sdk.each_json_object(js, [obj] { ... })` — iterate `{...}` objects in host JSON responses
- **Layout & Hit-Testing Helpers:** `sdk.vstack(x, y, w, gap)` (`VStack.next`, `spacer`, `text`, `button`, `divider`) and `sdk.row_hit(ctx, x, y, w, h, scroll_y)`.

---

## Host Import Opcodes (`#foreign "odit_host"`)

### A. Core & Command Palette (`1..19`)
| Opcode | Function | Status |
| :---: | :--- | :---: |
| `1` | `host_log(level, msg_ptr, msg_len)` | Live |
| `2` | `host_set_window_title(title_ptr, title_len)` | Live |
| `3` | `host_request_redraw()` | Live |
| `4` | `host_notify(severity, msg_ptr, msg_len)` | Live |
| `5` | `host_register_command(id..., label..., shortcut...) -> u32` | Live |
| `6` | `host_execute_command(id..., arg...) -> i32` | Live |
| `7` | `host_spawn_plugin(plugin_id...) -> u32` | Live |
| `8` | `host_set_clipboard(text_ptr, text_len)` | Live |
| `9` | `host_register_setting(key..., label..., kind, default...) -> u32` | Live |
| `10` | `host_get_setting(key..., out_ptr, out_cap) -> i32` | Live |
| `11` | `host_set_setting(key..., val_ptr, val_len) -> i32` | Live |

### B. UI Drawing & Immediate Widgets (`20..49`)
All coordinates are window-local `(0, 0) .. (ctx.width, ctx.height)` and clipped to the pane.

| Opcode | Function | Status |
| :---: | :--- | :---: |
| `20–21` | `ui_push_clip` / `ui_pop_clip` | Live |
| `22–23` | `ui_measure_text` / `ui_get_theme_color` | Live |
| `24–28` | `ui_draw_rect`, `ui_draw_line`, `ui_draw_arrow`, `ui_draw_circle`, `ui_draw_text` | Live |
| `29–32` | `ui_pill`, `ui_button`, `ui_input`, `ui_dropdown` | Live |
| `33–34` | `ui_scroll_area_begin` / `ui_scroll_area_end` | Live |
| `35–37` | `ui_table_begin` / `ui_table_row` / `ui_table_end` | Live |
| `38–40` | `ui_texture_load`, `ui_texture_unload`, `ui_draw_texture` | Live |
| `41–44` | `audio_load(path...)`, `audio_play(handle, volume)`, `audio_unload(handle)`, `audio_load_memory(data..., ext...)` | Live |

### C. Workspace, Editor, AST/LSP & Docs (`50..79`)
| Opcode | Function | Status |
| :---: | :--- | :---: |
| `50–52` | `workspace_get_root`, `workspace_list_dir`, `workspace_read_file` | Live |
| `53` | `workspace_find_files(ext...)` | Live |
| `54–61` | `editor_get_active_path`, `editor_get_buffer_len`, `editor_get_buffer`, `editor_get_cursor`, `editor_open_file`, `editor_set_cursor`, `editor_set_gutter_marker`, `editor_clear_gutter_markers` | Live |
| `62–64` | `ast_get_symbols`, `ast_get_references`, `ast_get_diagnostics` | Live |
| `65` | `docs_search(query...)` | Live |
| `66` | `workspace_write_file(rel_path..., data...)` | Live |

### D. Process, Compiler & WAT (`80..89`)
| Opcode | Function | Status |
| :---: | :--- | :---: |
| `80–83` | `proc_spawn`, `proc_poll_status`, `proc_read_output`, `proc_kill` | Live |
| `84` | `compiler_build(entry..., target, flags)` | Live |
| `85` | `compiler_get_function_wat(file..., func..., out...)` | Live |
| `86` | `proc_write_input(handle, data_ptr, data_len)` | Live |

### E. Live Debugger (`90..99`)
| Opcode | Function | Status |
| :---: | :--- | :---: |
| `90–94` | `debug_toggle_breakpoint`, `debug_get_breakpoints`, `debug_start`, `debug_command`, `debug_get_state` | Live |

---

## Authoring a New WASM Plugin (Modern Declarative SDK)

1. Create `plugins/<my_plugin>/main.onyx`:
   ```onyx
   #load "../../abi/abi.onyx"
   #load "../../sdk/plugin_sdk.onyx"

   use odit.abi
   use odit.sdk

   @sdk.Plugin.{
       id             = "odit.my_plugin",
       title          = "My Plugin",
       version        = "0.1.0",
       default_slot   = 0,
       default_width  = 360.0f,
       default_height = 240.0f,
       event_mask     = 67, // ActiveFileChanged | BufferModified | CommandInvoked
       allow_multiple = false,
   }
   MyPlugin :: struct {
       greeting_count: u32;
   }

   @sdk.PluginInit
   init :: (self: &MyPlugin, window_id: u32) -> i32 {
       sdk.register_command("my_plugin.hello", "My Plugin: Say Hello", "");
       return 0;
   }

   @sdk.PluginEvent
   on_event :: (self: &MyPlugin, window_id: u32, event: sdk.Event) {
       switch event {
           case .Command as cmd {
               if cmd == "my_plugin.hello" do self.greeting_count += 1;
           }
           case _ {}
       }
   }

   @sdk.PluginRender
   render :: (self: &MyPlugin, window_id: u32, ctx: &abi.FrameContext) {
       v := sdk.vstack(12.0f, 12.0f, ctx.width - 24.0f, 8.0f);
       cy := v->next(72.0f);
       sdk.card(12.0f, cy, v.w, 72.0f, "My Plugin") {
           sdk.draw_text(22.0f, cy + 36.0f, "Hello from WASM!", 14.0f, sdk.theme_color(.TextPrimary));
       }
       by := v->next(28.0f);
       sdk.on_click(1, 12.0f, by, 120.0f, 28.0f, "Greet", .Primary) {
           self.greeting_count += 1;
           sdk.notify(.Info, "Hello from WASM!");
       }
   }
   ```
   - `sdk/plugin_sdk.onyx` automatically exports `odit_plugin_manifest`, `odit_plugin_init`, `odit_plugin_shutdown`, `odit_plugin_event`, and `odit_plugin_render` (and calls `sdk.begin_frame()` automatically before render).
   - Available scoped trailing `Code`-block macros: `sdk.card`, `sdk.scroll_area`, `sdk.clipped`, `sdk.table`, `sdk.on_click`, and `sdk.each_json_object`.
   - Available layout & hit-testing helpers: `sdk.vstack` (`VStack`), `sdk.row_hit`, and `sdk.table_row`.
   - Legacy manual `#export "odit_plugin_*"` plugins remain 100% supported automatically.
2. Run `nu build.nu plugins` (or `nu build.nu install` to update your global install) — every `plugins/*/main.onyx` is compiled automatically to `build/plugins/<my_plugin>.wasm`.
3. Inside `odit`, press `Ctrl+P` → `Plugin: Open my_plugin.wasm` (or `Plugins: Reload All .wasm Plugins`).
