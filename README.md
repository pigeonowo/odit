# Onyx Editor (`odit`) — Modular WASM-Plugin Desktop IDE

**`odit`** is a desktop IDE for the [Onyx programming language](https://onyxlang.io) written in **Onyx** using **Raylib** (`pkg-raylib`) for native windowing and 2D rendering.

Its core architectural principle is **"Window = WASM Plugin"**: every panel, tool window, and visual inspector is an independent mini-app compiled to WebAssembly (`.wasm`) and dynamically loaded, sandboxed, and rendered by the native host editor via a versioned Host ABI ([`abi/abi.onyx`](abi/abi.onyx)).

---

## Project Status

| Phase | Scope | Status |
| :--- | :--- | :--- |
| **Phase 1** | Host app, Raylib tiled window manager, `Ctrl+P` Command Palette, Settings (`Ctrl+,`, `odit.json`), Onyx code editor (syntax highlighting + modal editing), Wasmer `.wasm` plugin loader, Host ABI v1.0, Guest SDK, and verification plugin (`welcome.wasm`) | **Complete** |
| **Phase 2** | Onyx LSP (`onyx lsp`) integration over non-blocking JSON-RPC stdio (diagnostics underlines & gutter markers, hover `K`, go-to-definition `gd`/`F12`, completion `Ctrl+Space`, `ast_get_symbols`/`references`/`diagnostics` Host ABI) | **Complete** |
| **Phase 3.1** | **Fuzzy File Finder (`Ctrl+O`)**, **File Explorer** (`.wasm` plugin — all folders, instant open-on-click), **Build & Run** (`.wasm` plugin with persistent `odit-build.json`, OBV-style flags, 2-stage Build → Run to build & run `odit` inside `odit`, clickable error jumps) | **Complete** |
| **Phase 3.2** | **WASM Inspector** (`.wat` disassembly `.wasm` plugin via `onyx --generate-name-section` + `wasm2wat`) & **Onyx Docs / Core Browser** (`.wasm` plugin indexing `~/.onyx/core` & `#` directives) | **Complete** |
| **Phase 4** | Live Side-by-Side Onyx/WASM Debugger + Domain Plugins (HTTP-Server Route Viewer, ORM Schema Viewer) | Planned |

---

## Quick Start

### Prerequisites
- **Onyx Toolchain** (tested on `2611`, `ONYX_PATH=~/.onyx`)
- **Wasmer** shared library (`~/.wasmer/lib/libwasmer.so`, loaded dynamically via `dlopen`)
- **GCC / Clang** (to build `bin/odit_wasm_host.so` and `bin/onyx_raylib.so`)
- **WABT (`wasm2wat`)** (optional in Phase 1; used in Phase 3/4 for `.wat` disassembly)
- **Nushell** (`nu`) or **Bash**

### Build & Run (Nushell)
```nu
nu build.nu              # Build native Wasmer bridge, all WASM plugins, and host
nu build.nu test         # Run headless Host <-> WASM Plugin ABI self-test
nu build.nu run          # Launch Onyx Editor (opens src/main.onyx)
nu build.nu run path.onyx
nu build.nu install      # Install `odit` globally to ~/.local/bin/odit
```

### Use `odit` in Any Project
After running `nu build.nu install` (or `./build.sh install`), you can open any Onyx project from anywhere:
```bash
cd ~/my-onyx-project && odit
odit ~/my-onyx-project
odit ~/my-onyx-project/main.onyx
```
Installed mode loads plugins from `$ODIT_HOME/plugins` (`~/.local/share/odit/plugins`), native `.so` libraries from `$ONYX_PATH/lib`, global settings/layout from `~/.config/odit/`, and project build settings from `<project>/odit-build.json`.

### Build & Run (Bash)
```bash
./build.sh               # Build all
./build.sh test          # Run headless self-test
./build.sh run           # Launch Onyx Editor
```

> **Note for fresh clones (Onyx 2611 + `pkg-raylib` 0.0.8):** `pkg-raylib`'s `build.onyx` calls legacy `string.concat` instead of `str.concat`. Running `nu build.nu sync-raylib` (or `./build.sh sync-raylib`) automatically patches it and builds `bin/onyx_raylib.so`.

---

## Keybindings & Controls (Phase 1)

| Shortcut | Action |
| :--- | :--- |
| `Ctrl+P` | Open / close the fuzzy **Command Palette** (includes built-in commands, Settings actions, discovered `.wasm` plugins, and plugin-registered commands) |
| `Ctrl+O` | Open / close the **Fuzzy File Finder** to jump to any workspace file immediately |
| `Ctrl+,` | Open / close the interactive **Settings Panel** (or click `Settings Ctrl+,` in the top bar) |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+0` | Increase / decrease / reset editor font size (`10px – 36px`, auto-saved to `odit.json`) |
| `F12` / `gd` | **LSP: Go to Definition** (opens target file and jumps cursor) |
| `K` (Normal) | **LSP: Hover Info** tooltip for symbol under cursor |
| `Ctrl+Space` | **LSP: Autocomplete** popup (`Up`/`Down` navigate, `Enter`/`Tab` insert, `Esc` close) |
| `F8` | **LSP: Jump to Next Diagnostic** |
| `Ctrl+Tab` | Cycle focus between the Code Editor and tiled WASM plugin panes |
| `Ctrl+S` | Save active buffer (saving `odit.json` reloads settings live!) |
| `Ctrl+Z` | Undo |
| `Esc` | Return to **NORMAL** mode (or close Command Palette / Settings / unfocus plugin input) |
| **Normal mode** | `h/j/k/l` or Arrows move, `w`/`b` word jump, `0`/`$` line start/end, `g`/`G` top/bottom, `i`/`a`/`o`/`O` enter **INSERT**, `x` delete char, `dd` delete line, `u` undo |
| **Mouse / Windows** | **Drag any plugin title bar** to reorder/swap panes, snap to **Left / Right / Bottom** dock edges, or drop in the center to **Float** freely; drag splitters or floating bottom-right corner grips to resize; click header `[ ]`/`v` to toggle Float/Dock or `x` to close (auto-saved to `odit-layout.json`) |

### Settings (`odit.json`)
Settings are persisted automatically in `odit.json` and take effect live (including in WASM plugins):
- **Typography & Scaling:** `editor_font_size` (default `18px`), `ui_scale` (default `1.15x`), `line_height_factor` (default `1.4x`), `font_path` (optional custom `.ttf` path)
- **Color Themes:** `mocha` (Catppuccin Mocha), `tokyo_night`, `gruvbox`, `light`
- **Editor Behavior:** `tab_size` (`2`/`4`/`8`), `modal_editing`, `show_line_numbers`, `highlight_current_line`, `auto_indent`
- **Layout:** `split_ratio`, `target_fps`

---

## Repository Structure

```text
odit/
├── abi/abi.onyx              # Single source of truth: Host <-> WASM Plugin ABI v1.0.0
├── sdk/plugin_sdk.onyx       # Ergonomic Onyx guest SDK for writing WASM plugins
├── native/wasm_host.c        # C bridge wrapping libwasmer.so -> bin/odit_wasm_host.so
├── src/
│   ├── main.onyx             # Native host entrypoint, event loop, plugin lifecycle, --self-test
│   ├── theme.onyx            # Dark theme palette, TTF monospace font loader, safe text helpers
│   ├── wm/layout.onyx        # Tiled multi-pane window manager, splitter, status bar, toasts
│   ├── ui/
│   │   ├── renderer.onyx     # Host Raylib renderer for plugin UI primitives & immediate widgets
│   │   └── palette.onyx      # Command-first Ctrl+P fuzzy command palette
│   ├── editor/
│   │   ├── buffer.onyx       # Line-based text buffer, file I/O, undo stack, gutter markers
│   │   ├── lexer.onyx        # Per-line Onyx syntax highlighter
│   │   └── view.onyx         # Code editor viewport, gutter, modal & standard editing
│   └── plugin/
│       ├── loader.onyx       # Foreign bindings to odit_wasm_host.so & guest memory helpers
│       └── host_abi.onyx     # Exported `__odit_host_dispatch` handling all `odit_host` opcodes
├── plugins/
│   └── welcome/main.onyx     # Phase 1 verification plugin ("WASM Inspector (Demo)")
├── docs/
│   ├── architecture.md       # Deep dive: Host, Wasmer bridge, isolation, UI & Editor internals
│   ├── plugin-abi.md         # Complete ABI v1.0 reference & guide to creating WASM plugins
│   └── roadmap.md            # Phase 1–4 implementation plan, extension points & toolchain notes
├── build.nu                  # Nushell build/test/run script
└── build.sh                  # Bash build/test/run script
```

---

## Documentation for Humans & AI Agents

- **[`docs/architecture.md`](docs/architecture.md)** — How the native Onyx host, `dlopen`-isolated Wasmer C bridge, UI renderer, window manager, and code editor fit together.
- **[`docs/plugin-abi.md`](docs/plugin-abi.md)** — Exact ABI contract (`abi/abi.onyx`), opcode map, memory safety rules, and how to author a new `.wasm` plugin in minutes.
- **[`docs/roadmap.md`](docs/roadmap.md)** — Detailed blueprint for Phases 2, 3, and 4, including exact toolchain flags and known Onyx 2611 / Wasmer quirks already solved.
