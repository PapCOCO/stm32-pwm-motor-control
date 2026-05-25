#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

for brew_bin in /opt/homebrew/bin /usr/local/bin; do
  if [[ -d "$brew_bin" ]]; then
    export PATH="$brew_bin:$PATH"
  fi
done

XPACK_GCC_BIN="$HOME/Library/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin"
if [[ -x "$XPACK_GCC_BIN/arm-none-eabi-gcc" ]]; then
  export PATH="$XPACK_GCC_BIN:$PATH"
fi

missing=()
for tool in arm-none-eabi-gcc cmake ninja; do
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

rm -rf build
cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/gcc-arm-none-eabi.cmake" \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --parallel

printf '\nBuild outputs:\n'
ls -lh build/416.elf build/416.hex build/416.bin build/416.map
