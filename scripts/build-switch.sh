#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH"
test -f "$ROOT/generated-perf/sources.cmake" || { echo 'Run scripts/generate.sh first' >&2; exit 1; }
test -x "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" || { echo 'devkitA64 not found' >&2; exit 1; }
test -f "$DEVKITPRO/libnx/lib/libdeko3d.a" || { echo 'deko3d not found' >&2; exit 1; }
# The host tool must already have been built: the overlay changes FFmpeg config.
test -x "$ROOT/sdk/out/linux-amd64/rexglue" || { echo 'Build host tool before Switch overlay' >&2; exit 1; }
python3 "$ROOT/sdk/tools/switch_setup.py"
cmake -S "$ROOT" -B "$ROOT/build/switch" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/switch-devkitA64.cmake" -DREXSDK_DIR="$ROOT/sdk" \
  -DREXGLUE_SWITCH_DEKO3D=ON -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_ENABLE_PERF_COUNTERS=OFF \
  -DMINININJAS_GENERATED_DIR="$ROOT/generated-perf"
cmake --build "$ROOT/build/switch" -j "${JOBS:-4}"
echo "NRO: $ROOT/build/switch/mini-ninjas.nro"
echo 'The NRO requires the matching release shader package and your own game files; see docs/building.md.'
