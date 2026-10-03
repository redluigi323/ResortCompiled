#!/usr/bin/env bash
# Extract a Mario Kart Wii PAL (RMCP01) image as the *reference* binary for analyze.sh's cross-matching.
# Only main.dol is kept (in work/ref/mkw/sys/); it lets analyze.py carry WiiCompiled's ~20k MKW function
# names (RVL SDK, NW4R, EGG) over to Wii Sports Resort by matching instruction patterns.
#   ./scripts/extract_ref.sh disc/<mkw image>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DTK="$ROOT/tools/bin/dtk"
[[ -x "$DTK" ]] || { echo "dtk missing - run ./setup.sh first" >&2; exit 1; }
[[ $# -ge 1 ]] || { echo "usage: $0 disc/<mkw image>" >&2; exit 2; }

IMAGE="$(realpath "$1")"
OUT="$ROOT/work/ref/mkw/sys"
WANT_SHA="80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05"   # WiiCompiled's pinned PAL main.dol

mkdir -p "$OUT"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
echo "==> Extracting main.dol + boot.bin from $(basename "$IMAGE")"
"$DTK" vfs cp "$IMAGE:sys/main.dol" "$IMAGE:sys/boot.bin" "$TMP/" 2>/dev/null \
  || "$DTK" disc extract "$IMAGE" "$TMP/x"
DOL="$(find "$TMP" -name main.dol | head -1)"; BOOT="$(find "$TMP" -name boot.bin | head -1)"
[[ -f "$DOL" ]] || { echo "could not extract main.dol" >&2; exit 1; }

ID="$(head -c 6 "$BOOT" 2>/dev/null || echo '??????')"
SHA="$(sha256sum "$DOL" | cut -d' ' -f1)"
echo "==> Game ID: $ID   main.dol sha256: $SHA"
[[ "$ID" == "RMCP01" ]] || echo "[warn] expected RMCP01 (PAL). WiiCompiled's MAP.txt only lines up with the PAL executable." >&2
[[ "$SHA" == "$WANT_SHA" ]] || echo "[warn] main.dol differs from WiiCompiled's pinned PAL build; matching will be poor." >&2

cp "$DOL" "$OUT/main.dol"
echo "Done. Re-run ./scripts/analyze.sh (it picks up work/ref/mkw/sys/main.dol automatically)."
