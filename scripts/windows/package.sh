#!/usr/bin/env bash
# Freeze using Windows Python under Wine; keep output local.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
VERSION="${1:-0.3.5-eos-preview}"
bash scripts/windows/wine.sh work/windows/python/python.exe scripts/release/package.py \
    --runtime-dir out/windows --dtk work/windows/dtk.exe --version "$VERSION" \
    --output dist/windows --archive --eos-config work/eos/client.json \
    --eos-sdk eosstuff/SDK --eos-bridge-dir work/windows/eos
