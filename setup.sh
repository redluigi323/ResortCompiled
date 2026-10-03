#!/usr/bin/env bash
# Resortcompiled - one-time setup for Linux (tested target: Linux Mint / Ubuntu, x86_64 or aarch64).
#
# What it does:
#   1. Installs build dependencies via apt (skip with --no-apt)
#   2. Ensures the .NET 8 SDK is available (apt, falling back to Microsoft's dotnet-install.sh)
#   3. Clones WiiCompiled into external/wiicompiled at a pinned commit
#   4. Downloads decomp-toolkit (dtk) into tools/bin/
#   5. Builds the WiiCompiled translator
#
# Safe to re-run.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

WIICOMPILED_URL="${WIICOMPILED_URL:-https://github.com/patchzyy/wiicompiled}"
# Commit this setup was written against (2026-09-29). Override with WIICOMPILED_REF=main to track upstream.
WIICOMPILED_REF="${WIICOMPILED_REF:-a88b7b502b620d38384e39aa7813c4ebae9a1f0c}"

DO_APT=1
for arg in "$@"; do
  case "$arg" in
    --no-apt) DO_APT=0 ;;
    -h|--help)
      sed -n 2,13p "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

log()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[warn]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[error]\033[0m %s\n' "$*" >&2; exit 1; }

case "$(uname -m)" in
  x86_64)        DTK_ARCH=linux-x86_64 ;;
  aarch64|arm64) DTK_ARCH=linux-aarch64 ;;
  *) die "unsupported CPU architecture: $(uname -m)" ;;
esac

# ---------------------------------------------------------------- 1. apt deps
if [[ $DO_APT -eq 1 ]]; then
  command -v apt-get >/dev/null || die "apt-get not found; install deps manually and re-run with --no-apt"
  log "Installing build dependencies (sudo will ask for your password)"
  sudo apt-get update
  # Toolchain + scripting
  sudo apt-get install -y build-essential git curl rsync ca-certificates pkg-config cmake ninja-build \
    clang lld llvm python3 python3-yaml python3-venv python3-pip openssl
  # Same set WiiCompiled's Linux CI uses for SDL3 / Dawn (aurora)
  sudo apt-get install -y libasound2-dev libpulse-dev libaudio-dev libfribidi-dev libjack-dev \
    libsndio-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
    libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev \
    libgles2-mesa-dev libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev libthai-dev \
    libusb-1.0-0-dev libpipewire-0.3-dev libwayland-dev libdecor-0-dev liburing-dev \
    libvulkan-dev \
    || warn "some optional dev packages failed to install; the translator still works, the native build may not"
fi

# ---------------------------------------------------------------- 2. .NET 8
export DOTNET_CLI_TELEMETRY_OPTOUT=1
has_dotnet8() { command -v dotnet >/dev/null && dotnet --list-sdks 2>/dev/null | grep -q '^8\.'; }
if [[ -x "$HOME/.dotnet/dotnet" ]]; then export PATH="$HOME/.dotnet:$PATH"; fi
if ! has_dotnet8; then
  log "Installing .NET 8 SDK"
  if [[ $DO_APT -eq 1 ]] && sudo apt-get install -y dotnet-sdk-8.0 && has_dotnet8; then
    :
  else
    warn "dotnet-sdk-8.0 not available from apt; using dotnet-install.sh into ~/.dotnet"
    curl -fsSL https://dot.net/v1/dotnet-install.sh -o /tmp/dotnet-install.sh
    bash /tmp/dotnet-install.sh --channel 8.0 --install-dir "$HOME/.dotnet"
    export PATH="$HOME/.dotnet:$PATH"
    has_dotnet8 || die ".NET 8 install failed"
    grep -q '.dotnet' "$HOME/.bashrc" 2>/dev/null || \
      echo 'export PATH="$HOME/.dotnet:$PATH"' >> "$HOME/.bashrc"
  fi
fi
log "dotnet: $(dotnet --version)"

# ---------------------------------------------------------------- 3. WiiCompiled
mkdir -p external
if [[ ! -d external/wiicompiled/.git ]]; then
  log "Cloning WiiCompiled"
  git clone "$WIICOMPILED_URL" external/wiicompiled
fi
log "Checking out WiiCompiled @ $WIICOMPILED_REF"
git -C external/wiicompiled fetch --quiet origin || warn "fetch failed; using local checkout"
git -C external/wiicompiled checkout --quiet "$WIICOMPILED_REF"

log "Applying Resortcompiled patches to WiiCompiled"
for p in "$ROOT"/patches/wiicompiled/*.patch; do
  [[ -f "$p" ]] || continue
  if git -C external/wiicompiled apply --reverse --check "$p" 2>/dev/null; then
    echo "   already applied: $(basename "$p")"
  else
    git -C external/wiicompiled apply "$p" && echo "   applied: $(basename "$p")"
  fi
done

# ---------------------------------------------------------------- 4. decomp-toolkit
mkdir -p tools/bin
if [[ ! -x tools/bin/dtk ]]; then
  log "Downloading decomp-toolkit ($DTK_ARCH)"
  curl -fL "https://github.com/encounter/decomp-toolkit/releases/latest/download/dtk-$DTK_ARCH" \
    -o tools/bin/dtk
  chmod +x tools/bin/dtk
fi
log "dtk: $(tools/bin/dtk --version 2>/dev/null || echo '?')"

# ---------------------------------------------------------------- 4b. IPL fonts
# WSR loads the console's system font from the IPL ROM (__OSReadROM). We use Dolphin's freely-licensed
# replacement fonts (Data/Sys/GC) instead of Nintendo's ROM data.
mkdir -p tools/fonts
for f in font_western.bin font_japanese.bin; do
  if [[ ! -s "tools/fonts/$f" ]]; then
    log "Downloading Dolphin replacement IPL font $f"
    curl -fL "https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Data/Sys/GC/$f" -o "tools/fonts/$f"
  fi
done

# ---------------------------------------------------------------- 5. translator
log "Building the WiiCompiled translator"
dotnet build external/wiicompiled/translator/src/Translator.Cli/Translator.Cli.csproj -c Release -nologo -v quiet
TRANSLATOR="external/wiicompiled/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll"
[[ -f "$TRANSLATOR" ]] || die "translator build did not produce $TRANSLATOR"

mkdir -p disc work
cat <<EOF

$(printf '\033[1;32m')Setup complete.$(printf '\033[0m')

Next:
  1. Put your PAL Wii Sports Resort image (RZTP01; ISO/RVZ/WBFS/WIA/CISO/GCZ) in:  disc/
  2. ./scripts/extract.sh disc/<your image>        # checks the ID, extracts main.dol etc.
  3. ./scripts/analyze.sh                           # symbol map, recomp.yml, HLE coverage report
  4. ./scripts/translate.sh                         # PowerPC -> C++ (first pass)

See README.md for details.
EOF
