---
name: odit
description: >-
  Workflows, architecture pointers, and verification steps for developing Onyx Editor
  (odit) and authoring sandboxed WebAssembly (.wasm) plugins in Onyx or Rust. Use when
  working on the odit host, built-in plugins, Host ABI changes, or creating, building,
  debugging, and installing standalone odit plugins.
---

# Onyx Editor (`odit`) & WASM Plugin Development

`odit` is a desktop IDE written in Onyx + Raylib (`pkg-raylib`) where every tool window is a sandboxed WebAssembly (`.wasm`) plugin loaded dynamically via Wasmer with zero OS/WASI imports.

> **Do not rely on memorized ABI details.** Always read the live source of truth listed below before writing or modifying ABI calls, structs, enums, or SDK wrappers.

---

## Where to Look Up the Live ABI & SDKs

### 1. Host & Canonical ABI (`odit`)
- **Repo:** `https://github.com/pigeonowo/odit`
- **Canonical ABI contract (types, enums, POD structs, foreign imports, required exports):** `abi/abi.onyx` and `docs/plugin-abi.md`
- **Host ABI dispatch & implementations:** `src/plugin/host_abi.onyx`
- **Native C Wasmer bridge & import table:** `native/wasm_host.c`
- **In-tree Onyx plugin SDK:** `sdk/plugin_sdk.onyx`
- **Reference core plugins:** `plugins/` (e.g., `plugins/plugin_dev/main.onyx`, `plugins/file_explorer/main.onyx`, `plugins/build_run/main.onyx`, `plugins/debugger/main.onyx`)
- **Architecture & debugger internals:** `docs/architecture.md`, `docs/debugging.md`, `docs/roadmap.md`

### 2. Standalone Onyx Plugin SDK (`odit_onyx_sdk`)
- **Repo:** `https://github.com/pigeonowo/odit_onyx_sdk`
- **Files to inspect:** `abi.onyx`, `plugin_sdk.onyx`, `module.onyx`, `template/odit-plugin.json`, `README.md`
- **Inside a plugin project:** `lib/github.com/pigeonowo/odit_onyx_sdk/` (after `onyx pkg sync`)

### 3. Standalone Rust Plugin SDK (`odit_rust_sdk`)
- **Repo:** `https://github.com/pigeonowo/odit_rust_sdk`
- **Files to inspect:** `odit-sdk/src/abi.rs`, `odit-sdk/src/event.rs`, `odit-sdk/src/ui.rs`, `odit-sdk/src/lib.rs`, `odit-sdk-macros/src/lib.rs`, `examples/hello_plugin/src/lib.rs`

---

## Authoring Standalone Plugins

### Plugin Discovery Priority in `odit`
`odit` scans `.wasm` plugins in priority order (first match wins, so workspace dev builds override global installs):
1. `.odit/plugins/` — workspace-local dev overrides
2. `~/.config/odit/plugins/` — user-installed plugins (survive `odit` upgrades)
3. `$ODIT_HOME/plugins/` or `build/plugins/` — built-in core plugins

### Onyx Plugin Workflow
1. **Scaffold:**
   ```bash
   onyx new odit-plugin my_plugin
   cd my_plugin
   ```
   *(Template lives at `${ONYX_PATH:-$HOME/.onyx}/tools/pkg_templates/odit-plugin.json` and is installed automatically by `nu build.nu install` in `odit`.)*
2. **Build & install:**
   ```bash
   onyx run build.onyx            # compiles build/<name>.wasm
   onyx run build.onyx -- dev     # compiles & copies to .odit/plugins/<name>.wasm
   onyx run build.onyx -- install # compiles & installs to ~/.config/odit/plugins/<name>.wasm
   ```
