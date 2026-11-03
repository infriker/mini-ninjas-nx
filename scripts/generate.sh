#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test -x sdk/out/linux-amd64/rexglue || { echo 'Build the patched host tool first' >&2; exit 1; }
printf '%s  %s\n' 2747afb17badc9832ca6fbc8d06ddb309b23205410902180b525a5373c8bdce6 assets/game/default.xex | sha256sum -c
test ! -e generated && test ! -e generated-perf || { echo 'Remove or move old generated outputs before regenerating' >&2; exit 1; }
mkdir -p generated
export REXGLUE_DUMP_IMAGE="$PWD/assets/default_image.bin"
sdk/out/linux-amd64/rexglue codegen minininjas.toml
python3 tools/patch_coroutines.py generated
node tools/perf/optimize_codegen.cjs generated generated-perf
