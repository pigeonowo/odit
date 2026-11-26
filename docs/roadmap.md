# Feature Overview & Developer Notes

This document summarizes the implemented subsystems of **Onyx Editor (`odit`)** and captures toolchain details so contributors can extend the IDE cleanly without re-discovering quirks.

---

## Core Host, Plugin ABI & Editor

- [x] Shared Host/Plugin ABI v1.0.0 (`abi/abi.onyx`), verified in both `-r onyx` and `-r custom` modes.
- [x] `dlopen`-isolated Wasmer C bridge (`native/wasm_host.c` → `bin/odit_wasm_host.so`).
- [x] Guest plugin SDK (`sdk/plugin_sdk.onyx`) with zero OS/WASI imports.
- [x] Raylib dark theme, monospace TTF loader, and immediate UI renderer (`src/theme.onyx`, `src/ui/renderer.onyx`).
- [x] Dockable/tiled window manager with draggable splitter, focus borders, close buttons, status bar, and toast notifications (`src/wm/layout.onyx`).
- [x] Command-first `Ctrl+P` fuzzy command palette supporting host commands, dynamic `.wasm` plugin spawning, and plugin-registered commands (`src/ui/palette.onyx`).
- [x] Core Onyx code editor with line buffer, undo, file I/O, per-line syntax highlighting, gutter markers, and modal Normal/Insert + `Ctrl+S`/`Ctrl+Z` shortcuts (`src/editor/`).
- [x] Verification WASM plugin (`plugins/welcome/main.onyx` → `build/plugins/welcome.wasm`) and headless `--self-test` verification.
- [x] Nushell (`build.nu`) and Bash (`build.sh`) build scripts + `./docs/`.

---

## Onyx LSP Integration

