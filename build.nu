#!/usr/bin/env nu
# =============================================================================
# Onyx Editor ("odit") — Nushell Build & Run Script
# =============================================================================
# Usage:
#   nu build.nu              # Build native bridge, all WASM plugins, and host
#   nu build.nu run [file]   # Build everything and launch the IDE
#   nu build.nu test         # Build everything and run headless ABI self-test
#   nu build.nu native       # Build only bin/odit_wasm_host.so
#   nu build.nu plugins      # Build all plugins/*/main.onyx -> build/plugins/*.wasm
#   nu build.nu host         # Build only build/odit.wasm
#   nu build.nu sync-raylib  # Patch (Onyx 2611) and sync pkg-raylib native lib
#   nu build.nu install      # Install `odit` globally to ~/.local/bin/odit
#   nu build.nu clean        # Remove build artifacts

def onyx-path []: nothing -> string {
    $env.ONYX_PATH? | default ($env.HOME | path join ".onyx")
}

def onyx-bin []: nothing -> string {
    (onyx-path) | path join "bin" "onyx"
}

# Build the native C Wasmer bridge (`bin/odit_wasm_host.so`)
export def "main native" [] {
    mkdir bin
    let inc = ((onyx-path) | path join "include")
    print "Building native Wasmer bridge -> bin/odit_wasm_host.so"
    ^gcc -O2 -fPIC -shared -Wall -Wno-missing-braces -Wno-unused-parameter $"-I($inc)" native/wasm_host.c -o bin/odit_wasm_host.so -ldl
    let installed_so = ((onyx-path) | path join "lib" "odit_wasm_host.so")
    if ($installed_so | path exists) {
        cp -f bin/odit_wasm_host.so $installed_so
    }
}

# Compile all WASM plugins in `plugins/*/main.onyx` to `build/plugins/*.wasm`
export def "main plugins" [] {
    mkdir build/plugins
    let onyx = (onyx-bin)
    let plugin_dirs = (ls plugins | where type == dir)

    for dir in $plugin_dirs {
        let entry = ($dir.name | path join "main.onyx")
        if ($entry | path exists) {
            let name = ($dir.name | path basename)
            let out = ($"build/plugins/($name).wasm")
            print $"Building WASM plugin ($name) -> ($out)"
            with-env { ONYX_PATH: (onyx-path) } {
                ^$onyx build -r custom -DComponent_Build --wasm-mvp -o $out $entry
            }
        }
    }
}

# Patch pkg-raylib build.onyx for Onyx 2611 (`string.concat` -> `str.concat`) and sync
export def "main sync-raylib" [] {
    let build_file = "lib/github.com/onyx-lang/pkg-raylib/build.onyx"
    if ($build_file | path exists) {
        let patched = (open --raw $build_file | str replace --all "string.concat" "str.concat")
        $patched | save -f $build_file
    }
    let onyx = (onyx-bin)
    print "Syncing Onyx packages (pkg-raylib)..."
    with-env { ONYX_PATH: (onyx-path) } {
        ^$onyx pkg sync
    }
}

# Compile the native host (`src/main.onyx` -> `build/odit.wasm`)
export def "main host" [] {
    mkdir build
    if not ("bin/onyx_raylib.so" | path exists) {
        main sync-raylib
    }
    let onyx = (onyx-bin)
    print "Building odit host -> build/odit.wasm"
    with-env { ONYX_PATH: (onyx-path) } {
        ^$onyx build -o build/odit.wasm src/main.onyx
    }
}

# Build everything (native bridge + plugins + host)
export def main [] {
    main native
    main plugins
    main host
    print "Build complete!"
}

# Build everything and launch Onyx Editor
export def "main run" [file: string = "src/main.onyx"] {
    main
    let onyx = (onyx-bin)
    print $"Launching Onyx Editor with ($file)..."
    with-env { ONYX_PATH: (onyx-path) } {
        ^$onyx run src/main.onyx -- $file
    }
}

# Build everything and run the headless WASM plugin ABI self-test
export def "main test" [] {
    main
    let onyx = (onyx-bin)
    print "Running headless WASM plugin ABI self-test..."
    with-env { ONYX_PATH: (onyx-path) } {
        ^$onyx run src/main.onyx -- --self-test
    }
}

# Install odit globally (`~/.local/share/odit`, `~/.onyx/lib/*.so`, `~/.local/bin/odit`)
export def "main install" [] {
    main
    let home      = $env.HOME
    let onyx_lib  = ((onyx-path) | path join "lib")
    let odit_home = ($home | path join ".local" "share" "odit")
    let odit_plug = ($odit_home | path join "plugins")
    let bin_dir   = ($home | path join ".local" "bin")
    let launcher  = ($bin_dir | path join "odit")

    mkdir $onyx_lib $odit_home $odit_plug $bin_dir

    print $"Installing native libraries -> ($onyx_lib)"
    cp -f bin/odit_wasm_host.so $onyx_lib
    cp -f bin/onyx_raylib.so $onyx_lib

    print $"Installing odit.wasm & plugins -> ($odit_home)"
    cp -f build/odit.wasm ($odit_home | path join "odit.wasm")
    for p in (ls build/plugins/*.wasm) {
        cp -f $p.name $odit_plug
    }

    let script = "#!/usr/bin/env bash
set -euo pipefail
export ONYX_PATH=\"${ONYX_PATH:-$HOME/.onyx}\"
export ODIT_HOME=\"${ODIT_HOME:-$HOME/.local/share/odit}\"
exec \"$ONYX_PATH/bin/onyx\" run \"$ODIT_HOME/odit.wasm\" -- \"$@\"
"
    $script | save -f $launcher
    ^chmod +x $launcher

    print $"Installed! Run `odit` from any project directory."
    print $"Make sure ($bin_dir) is on your PATH."
}

# Clean generated build outputs
export def "main clean" [] {
    rm -rf build bin/odit_wasm_host.so
    print "Cleaned build outputs."
}
