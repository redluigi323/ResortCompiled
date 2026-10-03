#!/usr/bin/env bash
# First-pass translation of Wii Sports Resort with the WiiCompiled translator.
# Produces C++ under work/generated/. This does NOT produce a playable build yet: the runtime's
# HLE hooks are still keyed to Mario Kart Wii addresses (see work/reports/hle_coverage.md).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ -x "$HOME/.dotnet/dotnet" ]] && export PATH="$HOME/.dotnet:$PATH"
export DOTNET_CLI_TELEMETRY_OPTOUT=1

WC="$ROOT/external/wiicompiled"
MANIFEST="$WC/projects/wsr/recomp.yml"
TRANSLATOR="$WC/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll"
[[ -f "$TRANSLATOR" ]] || { echo "translator not built - run ./setup.sh" >&2; exit 1; }
[[ -f "$MANIFEST" ]]   || { echo "manifest missing - run ./scripts/analyze.sh" >&2; exit 1; }

grep -qE '^\s+sda_base: 0x' "$MANIFEST" || { echo "sda_base not set in $MANIFEST - fill it in first" >&2; exit 1; }
ENTRY="$(awk '/entry_points:/{getline; gsub(/[- ]/,""); print; exit}' "$MANIFEST")"

# WSR-native (HLE) sources. Empty until hooks are re-keyed; keeps MKW-address registrations out of the index.
NATIVE="$ROOT/runtime/wsr/src"
GEN="$ROOT/work/generated"
mkdir -p "$ROOT/work/logs" "$NATIVE" "$GEN"
LOG="$ROOT/work/logs/translate-$(date +%Y%m%d-%H%M%S).log"
run_tr() { (cd "$WC" && dotnet "$TRANSLATOR" "$@") 2>&1 | tee -a "$LOG"; }

echo "==> info"; run_tr info --project "$MANIFEST"
echo "==> translate-recursive $ENTRY"; run_tr translate-recursive "$ENTRY" --project "$MANIFEST" --output-metadata "$GEN/base_translation_output.json"
echo "==> generate-data-init"; run_tr generate-data-init --project "$MANIFEST"
echo "==> emit-build-shards"; run_tr emit-build-shards --project "$MANIFEST" \
  --base-metadata "$GEN/base_translation_output.json" --base-functions-dir "$GEN/functions" \
  --native-source-dir "$NATIVE" --out "$GEN/build_shards"

echo
echo "Translation finished. Log: ${LOG#$ROOT/}"
echo "Unsupported instructions became runtime traps (allow_unsupported_instructions: true):"
grep -ciE 'unsupported' "$LOG" || true
