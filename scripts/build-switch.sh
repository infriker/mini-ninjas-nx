#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/tools/bin:$DEVKITPRO/devkitA64/bin:$PATH"
generated="${MINININJAS_GENERATED_DIR:-$ROOT/generated-perf}"
test -f "$generated/sources.cmake" || { echo 'Run scripts/generate.sh first' >&2; exit 1; }
if ! grep -q '"crossUnitCalls"' "$generated/perf-manifest.json"; then
  echo 'This generated tree predates cross-unit calls. See docs/building.md.' >&2; exit 1
fi
mode="${1:-release}"
build_dir="$ROOT/build/switch"
profile_dir=""
case "$mode:$#" in
  release:0|release:1) pgo=off ;;
  train:1) pgo=generate; build_dir="$ROOT/build/switch-train" ;;
  use:2) pgo=use; profile_dir="$(cd "$2" && pwd)" ;;
  *) echo "Usage: $0 [release | train | use /path/to/profile]" >&2; exit 2 ;;
esac
test -x "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-g++" || { echo 'devkitA64 not found' >&2; exit 1; }
test -f "$DEVKITPRO/libnx/lib/libdeko3d.a" || { echo 'deko3d not found' >&2; exit 1; }
# The host tool must already have been built: the overlay changes FFmpeg config.
test -x "$ROOT/sdk/out/linux-amd64/rexglue" || { echo 'Build host tool before Switch overlay' >&2; exit 1; }
python3 "$ROOT/sdk/tools/switch_setup.py"
cmake -S "$ROOT" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/switch-devkitA64.cmake" -DREXSDK_DIR="$ROOT/sdk" \
  -DREXGLUE_SWITCH_DEKO3D=ON -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_ENABLE_PERF_COUNTERS=OFF \
  -DMINININJAS_GENERATED_DIR="$generated" -DMINININJAS_LTO="${MINININJAS_LTO:-ON}" \
  -DMINININJAS_LTO_JOBS="${LTO_JOBS:-4}" -DMINININJAS_PGO="$pgo" -DMINININJAS_PROFILE_DIR="$profile_dir"
cmake --build "$build_dir" -j "${JOBS:-4}"
check_args=()
if [[ "$mode" == train ]]; then check_args+=(--recorder); fi
python3 "$ROOT/tools/perf/verify_link.py" "$build_dir/mini-ninjas" "$generated" \
  "$DEVKITPRO/devkitA64/bin/aarch64-none-elf-nm" "${check_args[@]}"
echo "NRO: $build_dir/mini-ninjas.nro"
echo 'The NRO requires the matching release shader package and your own game files; see docs/building.md.'
