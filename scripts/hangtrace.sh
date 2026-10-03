#!/usr/bin/env bash
# Host-side stack traces of a running (hung) Resortcompiled, so we can see which translated guest function the
# RUNNING guest thread is spinning in (translated functions show up as func_XXXXXXXX = guest address).
#
#   ./out/Resortcompiled &          # start the game, wait until it's stuck
#   bash scripts/hangtrace.sh       # takes 3 samples 2 s apart -> out/UserData/Logs/gdb_bt.txt
#
# Needs gdb (sudo apt install gdb). Attaching to a running process needs root on Ubuntu/Mint
# (kernel.yama.ptrace_scope=1), so this uses sudo.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
command -v gdb >/dev/null || { echo "gdb not installed: sudo apt install gdb" >&2; exit 1; }
PID="$(pgrep -f "$ROOT/out/Resortcompiled|out/Resortcompiled" | head -1 || true)"
[[ -n "$PID" ]] || { echo "Resortcompiled is not running" >&2; exit 1; }
OUT="$ROOT/out/UserData/Logs/gdb_bt.txt"
mkdir -p "$(dirname "$OUT")"
: > "$OUT"
for i in 1 2 3; do
  echo "===== sample $i ($(date +%T)) =====" >> "$OUT"
  sudo gdb -p "$PID" -batch -nx \
    -ex "set pagination off" -ex "set print frame-arguments none" \
    -ex "thread apply all bt 40" >> "$OUT" 2>&1 || true
  sleep 2
done
echo "wrote $OUT ($(grep -c 'func_' "$OUT") translated frames)"
