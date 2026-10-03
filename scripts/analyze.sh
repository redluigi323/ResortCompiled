#!/usr/bin/env bash
# Thin wrapper so every step is ./scripts/<step>.sh. Passes all args to analyze.py (see --help).
exec python3 "$(dirname "${BASH_SOURCE[0]}")/analyze.py" "$@"