- [x] Non-blocking POSIX process manager (`native_proc_spawn`/`read`/`write`/`poll`/`kill`) in `native/wasm_host.c` keeping child stdin blocking while parent stdout/stderr are `O_NONBLOCK` (avoids Onyx's `os.Process.spawn` `non_blocking_io` making `onyx lsp` exit on `EAGAIN`).
- [x] Async JSON-RPC 2.0 `Content-Length` LSP client (`src/lsp/client.onyx`) spawning `onyx lsp`, syncing `didOpen`/`didChange` (debounced 350ms)/`didSave`, and supporting large files via `dyn_str` (avoiding `tprintf`'s 8 KB stack buffer).
- [x] Live editor integration (`src/editor/view.onyx`, `src/wm/layout.onyx`): diagnostic underlines, inline error messages, gutter dots, `K` hover popup, `F12`/`gd` go-to-definition, `Ctrl+Space` completion popup, `F8` jump-to-next-diagnostic, and status bar `LSP: Ready | E:0 W:0` readout.
- [x] Host ABI opcodes wired live (`src/plugin/host_abi.onyx`): `62` (`ast_get_symbols`), `63` (`ast_get_references`), `64` (`ast_get_diagnostics`) plus `DiagnosticsUpdated` and `SymbolsUpdated` broadcasts to WASM plugins.
- [x] Verified end-to-end in `nu build.nu test` (live `onyx lsp` initializes, indexes workspace, returns symbols, and feeds `welcome.wasm`).

---

## Fuzzy File Finder, File Explorer & Build/Run Plugins

- [x] Workspace recursive file indexer (`src/services/workspace.onyx`), `workspace_find_files` (53), `workspace_write_file` (66), and `file_stat`-backed sorted `workspace_list_dir`.
- [x] Host ABI opcodes `80–84` (`proc_spawn`, `proc_poll_status`, `proc_read_output`, `proc_kill`, `compiler_build`) backed by `native_proc_*` with `waitpid(..., WNOHANG)`.
- [x] **Fuzzy File Finder (`Ctrl+O`)** (`src/ui/file_finder.onyx`) with filename & word-boundary scoring and extension badges.
- [x] **File Explorer WASM Plugin** (`plugins/file_explorer/main.onyx` → `build/plugins/file_explorer.wasm`) — expandable tree for all workspace folders, `sdk.begin_frame()` scratch cleanup, filter input, single-click folder toggle & file open.
- [x] **Build & Run WASM Plugin** (`plugins/build_run/main.onyx` → `build/plugins/build_run.wasm`) — OBV-inspired persistent `odit-build.json`, modes (`build`/`build-exe`/`check`), runtimes (`onyx`/`wasi`/`wasip2`/`js`/`custom`), 10-flag collapsible drawer, 2-stage Build → Run (builds & launches `odit` inside `odit`), elapsed duration timer, and clickable `file.onyx:line:col` error jumps.

## WASM Inspector & Onyx Docs Browser Plugins

- [x] **WAT Disassembly Service** (`src/services/compiler_wat.onyx`, opcode `85`): compiles with `--generate-name-section`, runs `wasm2wat`, caches disassembled module per buffer version, extracts exact `(func ...)` blocks.
- [x] **Onyx Core & Directives Docs Index** (`src/services/docs_index.onyx`, opcode `65`): indexes `~/.onyx/core/**/*.onyx` packages/procs/types + 26 built-in `#` directives (`#format`, `#load`, `#export`, `#foreign`, `#match`, etc.).
- [x] **WASM Inspector WASM Plugin** (`plugins/wasm_inspector/main.onyx` → `build/plugins/wasm_inspector.wasm`): lists active file procedures via LSP `ast_get_symbols` and renders syntax-colored `.wat` instructions.
- [x] **Onyx Docs / Core Browser WASM Plugin** (`plugins/docs_browser/main.onyx` → `build/plugins/docs_browser.wasm`): live search bar, category filter pills (`All`, `#Dir`, `Pkg`, `Procs`, `Types`), and detail signature/summary preview card.

---

## Core Polish & Draggable Window Manager

- [x] **Null-safe WASM Guest String Helpers (`sdk.free_str` / `sdk.copy_str`)**: Fixed WASM out-of-bounds traps caused by calling `str.free("")` (null pointer) or freeing static fallback literals in plugins (e.g., Docs Browser live search).
- [x] **Plugin Mouse-Event Bitmask Fix**: Replaced `cast(u32) abi.MouseButton.Left if cond else 0` ternary expressions in `src/main.onyx` with explicit `if ... do mask |= ...;` statements so `FrameContext.mouse_pressed`/`mouse_down`/`mouse_released`/`key_mods` always transmit accurate bits to plugins (fixing File Explorer folder expand & file open clicks, Docs Browser row selection, and Build & Run clickable errors).
- [x] **Draggable, Dockable & Floating Window Manager (`src/wm/layout.onyx`)**: Drag plugin title bars to swap panes, dock to Left/Right/Bottom edges with live drop-zone previews, or pop out into z-ordered, resizable floating windows; active splitters for all occupied dock zones; layout persisted automatically in `odit-layout.json`; plus Command Palette actions (`Window: Toggle Float / Dock`, `Dock Left/Right/Bottom`, `Reset Workspace Layout`).
- [x] **Global Multi-Project Install (`nu build.nu install`)**: Separates `$ODIT_HOME` (`~/.local/share/odit`), global user config (`~/.config/odit/`), and per-project workspaces (`odit [dir_or_file]`), installing a global `~/.local/bin/odit` launcher and `~/.onyx/lib/*.so`.
- [x] **Interactive `stdin`, Extra Compiler Args, Pixel-Accurate Cursor & Horizontal Scrolling**: Added `proc_write_input` (opcode `86`), live `stdin` bar + unflushed prompt rendering, and persistent `compiler_args` (e.g. `-DComponent_Build`) to Build & Run; fixed monospace token/cursor drift on long lines; added horizontal scrolling (`Shift+Wheel`, auto-scroll, gutter scissor clipping, and `<`/`>` pan buttons in WASM Inspector).
- [x] **Modernized Declarative Plugin SDK (`sdk/plugin_sdk.onyx`)**: Added `@sdk.Plugin.{...}`, `@sdk.PluginInit`/`Shutdown`/`Event`/`Render` auto-exports, typed `sdk.Event` union, trailing `Code`-block macros (`card`, `on_click`, `scroll_area`, `clipped`, `table`, `each_json_object`), and `VStack`/`row_hit` helpers, backed by `-r js --wasm-mvp` for full `runtime.info` support while keeping 100% backward compatibility with manual plugins.
- [x] **Unit & Integration Test Suites**: Added 12-test unit suite in `tests/odit_test.onyx` powered by [`onyx_testing_framework`](https://github.com/pigeonowo/onyx_testing_framework) (`nu build.nu unit`) alongside the headless WASM plugin + Onyx LSP self-test (`nu build.nu test`).

---

## Live Side-by-Side Onyx <-> WASM Debugger

- [x] **Host Debugger Service (`src/services/debugger.onyx`) & Opcodes `90–94`**:
  - Line breakpoint management (`debug_toggle_breakpoint`, `debug_get_breakpoints`) synchronized with red gutter dots in the core editor (via gutter click, `F9`, or inside the Debugger plugin).
  - Compiles & disassembles target functions (`wat_get_function_raw`), maps executable Onyx source lines to WAT instruction ranges (`SourceWatMap`), parses procedure parameters & WASM `(local ...)` slots, and supports `StepOver` (`F10`), `StepInstruction` (`F11`), `StepInto`, `StepOut`, `Continue` (`F5`), and `Stop` (`Shift+F5`).
  - Broadcasts `DebugStateChanged` events to plugins and syncs the editor cursor/gutter highlight to the paused execution line.
- [x] **Live Debugger WASM Plugin (`plugins/debugger/main.onyx` → `build/plugins/debugger.wasm`)**:
  - Declarative `@sdk.Plugin` split view showing Onyx source lines (with clickable breakpoint gutter) alongside syntax-colored mapped `.wat` instructions, active range highlighting, single WAT-instruction stepping, and a bottom Locals/Params inspector.

## Domain-Specific Mini-Apps, Audio, Plugin Settings & Distribution Readiness

- [x] **Portable Tool Discovery**: Removed all hardcoded local user paths; `wasm2wat` resolves via `$WASM2WAT`, `./tools/wasm2wat`, `$ODIT_HOME/tools/wasm2wat`, `~/.local/bin/wasm2wat`, `/usr/bin`, `/usr/local/bin`, or `PATH`, and `README.md` documents installation & search paths for every prerequisite.
- [x] **HTTP-Server Route Viewer (`plugins/http_routes/main.onyx` → `build/plugins/http_routes.wasm`)**: Scans workspace `.onyx` files for `onyx pkg add http-server` routes (`->get/post/put/patch/delete`, `->route(...)`, `@http.route`/`@route`), displays a filterable `Method | Path | Handler | Location` table with color-coded method pills, and jumps to definitions on click.
- [x] **Postgres-ORM Schema Viewer (`plugins/orm_schema/main.onyx` → `build/plugins/orm_schema.wasm`)**: Scans workspace `.onyx` files for `onyx pkg add postgres-orm` `@orm.model` structs and renders visual schema cards (`Table Name`, `StructName`, Onyx types, and `PK`/`AUTO`/`NN`/`UNQ`/`FK` constraint badges) with click-to-jump.

---

## Toolchain & Environment Quirks (Already Solved!)

Keep these in mind when extending the codebase:
1. **Onyx 2611 + `pkg-raylib` 0.0.8**: `lib/github.com/onyx-lang/pkg-raylib/build.onyx` used deprecated `string.concat`. Replaced with `str.concat` (automated in `nu build.nu sync-raylib`).
2. **Toolchain Discovery**: `onyx` is resolved via `$ONYX_PATH/bin/onyx` (defaulting to `~/.onyx/bin/onyx`), `libwasmer.so` via `$WASMER_DIR/lib` / `~/.wasmer/lib` / system library paths, and `wasm2wat` via `$WASM2WAT`, `./tools/wasm2wat`, `$ODIT_HOME/tools/wasm2wat`, `~/.local/bin/wasm2wat`, or `PATH`. Both `build.nu` and `build.sh` configure `ONYX_PATH` automatically.
3. **WASM Plugin Target & Type Info**: Plugins compile with `onyx build -r js --wasm-mvp` (the only built-in zero-WASI Onyx runtime with `Supports_Type_Info :: true`, enabling `@sdk.Plugin` / `@sdk.Plugin*` reflection tags, `core.conv.format`, and `core.encoding.json`). `build.nu` and `build.sh` delete the unused `.wasm.js` glue file immediately after compiling. `native/wasm_host.c` also still supports legacy `-r custom -DComponent_Build` modules (`"cm32p2_memory"`).
4. **Wasmer C API Callback Result Kind**: Wasmer requires host callbacks (`wasm_func_callback_with_env_t`) to explicitly assign `results->data[0].kind = env->ret_kind` before returning. Handled automatically in `native/wasm_host.c`.
5. **Onyx 2611 Syntax & Stdlib Notes**:
   - `#flags` precedes the backing type: `enum #flags (u32) { ... }`.
   - `#export` syntax is `#export "sym_name" local_proc_name`.
   - `Array.remove_at(&arr, idx)` removes by index (`Array.remove` removes by value).
   - Unsized float literals in ternary/const expressions default to `f64`; suffix `f` (`1.5f`) when `f32` is expected.
   - `raylib.SetWindowState(...)` is used after `InitWindow` (`SetConfigFlags` is not bound in `pkg-raylib` 0.0.8).
   - `core.tprintf` uses a fixed 8,196-byte stack buffer; build arbitrarily large payloads (like LSP `didOpen`/`didChange`) with `dyn_str` + `Array.concat`.
   - `os.Process.spawn(..., non_blocking_io = true)` sets `O_NONBLOCK` on the child's stdin fd too, causing `onyx lsp` to see `EAGAIN` and exit. Use `native_proc_spawn` in `native/wasm_host.c` for all interactive/async child processes.
   - **Avoid `cast(T) X if cond else 0` inline ternaries**: In Onyx 2611, `cast(u32) Enum.Val if cond else 0` can evaluate to `0` when combined with bitwise `|`; use explicit `if cond do mask |= cast(u32) Enum.Val;` statements instead.
   - **Never call `str.free` on empty/static strings in WASM plugins**: Calling `raw_free(null)` reads `0xFFFFFFF8` and triggers a Wasmer out-of-bounds trap. Always use `sdk.free_str` and `sdk.copy_str`.
