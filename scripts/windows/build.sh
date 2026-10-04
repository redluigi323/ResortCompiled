#!/usr/bin/env bash
# Cross compile the native runtime and EOS bridge without executing the game.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WINDOWS="$ROOT/work/windows"
LLVM_MINGW_ROOT="${LLVM_MINGW_ROOT:-$WINDOWS/llvm-mingw-20260922-ucrt-ubuntu-22.04-x86_64}"
JOBS="${JOBS:-1}"
TREE="$WINDOWS/tree"; BUILD="$WINDOWS/build"; OUT="$ROOT/out/windows"
mkdir -p "$TREE/runtime" "$OUT"
rsync -a --delete --exclude third_party --exclude assets "$ROOT/runtime/wsr/" "$TREE/runtime/"
rsync -a "$ROOT/external/wiicompiled/runtime/third_party" "$ROOT/external/wiicompiled/runtime/assets" "$TREE/runtime/"
ln -sfn "$ROOT/work/generated" "$TREE/generated"
ln -sfn "$ROOT/external/wiicompiled/aurora-main" "$TREE/aurora-main"
# Reuse pinned source downloads from the Linux build, never its compiled libraries.
DEPENDENCIES=()
for name in abseil-cpp fmt freetype imgui png sqlite3 tracy xxhash zstd; do
    path="$ROOT/work/build/_deps/$name-src"
    [[ ! -d "$path" ]] || DEPENDENCIES+=("-DFETCHCONTENT_SOURCE_DIR_${name^^}=$path")
done
DEPENDENCIES+=("-DFETCHCONTENT_SOURCE_DIR_ZLIB=$WINDOWS/zlib-1.3.2")
DEPENDENCIES+=("-DFETCHCONTENT_SOURCE_DIR_SDL=$ROOT/work/build/_deps/sdl-src")
python3 "$ROOT/scripts/netplay_build_identity.py" --output "$ROOT/work/netplay-simulation-id.txt"
SIMULATION_ID="$(cat "$ROOT/work/netplay-simulation-id.txt")"
cmake -S "$TREE/runtime" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/scripts/windows/llvm-mingw.cmake" \
    -DLLVM_MINGW_ROOT="$LLVM_MINGW_ROOT" -DCMAKE_BUILD_TYPE=Release \
    -DRIISORTED_SIMULATION_ID="$SIMULATION_ID" -DMKW_TRANSLATED_COMPILE_JOBS=1 \
    -DMKW_TRANSLATED_SHARD_MANIFEST="$TREE/generated/build_shards/shards.cmake" \
    -DMKW_CPPWINRT_INCLUDE_DIR="$WINDOWS/cppwinrt" \
    -DAURORA_DAWN_PROVIDER=system -DDawn_DIR="$WINDOWS/dawn/lib/cmake/Dawn" \
    -DAURORA_DAWN_LINKAGE=shared -DAURORA_SDL3_PROVIDER=vendor \
    -DAURORA_SDL3_LIBUSB=OFF "${DEPENDENCIES[@]}"
cmake --build "$BUILD" --target WiiCompiled --parallel "$JOBS"
cp "$BUILD/WiiCompiled.exe" "$OUT/Resortcompiled.exe"
find "$BUILD" -maxdepth 1 -name '*.dll' -exec cp -t "$OUT" {} +
cp "$WINDOWS/dawn/bin/"*.dll "$OUT/"
cp "$WINDOWS/vcredist/"*.dll "$OUT/"
cp "$LLVM_MINGW_ROOT/LICENSE.TXT" "$OUT/LLVM-MinGW-LICENSE.TXT"
for name in dsp_coef.bin cacert.pem; do
    [[ ! -f "$BUILD/$name" ]] || cp "$BUILD/$name" "$OUT/"
done
for name in font_western.bin font_japanese.bin; do cp "$ROOT/tools/fonts/$name" "$OUT/"; done
cp -a "$BUILD/wii_bootstrap" "$OUT/"
touch "$OUT/portable.txt"
python3 "$ROOT/scripts/build_eos.py" --platform win32 \
    --compiler "$LLVM_MINGW_ROOT/bin/x86_64-w64-mingw32-clang++" --output "$WINDOWS/eos"
echo "Windows runtime: $OUT"
