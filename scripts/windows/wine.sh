#!/usr/bin/env bash
# Isolated Windows packaging environment; no game or launcher UI is executed.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export WINEPREFIX="$ROOT/work/windows/wine-prefix"
export XDG_RUNTIME_DIR="$ROOT/work/windows/run"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export RESORT_CROSS_WINE=1 PYOPENGL_PLATFORM=nt
export WINEARCH=win64 WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml='
export LD_LIBRARY_PATH="$ROOT/work/windows/wine/usr/lib/x86_64-linux-gnu/wine/x86_64-unix:$ROOT/work/windows/wine/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export WINEDLLPATH="$ROOT/work/windows/wine/usr/lib/x86_64-linux-gnu/wine/x86_64-windows:$ROOT/work/windows/wine/usr/lib/x86_64-linux-gnu/wine/x86_64-unix"
export WINESERVER="$ROOT/work/windows/wine/usr/lib/wine/wineserver64"
exec "$ROOT/work/windows/wine/usr/lib/wine/wine64" "$@"
