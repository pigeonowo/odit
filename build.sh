#!/usr/bin/env bash
set -euo pipefail

export ONYX_PATH="${ONYX_PATH:-$HOME/.onyx}"
export PATH="$ONYX_PATH/bin:$PATH"

CMD="${1:-all}"

build_native() {
    mkdir -p bin
    echo "Building native Wasmer bridge -> bin/odit_wasm_host.so"
    gcc -O2 -fPIC -shared -Wall -Wno-missing-braces -Wno-unused-parameter \
        -I"$ONYX_PATH/include" native/wasm_host.c -o bin/odit_wasm_host.so -ldl
    if [[ -f "$ONYX_PATH/lib/odit_wasm_host.so" ]]; then
        cp -f bin/odit_wasm_host.so "$ONYX_PATH/lib/odit_wasm_host.so"
    fi
}

sync_raylib() {
    local bf="lib/github.com/onyx-lang/pkg-raylib/build.onyx"
    if [[ -f "$bf" ]]; then
        sed -i 's/string\.concat/str.concat/g' "$bf"
    fi
    onyx pkg sync
}

build_plugins() {
    mkdir -p build/plugins
    for dir in plugins/*/; do
        if [[ -f "${dir}main.onyx" ]]; then
            local name
            name="$(basename "$dir")"
            echo "Building WASM plugin ${name} -> build/plugins/${name}.wasm"
            onyx build -r js --wasm-mvp \
                -o "build/plugins/${name}.wasm" "${dir}main.onyx"
            rm -f "build/plugins/${name}.wasm.js"
        fi
    done
}

build_host() {
    mkdir -p build
    if [[ ! -f "bin/onyx_raylib.so" ]]; then
        sync_raylib
    fi
    echo "Building odit host -> build/odit.wasm"
    onyx build -o build/odit.wasm src/main.onyx
}

case "$CMD" in
    native)      build_native ;;
    plugins)     build_plugins ;;
    host)        build_host ;;
    sync-raylib) sync_raylib ;;
    all)
        build_native
        build_plugins
        build_host
        echo "Build complete!"
        ;;
    unit)
        onyx run tests/odit_test.onyx -- --verbose
        ;;
    test)
        build_native
        build_plugins
        build_host
        onyx run tests/odit_test.onyx -- --verbose
        onyx run src/main.onyx -- --self-test
        ;;
    run)
        build_native
        build_plugins
        build_host
        shift || true
        onyx run src/main.onyx -- "${1:-src/main.onyx}"
        ;;
    install)
        build_native
        build_plugins
        build_host
        ODIT_HOME="${ODIT_HOME:-$HOME/.local/share/odit}"
        BIN_DIR="$HOME/.local/bin"
        mkdir -p "$ONYX_PATH/lib" "$ODIT_HOME/plugins" "$BIN_DIR"
        cp -f bin/odit_wasm_host.so bin/onyx_raylib.so "$ONYX_PATH/lib/"
        cp -f build/odit.wasm "$ODIT_HOME/odit.wasm"
        cp -f build/plugins/*.wasm "$ODIT_HOME/plugins/"
        cat > "$BIN_DIR/odit" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
export ONYX_PATH="${ONYX_PATH:-$HOME/.onyx}"
export ODIT_HOME="${ODIT_HOME:-$HOME/.local/share/odit}"
exec "$ONYX_PATH/bin/onyx" run "$ODIT_HOME/odit.wasm" -- "$@"
EOF
        chmod +x "$BIN_DIR/odit"
        echo "Installed! Run 'odit' from any project directory (ensure $BIN_DIR is on PATH)."
        ;;
    clean)
        rm -rf build bin/odit_wasm_host.so
        ;;
    *)
        echo "Usage: $0 {all|native|plugins|host|sync-raylib|run [file]|test|install|clean}"
        exit 1
        ;;
esac
