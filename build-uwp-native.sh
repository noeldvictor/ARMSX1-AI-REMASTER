#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./build-uwp-native.sh [x64|x86] [Debug|Release]

Builds the UWP/WinRT native DLL with MinGW, then stages:
  - SDL2.dll into uwp/deps/bin
  - libarmsx.dll, SDL2.dll, and icons/ into uwp/ARMSX/bin/<platform>/<config>

Environment overrides:
  BUILD_JOBS   Parallel build jobs (default: 4)
  SDL2_DIR     Override the SDL2 package path relative to repo root
  MINGW_MAKE   Override the make command (default: mingw32-make, fallback: make)
EOF
}

platform="${1:-x64}"
configuration="${2:-Debug}"
build_jobs="${BUILD_JOBS:-4}"

case "$platform" in
    x64)
        default_sdl2_dir="SDL2-2.30.3/x86_64-w64-mingw32"
        ;;
    x86)
        default_sdl2_dir="SDL2-2.30.3/i686-w64-mingw32"
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        echo "Unsupported platform '$platform'. Use x64 or x86." >&2
        exit 1
        ;;
esac

case "$configuration" in
    Debug|Release)
        ;;
    *)
        echo "Unsupported configuration '$configuration'. Use Debug or Release." >&2
        exit 1
        ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
sdl2_dir="${SDL2_DIR:-$default_sdl2_dir}"
sdl2_root="$repo_root/$sdl2_dir"
sdl2_include="$sdl2_root/include/SDL2"
sdl2_lib="$sdl2_root/lib"
sdl2_dll="$sdl2_root/bin/SDL2.dll"
fsui_build_dir="$repo_root/build/fsui/uwp-$platform"
native_stage_dir="$repo_root/uwp/deps/bin"
uwp_output_dir="$repo_root/uwp/ARMSX/bin/$platform/$configuration"
uwp_icons_dir="$uwp_output_dir/icons"

make_cmd="${MINGW_MAKE:-mingw32-make}"
if ! command -v "$make_cmd" >/dev/null 2>&1; then
    make_cmd="make"
fi
if ! command -v "$make_cmd" >/dev/null 2>&1; then
    echo "Unable to find mingw32-make or make in PATH." >&2
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake is required." >&2
    exit 1
fi

if [ ! -d "$sdl2_root" ]; then
    echo "SDL2 dependency directory not found at $sdl2_root" >&2
    exit 1
fi

mkdir -p "$repo_root/bin" "$native_stage_dir" "$uwp_output_dir" "$uwp_icons_dir"

cmake -S "$repo_root/third_party/fsui-lib" -B "$fsui_build_dir" -G "MinGW Makefiles" \
    -DFSUI_BUILD_SAMPLES=OFF \
    -DFSUI_PLATFORM_BACKEND=SDL2 \
    -DFSUI_USE_SYSTEM_SDL2=ON \
    -DCMAKE_PREFIX_PATH="$sdl2_root"
cmake --build "$fsui_build_dir" -j"$build_jobs"

"$make_cmd" clean
"$make_cmd" shared \
    WINDOWS_TARGET=1 \
    UWP_TARGET=1 \
    SDL_STATIC=0 \
    FSUI_BUILD_DIR="$fsui_build_dir" \
    SDL_CFLAGS="-I$sdl2_include" \
    SDL_LIBS_DYNAMIC="-L$sdl2_lib -lSDL2"

cp "$sdl2_dll" "$native_stage_dir/"
cp "$repo_root/bin/libarmsx.dll" "$uwp_output_dir/"
cp "$sdl2_dll" "$uwp_output_dir/"
cp -R "$repo_root/icons/." "$uwp_icons_dir/"

echo "Staged UWP native runtime to $uwp_output_dir"
