# Onyx Editor (`odit`) — Modular WASM-Plugin Desktop IDE

> **"Window = WASM Plugin"** — A fast, hackable desktop IDE for the [Onyx programming language](https://onyxlang.io), written in **Onyx** and **Raylib** (`pkg-raylib`), where every tool window and inspector is a sandboxed WebAssembly (`.wasm`) mini-app loaded dynamically at runtime.

---

## Highlights

- **Sandboxed WASM Plugin Architecture:** Every panel (`File Explorer`, `Build & Run`, `WASM Inspector`, `Onyx Docs & Core`, and any custom plugin you drop in `plugins/`) compiles to standalone WebAssembly (`-r js --wasm-mvp`) with zero OS/WASI imports and communicates exclusively through a versioned Host ABI ([`abi/abi.onyx`](abi/abi.onyx)) and declarative `@sdk.Plugin` SDK ([`sdk/plugin_sdk.onyx`](sdk/plugin_sdk.onyx)).
- **Draggable, Dockable & Floating Window Manager:** Drag any plugin title bar to **swap** panes, snap into **Left**, **Right**, or **Bottom** dock zones with live drop previews, or pop out into z-ordered, corner-resizable **floating windows**. Layout persists automatically in `odit-layout.json`.
- **Core Onyx Code Editor:** Custom line buffer with multi-step undo, per-line Onyx syntax highlighting, modal Normal/Insert editing (toggleable), pixel-accurate cursor & click alignment, gutter markers, and horizontal scrolling (`Shift+Wheel` + auto-scroll).
- **Onyx LSP Integration:** Non-blocking JSON-RPC client for `onyx lsp` powering live diagnostic underlines, inline error messages, gutter dots, hover tooltips (`K`), go-to-definition (`F12` / `gd`), autocomplete (`Ctrl+Space`), and symbol feeds to WASM plugins.
- **Built-In Core Mini-Apps (`.wasm` Plugins):**
  - **File Explorer** — Full workspace directory tree with live filter, single-click folder expand/collapse, and instant file opening.
  - **Build & Run** — Persistent per-project `odit-build.json`, OBV-inspired compiler flags drawer, extra `compiler_args` (e.g. `-DComponent_Build`), 2-stage Build → Run pipeline (capable of building & running `odit` inside `odit`), clickable `file.onyx:line:col` error jumps, and **interactive `stdin`** with live unflushed prompt rendering.
  - **WASM Inspector (`.wat`)** — Lists functions from the active file via Onyx LSP and disassembles them to syntax-colored WebAssembly Text Format (`.wat`) with horizontal scrolling & pan controls.
  - **Onyx Docs & Core Browser** — Live searchable reference indexing `~/.onyx/core` packages/procs/types and built-in `#` compiler directives.
  - **Live Debugger (`Onyx <-> .wat`)** — Toggle breakpoints via editor gutter clicks or `F9`, customize run commands (`--param ...`), step through Onyx lines (`F10`), local/indirect calls (`In`/`Out`), or individual `.wat` instructions (`F11`), and inspect live variable & WASM local slot values.
  - **HTTP Routes Viewer (`http-server`)** — Scans `onyx pkg add http-server` projects (`router->get/post/put/patch/delete`, `router->route`, `@http.route`) into a filterable `Method | Path | Handler | Location` table with click-to-jump.
  - **Postgres-ORM Schema Viewer (`postgres-orm`)** — Scans `onyx pkg add postgres-orm` `@orm.model` structs and renders visual database schema cards (`PK`, `AUTO`, `NN`, `UNQ`, `FK` badges + Onyx types) with click-to-jump.
  - **Advent of Code Runner** — Day-by-day `test`/`real` runner with `.odit/aoc_state.json` persistence, expected vs. actual comparison, clipboard copy, and per-day boilerplate generator.
- **Command-First & Configurable:** Fuzzy Command Palette (`Ctrl+P`), Fuzzy File Finder (`Ctrl+O`), live Settings panel (`Ctrl+,`) with 4 built-in themes (`mocha`, `tokyo_night`, `gruvbox`, `light`), font size & UI scaling, and global CLI install (`odit [dir_or_file]`).

---

## Project Status

| Phase | Scope | Status |
| :--- | :--- | :--- |
| **Phase 1** | Native Onyx + Raylib host, draggable/dockable/floating window manager, `Ctrl+P` Command Palette, Settings (`Ctrl+,`), Onyx code editor, Wasmer `.wasm` plugin loader, Host ABI v1.0, Guest SDK | **Complete** |
| **Phase 2** | Non-blocking Onyx LSP client (`onyx lsp`): diagnostics, hover (`K`), go-to-definition (`F12`/`gd`), completion (`Ctrl+Space`), `ast_get_*` Host ABI | **Complete** |
| **Phase 3.1** | Fuzzy File Finder (`Ctrl+O`), **File Explorer** `.wasm` plugin, **Build & Run** `.wasm` plugin (`odit-build.json`, flags drawer, clickable errors, interactive `stdin`) | **Complete** |
| **Phase 3.2** | **WASM Inspector** (`.wat` disassembly `.wasm` plugin) & **Onyx Docs / Core Browser** `.wasm` plugin | **Complete** |
| **Phase 4.1** | **Live Side-by-Side Onyx/WASM Debugger** (`F5`/`F9`/`F10`/`F11`, custom run command, live value inspection, `debugger.wasm`) + **Advent of Code** (`aoc.wasm`) | **Complete** |
| **Phase 4.2** | Domain Plugins: **HTTP Routes Viewer** (`http_routes.wasm` for `http-server`) & **Postgres-ORM Schema Viewer** (`orm_schema.wasm` for `postgres-orm`) | **Complete** |

---

## Quick Start

### Prerequisites & Tool Installation

ODIT contains no hardcoded user paths and discovers external tools automatically via environment variables and standard locations:

1. **Onyx Toolchain** (`onyx`, tested on `2611`):
   - Install from [onyxlang.io](https://onyxlang.io) (`curl https://get.onyxlang.io -sSfL | sh`).
   - Searched at `$ONYX_PATH/bin/onyx` (defaults to `~/.onyx/bin/onyx`).
2. **Wasmer C Library (`libwasmer.so`)** (loaded dynamically via `dlopen` to sandbox `.wasm` plugins):
   - Install from [wasmer.io](https://wasmer.io) (`curl https://get.wasmer.io -sSfL | sh`).
   - Searched at `$WASMER_DIR/lib/libwasmer.so`, `~/.wasmer/lib/libwasmer.so`, `/usr/local/lib/libwasmer.so`, `/usr/lib/libwasmer.so`, or system `LD_LIBRARY_PATH`.
3. **WABT (`wasm2wat`)** *(required for WASM Inspector & Live Debugger `.wat` disassembly)*:
   - Install via your package manager (`sudo apt install wabt`, `brew install wabt`, `sudo pacman -S wabt`) **or** place the `wasm2wat` binary into `./tools/wasm2wat`, `~/.local/share/odit/tools/wasm2wat`, `~/.local/bin/wasm2wat`, or point `$WASM2WAT=/path/to/wasm2wat`.
4. **C Compiler (`gcc` or `clang`)** — used to compile `bin/odit_wasm_host.so` and `bin/onyx_raylib.so`.
5. **Nushell (`nu`)** or **Bash** — to run `build.nu` or `build.sh`.

### Build, Test & Run (Nushell)
```nu
nu build.nu sync-raylib  # First-time setup: syncs packages & patches pkg-raylib for Onyx 2611
nu build.nu              # Build native Wasmer bridge, all WASM plugins, and host
nu build.nu unit         # Run onyx_testing_framework unit suite (tests/odit_test.onyx)
nu build.nu test         # Run unit tests + headless WASM plugin ABI & LSP self-test
nu build.nu run          # Launch Onyx Editor in this repo
nu build.nu install      # Install `odit` globally to ~/.local/bin/odit
```

### Build, Test & Run (Bash)
```bash
./build.sh sync-raylib
./build.sh
./build.sh test
./build.sh run
./build.sh install
```

### Use `odit` in Any Onyx Project
After running `nu build.nu install` (or `./build.sh install`), ensure `~/.local/bin` is on your `PATH`, then open any project from anywhere:
```bash
cd ~/my-onyx-project && odit
odit ~/my-onyx-project
odit ~/my-onyx-project/main.onyx
```
In installed mode:
- **Plugins & Host WASM:** `$ODIT_HOME` (`~/.local/share/odit`)
- **Native `.so` Libraries:** `$ONYX_PATH/lib` (`~/.onyx/lib`)
- **Global User Settings & Layout:** `~/.config/odit/odit.json` and `~/.config/odit/odit-layout.json`
- **Project Build Config:** `<project>/odit-build.json`

---

## Keybindings & Controls

| Shortcut / Input | Action |
| :--- | :--- |
| `Ctrl+P` | Open / close the fuzzy **Command Palette** (host actions, window docking, discovered `.wasm` plugins, and plugin-registered commands) |
| `Ctrl+O` | Open / close the **Fuzzy File Finder** to jump to any workspace file |
| `Ctrl+,` | Open / close the interactive **Settings Panel** |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+0` | Increase / decrease / reset editor font size (`10px – 36px`, auto-saved) |
| `F12` / `gd` | **LSP: Go to Definition** |
| `K` (Normal) | **LSP: Hover Info** tooltip |
| `Ctrl+Space` | **LSP: Autocomplete** (`Up`/`Down`, `Enter`/`Tab`, `Esc`) |
| `F8` | **LSP: Jump to Next Diagnostic** |
| `F9` / Gutter Click | **Debugger: Toggle Breakpoint** on line (red gutter dot) |
| `F5` / `Shift+F5` | **Debugger: Start / Continue** / **Stop** session |
| `F10` / `F11` | **Debugger: Step Over (Onyx line)** / **Step WAT Instruction** |
| `Ctrl+Tab` | Cycle focus between the Code Editor and plugin panes |
| `Ctrl+S` / `Ctrl+Z` | Save active buffer (saving `odit.json` reloads live!) / Undo |
| `Esc` | Return to **NORMAL** mode (or close overlays / unfocus plugin input) |
| **Normal mode** | `h/j/k/l` or Arrows move, `w`/`b` word jump, `0`/`$` line start/end, `g`/`G` top/bottom, `i`/`a`/`o`/`O` enter **INSERT**, `x` delete char, `dd` delete line, `u` undo |
| **Horizontal Scroll** | `Shift + MouseWheel` in the Editor or WASM Inspector (plus `<` / `>` pan buttons in WASM Inspector) |
| **Window Management** | **Drag any plugin title bar** to swap panes, dock to **Left / Right / Bottom** edges, or release in the center to **Float**; drag splitters or floating bottom-right corner grips to resize; click header `[ ]`/`v` to toggle Float/Dock or `x` to close |

---

## Repository Structure

```text
odit/
├── abi/abi.onyx                  # Single source of truth: Host <-> WASM Plugin ABI v1.0.0
├── sdk/plugin_sdk.onyx           # Guest SDK for writing zero-WASI Onyx WASM plugins
├── native/wasm_host.c            # C Wasmer bridge & POSIX process manager -> bin/odit_wasm_host.so
├── src/
│   ├── main.onyx                 # Native host entrypoint, CLI, event loop, plugin lifecycle, --self-test
│   ├── settings.onyx             # Persistent settings (`odit.json` / `~/.config/odit/odit.json`)
│   ├── theme.onyx                # 4 theme presets, crisp 48px TTF font loader, Raylib draw helpers
│   ├── wm/layout.onyx            # Draggable, dockable & floating window manager + `odit-layout.json`
│   ├── lsp/client.onyx           # Non-blocking JSON-RPC 2.0 client for `onyx lsp`
│   ├── services/
│   │   ├── workspace.onyx        # Recursive workspace file indexer
│   │   ├── compiler_wat.onyx     # `onyx --generate-name-section` + `wasm2wat` disassembly service
│   │   ├── debugger.onyx         # Live Onyx <-> WASM breakpoint & stepping service (opcodes 90-94)
│   │   └── docs_index.onyx       # Onyx `~/.onyx/core` & `#` directive documentation indexer
│   ├── ui/
│   │   ├── renderer.onyx         # Host Raylib immediate UI renderer for WASM plugins (+ headless mode)
│   │   ├── palette.onyx          # Command-first `Ctrl+P` fuzzy command palette
│   │   ├── file_finder.onyx      # `Ctrl+O` fuzzy file finder
│   │   └── settings_view.onyx    # `Ctrl+,` interactive settings modal
│   ├── editor/
│   │   ├── buffer.onyx           # Line buffer, file I/O, undo stack, gutter markers
│   │   ├── lexer.onyx            # Per-line Onyx syntax highlighter
│   │   └── view.onyx             # Editor viewport, horizontal/vertical scroll, modal & standard editing
│   └── plugin/
│       ├── loader.onyx           # Foreign bindings to `odit_wasm_host.so` & guest memory helpers
│       └── host_abi.onyx         # Exported `__odit_host_dispatch` handling all `odit_host` opcodes
├── plugins/
│   ├── file_explorer/main.onyx   # File Explorer WASM plugin
│   ├── build_run/main.onyx       # Build & Run WASM plugin (persistent config + interactive stdin)
│   ├── wasm_inspector/main.onyx  # Live `.wat` WebAssembly Inspector WASM plugin
│   ├── docs_browser/main.onyx    # Onyx Core & `#` Directives Docs Browser WASM plugin
│   ├── debugger/main.onyx        # Live Side-by-Side Onyx <-> WAT Debugger WASM plugin
│   ├── http_routes/main.onyx     # HTTP-Server Route Viewer WASM plugin (`http-server`)
│   ├── orm_schema/main.onyx      # Postgres-ORM Schema Viewer WASM plugin (`postgres-orm`)
│   ├── aoc/main.onyx             # Advent of Code Runner & Dashboard WASM plugin
│   └── welcome/main.onyx         # Minimal reference/verification WASM plugin
├── tests/
│   └── odit_test.onyx            # Unit test suite powered by `onyx_testing_framework`
├── docs/
│   ├── architecture.md           # Host, Wasmer isolation, WM, LSP & Editor internals
│   ├── debugging.md              # Quick-start guide for the Live Onyx <-> WASM Debugger
│   ├── plugin-abi.md             # Complete ABI v1.0 opcode reference & plugin authoring guide
│   └── roadmap.md                # Phase 1–4 roadmap & Onyx 2611 / Wasmer engineering notes
├── build.nu                      # Nushell build/test/run/install script
└── build.sh                      # Bash build/test/run/install script
```

---

## Documentation

- **[`docs/architecture.md`](docs/architecture.md)** — How the native Onyx host, `dlopen`-isolated Wasmer C bridge, draggable window manager, Onyx LSP client, UI renderer, and code editor fit together.
- **[`docs/debugging.md`](docs/debugging.md)** — How to get started with breakpoints, Onyx line stepping, and `.wat` instruction stepping in the Live Debugger.
- **[`docs/plugin-abi.md`](docs/plugin-abi.md)** — Complete ABI v1.0 contract (`abi/abi.onyx`), opcode map, memory safety guidelines, and a quickstart template for authoring your own `.wasm` plugin.
- **[`docs/roadmap.md`](docs/roadmap.md)** — Phase 1–4 status, Phase 4 design notes, and Onyx 2611 / Wasmer toolchain quirks already solved.
