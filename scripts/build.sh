#!/bin/sh
# Build the native module for Linux (desktop testing) and Android arm64 (the handheld), then pack
# both into dist/0100D71004694000.dsmod.zip.
#   scripts/build.sh [linux|android|package|all]   (default: all)
# Settings come from local.env (see local.env.example).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[ -f "$ROOT/local.env" ] && . "$ROOT/local.env"
TARGET="${1:-all}"
VERSION="${MC_VERSION:-$(python3 -c "import json;print(json.load(open('$ROOT/package/package.json'))['version'])")}"
# Every game build the module's Layout table supports (mc_reader.cpp).
BUILD_IDS="53E6D516A4DA5CD0C49FCE555994DA196B63E9C1000000000000000000000000 D8B7E605E809E80C76FA3BD670FAB5BA00000000000000000000000000000000"
LINUX_SO="$ROOT/build/linux/stripped/0100D71004694000.so"
ANDROID_SO="$ROOT/build/android/stripped/0100D71004694000.so"

build_linux() {
    cmake -S "$ROOT" -B "$ROOT/build/linux" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$ROOT/build/linux"
    mkdir -p "$ROOT/build/linux/stripped"
    strip --strip-all -o "$LINUX_SO" "$ROOT/build/linux/0100D71004694000.so"
    echo "linux module: $LINUX_SO"
}

build_android() {
    NDK="${ANDROID_NDK_ROOT:?set ANDROID_NDK_ROOT in local.env}"
    cmake -S "$ROOT" -B "$ROOT/build/android" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_static >/dev/null
    cmake --build "$ROOT/build/android"
    mkdir -p "$ROOT/build/android/stripped"
    "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" --strip-all \
        -o "$ANDROID_SO" "$ROOT/build/android/0100D71004694000.so"
    echo "android module: $ANDROID_SO"
}

build_package() {
    set -- --package "$ROOT/package" --output "$ROOT/dist" --version "$VERSION" --abi 1
    [ -f "$LINUX_SO" ] && set -- "$@" --module "linux-x86_64=$LINUX_SO"
    [ -f "$ANDROID_SO" ] && set -- "$@" --module "android-arm64-v8a=$ANDROID_SO"
    for id in $BUILD_IDS; do set -- "$@" --build-id "$id"; done
    python3 "$ROOT/scripts/build_dualscreen_package.py" "$@"
}

case "$TARGET" in
    linux) build_linux ;;
    android) build_android ;;
    package) build_package ;;
    all) build_linux; build_android; build_package ;;
    *) sed -n 2,6p "$0"; exit 1 ;;
esac
