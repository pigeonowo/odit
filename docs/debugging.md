# Getting Started with Live Debugging in ODIT

ODIT includes a **Real Live Onyx Debugger** (`debugger.wasm`) that executes your actual compiled Onyx program via non-destructive shadow instrumentation (`.odit/dbg_shadow/`) and a live loopback TCP debug runtime.

Because your real program runs natively via `onyx run`:
- **All control flow executes faithfully**: `for` / `while` loops, `break`, `continue`, `if` / `elseif` / `else`, `switch`, recursion, `defer`, `use`, indirect dispatch (such as `aoc.run()` calling `@aoc.part1` / `@aoc.part2`), `--param` compiler flags, and file I/O.
- **Live runtime state**: Inspect real in-scope local variables & parameters (with change highlighting), real call stack frames across files, live `stdout` / `stderr` output, and on-demand WebAssembly Text (`.wat`) disassembly.

---

## Prerequisites

- **Onyx Toolchain** (`onyx` on `PATH` or `$ONYX_PATH/bin/onyx`)
- **WABT (`wasm2wat`)** *(optional — only needed for the on-demand `WAT` inspector tab)*

---

## Quick Start (5 Steps)

1. **Open an Onyx file** in the editor (e.g. via `Ctrl+O` Fuzzy File Finder or the File Explorer pane).
2. **Open the Live Debugger plugin**:
   - Press `Ctrl+P` to open the Command Palette.
   - Type `debugger` and select **`Plugin: Open debugger.wasm`**.
   - *(Tip: Drag the Live Debugger title bar to dock it to the Right or Bottom, or release in the center to float it.)*
3. **Set one or more breakpoints** (even across multiple files):
   - **Click the editor gutter** (left of the line numbers), **or**
   - Move the cursor to a line and press **`F9`**, **or**
   - Click the gutter of any line in the Live Debugger's source pane.
   - A red dot appears in both the editor gutter and the Live Debugger. You can also toggle breakpoints dynamically while paused or running.
4. **Configure the Run Command (optional) & Start debugging**:
   - Use the **`Cmd:`** input bar at the top of the Live Debugger to customize compiler flags, entry file, or program arguments—for example, in an Advent of Code project:
     ```text
     onyx run --param 'aoc.day=1' --param 'aoc.part=0' src/day1.onyx
     ```
     Program arguments after `--` are also forwarded to your binary (e.g. `onyx run src/main.onyx -- --verbose`).
   - Press **`Enter`** in the `Cmd:` box, press **`F5`**, or click **`Debug (F5)`** in the toolbar. (Unsaved edits in the active editor buffer are saved automatically before launch.)
   - If you have breakpoints set, execution runs at full speed until a breakpoint hits; otherwise it pauses on the first statement of `main()`.
5. **Step & inspect live state**:
   - **`F10` / `Over`**: Step Over to the next executed line in the current (or enclosing) call frame.
   - **`F11` / `Into`**: Step Into called Onyx procedures.
   - **`Shift+F11` / `Out`**: Step Out back to the caller frame.
   - **`F5` / `Cont` / `Pause`**: Continue execution to the next breakpoint (or pause a running program).
   - **`Shift+F5` / `Stop`**: Terminate the live debug session cleanly.

---

## Live Inspector Tabs

The right half of the Live Debugger panel provides five live tabs:

| Tab | What it shows |
| :--- | :--- |
| **`Locals(N)`** | All in-scope parameters and local variables formatted from live runtime memory. Values that changed since the previous pause are highlighted; hover for full `name: type = value` tooltip or click any row to copy the value to the clipboard. |
| **`Stack(N)`** | Live call stack from innermost frame (`#0`) to `main()`. Click any frame to jump both the debugger source view and main editor to that caller location. |
| **`BPs(N)`** | All breakpoints across the workspace. Click a breakpoint to jump to it, or click `x` to remove it. |
| **`Out`** | Live captured `stdout` and `stderr` from the debugged program (automatically selected if the program exits with a non-zero code). |
| **`WAT`** | On-demand WebAssembly Text (`.wat`) disassembly of the currently paused function. |

---

## Keyboard Shortcuts & Controls

| Shortcut / Action | Effect |
| :--- | :--- |
| `F9` / Click Editor Gutter | Toggle breakpoint on line |
| `F5` | Start debug session, Continue when paused, or Pause when running |
| `F10` | Step Over |
| `F11` | Step Into |
| `Shift+F11` | Step Out |
| `Shift+F5` | Stop debug session |
| `Ctrl+P` → `Debug: ...` | Run any debugger command from the Command Palette |

---

## Troubleshooting

- **Compile error on launch**: Check the bottom error banner and the **`Out`** tab for the full Onyx compiler diagnostic.
- **No breakpoints hit**: Make sure the entry file in **`Cmd:`** reaches the procedure containing your breakpoint (for example, pass `--param 'aoc.day=1' --param 'aoc.part=0'` for AoC projects).
- **Empty `WAT` tab**: Ensure `wasm2wat` (from WABT) is installed and reachable on your `PATH`.
