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
            onyx build -r custom -DComponent_Build --wasm-mvp \
                -o "build/plugins/${name}.wasm" "${dir}main.onyx"
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
    test)
        build_native
        build_plugins
        build_host
        onyx run src/main.onyx -- --self-test
        ;;
    run)
        build_native
        build_plugins
        build_host
        shift || true
        onyx run src/main.onyx -- "${1:-src/main.onyx}"
        ;;
    clean)
        rm -rf build bin/odit_wasm_host.so
        ;;
    *)
        echo "Usage: $0 {all|native|plugins|host|sync-raylib|run [file]|test|clean}"
        exit 1
        ;;
esac
