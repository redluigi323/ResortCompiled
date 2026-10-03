#!/usr/bin/env bash
# Extract a Wii Sports Resort disc image into work/extracted/ and check its game ID.
#   ./scripts/extract.sh disc/<image> [--verify]
# --verify also hashes the image against dtk's built-in Redump database (slow, but confirms a clean dump).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DTK="$ROOT/tools/bin/dtk"
[[ -x "$DTK" ]] || { echo "dtk missing - run ./setup.sh first" >&2; exit 1; }
[[ $# -ge 1 ]] || { echo "usage: $0 disc/<image> [--verify]" >&2; exit 2; }

IMAGE="$(realpath "$1")"
OUT="$ROOT/work/extracted"
EXPECTED_ID="${EXPECTED_ID:-RZTP01}"

echo "==> Disc info"
"$DTK" disc info "$IMAGE"

if [[ "${2:-}" == "--verify" ]]; then
  echo "==> Verifying against Redump"
  "$DTK" disc verify "$IMAGE"
fi

if [[ -d "$OUT" ]]; then
  echo "==> Removing previous extraction"
  rm -rf "$OUT"
fi
echo "==> Extracting data partition to work/extracted"
"$DTK" disc extract "$IMAGE" "$OUT"

BOOT="$OUT/sys/boot.bin"
[[ -f "$BOOT" ]] || { echo "extraction layout unexpected: $BOOT not found" >&2; exit 1; }
ID="$(head -c 6 "$BOOT")"
echo "==> Game ID: $ID"
if [[ "$ID" != "$EXPECTED_ID" ]]; then
  echo "[warn] expected $EXPECTED_ID (PAL Wii Sports Resort). The project is pinned to that revision;" >&2
  echo "       set EXPECTED_ID=$ID to silence this if you're deliberately using another one." >&2
fi
sha256sum "$OUT/sys/main.dol" | tee "$ROOT/work/main.dol.sha256"
echo "==> REL modules on disc:"
find "$OUT/files" -iname '*.rel' -printf '   %P\n' | sort || true
echo "==> Archives that may contain RELs (check with: tools/bin/dtk vfs ls -r <arc>:)"
find "$OUT/files" -iname '*rel*.arc' -printf '   %P\n' | sort || true
echo "Done. Next: ./scripts/analyze.sh"
