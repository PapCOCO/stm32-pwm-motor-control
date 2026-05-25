#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_CONFIG="${PROJECT_CONFIG:-$SCRIPT_DIR/project.env}"

if [[ -f "$PROJECT_CONFIG" ]]; then
  # shellcheck source=/dev/null
  source "$PROJECT_CONFIG"
fi

PROJECT_NAME="${PROJECT_NAME:-416}"
BUILD_DIR="${BUILD_DIR:-build}"
ISP_HEX="${ISP_HEX:-$BUILD_DIR/$PROJECT_NAME.hex}"
ISP_PORT="${ISP_PORT:-/dev/cu.usbserial-1140}"
ISP_BAUD="${ISP_BAUD:-115200}"

if [[ "$ISP_HEX" = /* ]]; then
  HEX_PATH="$ISP_HEX"
else
  HEX_PATH="$PROJECT_ROOT/$ISP_HEX"
fi

if [[ ! -f "$HEX_PATH" ]]; then
  printf 'Error: %s not found. Please run ./scripts/build.sh first.\n' "$ISP_HEX" >&2
  exit 1
fi

cd "$PROJECT_ROOT"

exec python3 scripts/flash_isp.py \
  --hex "$ISP_HEX" \
  --port "$ISP_PORT" \
  --baud "$ISP_BAUD" \
  "$@"
