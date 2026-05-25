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
BUILD_TYPE="${BUILD_TYPE:-Release}"
CMAKE_GENERATOR="${CMAKE_GENERATOR:-Ninja}"
CMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE:-cmake/gcc-arm-none-eabi.cmake}"
REQUIRED_BUILD_TOOLS="${REQUIRED_BUILD_TOOLS:-arm-none-eabi-gcc cmake ninja}"
CLEAN_BUILD="${CLEAN_BUILD:-1}"
XPACK_GCC_BIN="${XPACK_GCC_BIN:-$HOME/Library/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin}"

for brew_bin in /opt/homebrew/bin /usr/local/bin; do
  if [[ -d "$brew_bin" ]]; then
    export PATH="$brew_bin:$PATH"
  fi
done

if [[ -x "$XPACK_GCC_BIN/arm-none-eabi-gcc" ]]; then
  export PATH="$XPACK_GCC_BIN:$PATH"
fi

missing=()
for tool in $REQUIRED_BUILD_TOOLS; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    missing+=("$tool")
  fi
done

if (( ${#missing[@]} > 0 )); then
  printf 'Error: missing required build tool(s): %s\n' "${missing[*]}" >&2
  printf 'Install CMake, Ninja, and a complete Arm GNU toolchain, then put them on PATH.\n' >&2
  printf 'For this project on macOS, xPack GNU Arm Embedded GCC is supported.\n' >&2
  exit 1
fi

cd "$PROJECT_ROOT"

if [[ "$CLEAN_BUILD" != "0" ]]; then
  rm -rf "$BUILD_DIR"
fi

if [[ "$CMAKE_TOOLCHAIN_FILE" = /* ]]; then
  TOOLCHAIN_PATH="$CMAKE_TOOLCHAIN_FILE"
else
  TOOLCHAIN_PATH="$PWD/$CMAKE_TOOLCHAIN_FILE"
fi

cmake -S . -B "$BUILD_DIR" -G "$CMAKE_GENERATOR" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_PATH" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

cmake --build "$BUILD_DIR" --parallel

printf '\nBuild outputs:\n'
ls -lh \
  "$BUILD_DIR/$PROJECT_NAME.elf" \
  "$BUILD_DIR/$PROJECT_NAME.hex" \
  "$BUILD_DIR/$PROJECT_NAME.bin" \
  "$BUILD_DIR/$PROJECT_NAME.map"
