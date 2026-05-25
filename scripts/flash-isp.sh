#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HEX_PATH="$PROJECT_ROOT/build/416.hex"

if [[ ! -f "$HEX_PATH" ]]; then
  printf 'Error: build/416.hex not found. Please run ./scripts/build.sh first.\n' >&2
  exit 1
fi

cd "$PROJECT_ROOT"

exec python3 scripts/flash_isp.py \
  --hex build/416.hex \
  --port /dev/cu.usbserial-1140 \
  --baud 115200 \
  "$@"