3. **Compile flags:** Onyx plugins compile with `onyx build -r js --wasm-mvp -o <out.wasm> src/main.onyx` (delete the unused `<out.wasm>.js` glue file).
4. **Live reload inside `odit`:**
   - Open the built-in **Plugin Dev** pane (`Ctrl+P` → `Plugin: Open plugin_dev.wasm`) for one-click **Dev + Reload** / **Install + Reload**, or
   - Press `Ctrl+P` → `Plugins: Reload All .wasm Plugins`, then `Plugin: Open <name>.wasm`.

### Rust Plugin Workflow
1. Create a `cdylib` crate depending on `odit-sdk = { git = "https://github.com/pigeonowo/odit_rust_sdk" }` and annotate your plugin struct with `#[odit_plugin(...)]` (implementing `odit_sdk::Plugin + Default`).
2. Build for bare WASM (zero WASI):
   ```bash
   cargo build --target wasm32-unknown-unknown --release
   cp target/wasm32-unknown-unknown/release/<crate>.wasm ~/.config/odit/plugins/
   ```
3. Reload in `odit` via `Ctrl+P` → `Plugins: Reload All .wasm Plugins`.

### Sandboxing & Onyx Pitfalls
- Plugins must have **zero WASI/OS imports** (only `"odit_host"` imports are allowed by `native/wasm_host.c`, plus `-r js` stubbed `"host"` imports).
- In Onyx plugins, never call `str.free` on empty/static strings (use `sdk.free_str` / `sdk.copy_str` to avoid Wasmer out-of-bounds traps).
- In Onyx 2611, `main :: (args: [] cstr)` under `onyx run` indexes user args starting at **`0`** (after `--`).
- Avoid `cast(T) X if cond else 0` inline ternaries with bitwise `|`; use explicit `if` statements.
- Prefer Onyx's scoped `use x := ...` over `x := ...; defer delete(&x);` for automatic cleanup at scope exit.
- Keep `odit_onyx_sdk` compatible with `onyx lsp` / `onyx check` (which run under default `-r onyx`).

---

## Developing the `odit` Host & Built-In Plugins

### Build, Test & Install Commands
Run from the `odit` repo root (Nushell `build.nu` or Bash `./build.sh`):
```bash
nu build.nu              # Build native Wasmer bridge, all built-in plugins, and host
nu build.nu unit         # Run unit test suite (tests/odit_test.onyx)
nu build.nu test         # Build all + run unit tests + headless ABI & LSP self-test
nu build.nu install      # Install globally (~/.local/share/odit, ~/.onyx/lib, ~/.local/bin/odit, template)
```
Always keep the global install (`~/.local/share/odit` / `~/.onyx/lib`) updated after host or core-plugin changes. Never commit local user config files (`odit.json`, `odit-build.json`, personal notes).

### Adding or Changing a Host ABI Function
When extending the ABI, update all layers consistently and verify with `nu build.nu test`:
1. Declare the foreign import / types in `abi/abi.onyx` and document in `docs/plugin-abi.md`.
2. Register the import name & opcode in `native/wasm_host.c`.
3. Implement the opcode handler in `src/plugin/host_abi.onyx`.
4. Add ergonomic wrappers in `sdk/plugin_sdk.onyx`, and mirror the change to `odit_onyx_sdk` and `odit_rust_sdk` if published.

---

## Verification Checklist

- **`odit` host / core plugin changes:**
  1. `nu build.nu` succeeds with 0 errors.
  2. `nu build.nu unit` (and `nu build.nu test` when touching ABI/LSP/WM) passes completely.
  3. Global install synced (`nu build.nu install` or updated binaries copied to `~/.local/share/odit/`).
- **Standalone Onyx plugin:**
  1. `onyx check src/main.onyx` passes with 0 errors (confirms `onyx lsp` is clean).
  2. `onyx run build.onyx` (or `-- dev` / `-- install`) produces a valid `.wasm`.
- **Standalone Rust plugin:**
  1. `cargo test` passes.
  2. `cargo build --target wasm32-unknown-unknown --release` succeeds.
  3. `wasm2wat <plugin>.wasm | grep -E '^\s*\((import|export)'` confirms only `"odit_host"` imports and all required `odit_*` exports.
