#!/usr/bin/env bash
# Build a Linux release from a locally owned PAL disc, using checked-in HLE/map.
# Dependencies: run bash setup.sh first. No MKW reference image is required.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
[[ $# -ge 1 ]] || { echo 'Usage: bash scripts/release/build.sh /path/to/Resort.iso [VERSION]'; exit 2; }
IMAGE="$1"
VERSION="${2:-0.2.0-netplay-preview}"
[[ $(uname -m) == x86_64 ]] || { echo 'Online preview targets Linux x86-64.'; exit 1; }
bash scripts/extract.sh "$IMAGE"
python3 scripts/configure_project.py
bash scripts/translate.sh
bash scripts/build.sh
python3 -m venv .venv-launcher
.venv-launcher/bin/python -m pip install -r launcher/requirements.txt
.venv-launcher/bin/python scripts/release/package.py --version "$VERSION" --archive
