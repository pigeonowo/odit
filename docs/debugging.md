# Getting Started with Debugging in ODIT

ODIT includes a **Live Side-by-Side Onyx <-> WASM Debugger** (`debugger.wasm`) that lets you set line breakpoints in Onyx source code, inspect the compiled WebAssembly Text Format (`.wat`) for the active function, and step through either Onyx source lines or individual WAT instructions.

---

## Prerequisites

- **Onyx Toolchain** (`onyx` on `PATH` or `$ONYX_PATH/bin/onyx`)
- **WABT (`wasm2wat`)** installed (used to disassemble compiled `.wasm` into `.wat`)

---

## Quick Start (5 Steps)

1. **Open an Onyx file** in the editor (e.g. via `Ctrl+O` Fuzzy File Finder or the File Explorer pane).
2. **Open the Live Debugger plugin**:
   - Press `Ctrl+P` to open the Command Palette.
   - Type `debugger` and select **`Plugin: Open debugger.wasm`**.
   - *(Tip: Drag the Live Debugger title bar to dock it to the Right or Bottom, or release in the center to float it.)*
3. **Set a breakpoint**:
   - **Click the editor gutter** (left of the line numbers) on any line inside a procedure, **or**
   - Move the cursor to a line and press **`F9`**, **or**
   - Click any line directly inside the left pane of the Live Debugger plugin.
   - A red dot appears in both the editor gutter and the Live Debugger.
4. **Configure the Run Command (optional) & Start debugging**:
   - Use the **`Cmd:`** input bar at the top of the Live Debugger to customize the command and compiler flags—for example, in an Advent of Code project:
     ```text
     onyx run --param 'aoc.day=1' --param 'aoc.test=true' src/day1.onyx
     ```
   - Press **`Enter`** in the `Cmd:` box, press **`F5`**, or click **`Start`** in the toolbar.
   - ODIT compiles your target with your flags plus `--generate-name-section`, disassembles the active function to `.wat`, pauses at your breakpoint, and highlights:
     - The active **Onyx line** on the left (and in the main editor gutter),
     - The corresponding **WAT instruction range** and active instruction (`>`) on the right,
     - The function's **parameters and WASM `(local ...)` slots** in the bottom strip.
5. **Step through your code**:
   - **`F10` / `Step`**: Step over to the next executable Onyx line (syncing the `.wat` highlight automatically).
   - **`F11` / `WAT`**: Step one individual WebAssembly `.wat` instruction at a time (updating the Onyx line when crossing line boundaries).
   - **`In` / `Out`**: Step into a local procedure call in the same file, or step back out to the caller.
   - **`F5` / `Cont`**: Continue to the next breakpoint in the file.
   - **`Shift+F5` / `Stop`**: End the debug session.

---

## Keyboard Shortcuts & Controls

| Shortcut / Action | Effect |
| :--- | :--- |
| `F9` / Click Editor Gutter | Toggle breakpoint on line |
| `F5` | Start debug session (or Continue when paused) |
| `F10` | Step Over (next Onyx source line) |
| `F11` | Step Instruction (next `.wat` instruction) |
| `Shift+F5` | Stop debug session |
| `Shift + MouseWheel` | Pan long Onyx or `.wat` lines horizontally inside the split view |
| `Ctrl+P` → `Debug: ...` | Run any debugger command from the Command Palette |

---

## Troubleshooting

- **"Function not found in compiled WASM"**: The procedure may have been inlined or dead-code eliminated by the Onyx compiler. Call it from `main()` (or another reachable entry point) so it is emitted into the `.wasm` module.
- **Empty `.wat` pane**: Ensure `wasm2wat` (from WABT) is installed and reachable on your `PATH`.
