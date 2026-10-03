#!/usr/bin/env bash
# Puts the Mii (RFL / FaceLib) files a real Wii keeps in NAND into Resortcompiled's managed NAND
# (out/UserData/NAND/shared2/menu/FaceLib/). Wii Sports Resort reads both at boot:
#
#   RFL_Res.dat  - Mii face/hair/body parts. Installed by the System Menu, not on the WSR disc.
#                  Mario Kart Wii ships a copy on its disc (files/contents/RFLRes01.arc), so we take it from
#                  your MKW image in disc/ (the same one extract_ref.sh uses).
#   RFL_DB.dat   - your Mii database (the Miis themselves). Taken, in order, from:
#                    1. the path you pass:  bash scripts/seed_nand.sh --db /path/to/RFL_DB.dat
#                    2. Dolphin's Wii NAND (native or Flatpak install)
#                  e.g. a NAND dump from your own Wii, or a database you built with Mii tools.
#
#   bash scripts/seed_nand.sh [--db FILE] [--mkw disc/RMCP01.iso] [--force]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DTK="$ROOT/tools/bin/dtk"
FACELIB="$ROOT/out/UserData/NAND/shared2/menu/FaceLib"

DB_SRC=""
MKW=""
FORCE=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --db) DB_SRC="$2"; shift 2 ;;
    --mkw) MKW="$2"; shift 2 ;;
    --force) FORCE=1; shift ;;
    -h|--help) sed -n 2,14p "$0"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*" >&2; }
mkdir -p "$FACELIB"

# ------------------------------------------------------------------ RFL_Res.dat
if [[ -s "$FACELIB/RFL_Res.dat" && $FORCE -eq 0 ]]; then
  log "RFL_Res.dat already present ($(stat -c %s "$FACELIB/RFL_Res.dat") bytes)"
else
  if [[ -z "$MKW" ]]; then
    MKW="$(find "$ROOT/disc" -maxdepth 1 -type f \( -iname '*RMC*' -o -iname '*mario*kart*' \) | head -1 || true)"
  fi
  [[ -x "$DTK" ]] || { echo "dtk missing - run setup.sh first" >&2; exit 1; }
  if [[ -z "$MKW" || ! -f "$MKW" ]]; then
    warn "no Mario Kart Wii image found in disc/ (pass --mkw <image>); RFL_Res.dat not installed"
  else
    TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
    ARC_PATH="files/contents/RFLRes01.arc"
    if ! "$DTK" vfs cp "$MKW:$ARC_PATH" "$TMP/RFLRes01.arc" >/dev/null 2>&1; then
      # Fall back to searching the disc listing for the archive.
      ARC_PATH="$("$DTK" vfs ls -r "$MKW:files" 2>/dev/null | grep -io '[^ ]*RFLRes01\.arc' | head -1 || true)"
      [[ -n "$ARC_PATH" ]] && "$DTK" vfs cp "$MKW:files/${ARC_PATH#files/}" "$TMP/RFLRes01.arc" >/dev/null 2>&1 || true
    fi
    if [[ ! -s "$TMP/RFLRes01.arc" ]]; then
      warn "could not find RFLRes01.arc on $(basename "$MKW")"
    else
      python3 - "$TMP/RFLRes01.arc" "$FACELIB/RFL_Res.dat" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
if data[:4] == b"Yaz0":                         # decompress if needed
    size = struct.unpack(">I", data[4:8])[0]; src = 16; out = bytearray()
    while len(out) < size:
        code = data[src]; src += 1
        for bit in range(8):
            if len(out) >= size: break
            if code & (0x80 >> bit):
                out.append(data[src]); src += 1
            else:
                b1, b2 = data[src], data[src + 1]; src += 2
                dist = ((b1 & 0xF) << 8 | b2) + 1
                n = b1 >> 4
                if n == 0: n = data[src] + 0x12; src += 1
                else: n += 2
                for _ in range(n): out.append(out[-dist])
    data = bytes(out)
assert data[:4] == b"\x55\xAA\x38\x2D", "not a U8 archive"
root_off = struct.unpack(">I", data[4:8])[0]
count = struct.unpack(">I", data[root_off + 8:root_off + 12])[0]
strtab = root_off + count * 12
for i in range(count):
    t_name, off, size = struct.unpack(">III", data[root_off + i * 12:root_off + i * 12 + 12])
    name_off = t_name & 0xFFFFFF
    name = data[strtab + name_off:data.index(b"\0", strtab + name_off)].decode()
    if (t_name >> 24) == 0 and name == "RFL_Res.dat":
        open(sys.argv[2], "wb").write(data[off:off + size])
        print(f"   extracted RFL_Res.dat ({size} bytes)")
        break
else:
    sys.exit("RFL_Res.dat not inside RFLRes01.arc")
PY
      log "installed RFL_Res.dat from $(basename "$MKW")"
    fi
  fi
fi

# ------------------------------------------------------------------ RFL_DB.dat
if [[ -s "$FACELIB/RFL_DB.dat" && $FORCE -eq 0 && -z "$DB_SRC" ]]; then
  log "RFL_DB.dat already present ($(stat -c %s "$FACELIB/RFL_DB.dat") bytes)"
else
  if [[ -z "$DB_SRC" ]]; then
    for c in "$HOME/.local/share/dolphin-emu/Wii/shared2/menu/FaceLib/RFL_DB.dat" \
             "$HOME/.dolphin-emu/Wii/shared2/menu/FaceLib/RFL_DB.dat" \
             "$HOME/.var/app/org.DolphinEmulator.dolphin-emu/data/dolphin-emu/Wii/shared2/menu/FaceLib/RFL_DB.dat"; do
      [[ -s "$c" ]] && { DB_SRC="$c"; break; }
    done
  fi
  if [[ -n "$DB_SRC" && -s "$DB_SRC" ]]; then
    cp -f "$DB_SRC" "$FACELIB/RFL_DB.dat"
    log "installed RFL_DB.dat from $DB_SRC ($(stat -c %s "$FACELIB/RFL_DB.dat") bytes)"
    [[ "$(head -c 4 "$FACELIB/RFL_DB.dat")" == "RNOD" ]] || warn "RFL_DB.dat does not start with 'RNOD' - is it really a Mii database?"
  else
    warn "no RFL_DB.dat found. Pass one with --db (NAND dump of your Wii: shared2/menu/FaceLib/RFL_DB.dat,"
    warn "or Dolphin after opening the Mii Channel once). Without it the game boots with no Miis."
  fi
fi
ls -l "$FACELIB"
