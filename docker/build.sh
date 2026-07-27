#!/bin/bash
set -euo pipefail

OS=${1:-all}
shopt -s nocasematch

build_for_linux() {
    echo
    echo "Building vlc-subtitle for Linux 64-bit..."
    cd /plugin
    if [[ ! -f runtime/whisper.cpp/CMakeLists.txt ]]; then
        echo "ERROR: runtime/whisper.cpp is missing; initialize Git submodules first"
        exit 1
    fi
    make clean
    make CC=cc CXX=c++
    mkdir -p build/linux/64
    cp libsuboffline_plugin.so libparakeet.so libmoonshine.so \
       libonnxruntime.so.1 build/linux/64/
}

build_for_windows() {
    echo
    echo "Building vlc-subtitle for Windows 64-bit..."
    local sdk
    sdk=$(find /opt/vlc-win64 -path '*/sdk/lib/pkgconfig' -type d | head -n 1)
    if [[ -z "$sdk" ]]; then
        echo "ERROR: VLC Windows SDK pkg-config directory was not found"
        exit 1
    fi

    cd "${sdk%/lib/pkgconfig}"
    sed -i "s|^prefix=.*|prefix=${PWD}|g" lib/pkgconfig/*.pc
    export PKG_CONFIG_PATH="${PWD}/lib/pkgconfig"

    cd /plugin
    make clean
    make CC=x86_64-w64-mingw32-gcc \
         CXX=x86_64-w64-mingw32-g++ \
         OS=Windows_NT
    mkdir -p build/win/64
    cp libsuboffline_plugin.dll libparakeet.dll libmoonshine.dll \
       onnxruntime.dll build/win/64/
}

if [[ ! "$OS" =~ ^(linux|windows|all)$ ]]; then
    echo "ERROR: unsupported OS '$OS'. Use linux, windows, or all."
    exit 1
fi

case "$OS" in
linux)
    build_for_linux
    ;;
windows)
    build_for_windows
    ;;
all)
    build_for_linux
    build_for_windows
    ;;
esac
