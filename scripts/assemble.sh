#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ "$#" != 1 ]; then
  echo "Usage: $0 /path/to/extracted-release-shader-package" >&2; exit 2
fi
source_dir=$(cd "$1" && pwd)
nro="$ROOT/build/switch/mini-ninjas.nro"
test -f "$nro" || { echo "Missing built NRO: $nro" >&2; exit 1; }
test -f "$source_dir/minininjas_shaders.dslib" || { echo 'Missing release shader library' >&2; exit 1; }
printf '%s  %s\n' 9f9f3a51fce53fa3989099b1a3cb9cbdfc6141b80c215511979479a4fb5938cb \
  "$source_dir/minininjas_shaders.dslib" | sha256sum -c
dest="$ROOT/build/package/switch/mini-ninjas"
test ! -e "$dest" || { echo "Destination already exists: $dest" >&2; exit 1; }
mkdir -p "$dest"
cp "$nro" "$dest/mini-ninjas.nro"
cp "$source_dir/minininjas_shaders.dslib" "$dest/minininjas_shaders.dslib"
cp "$ROOT/LICENSE" "$ROOT/NOTICE" "$dest/"
cp -R "$ROOT/LICENSES" "$dest/LICENSES"
# Cache is optional: without it, the renderer compiles shaders on demand.
if test -f "$source_dir/shadercache_dksh.bin"; then
  cp "$source_dir/shadercache_dksh.bin" "$dest/shadercache_dksh.bin"
fi
if test -f "$source_dir/cache/nfsmw_dksh.bin"; then
  mkdir "$dest/cache"
  cp "$source_dir/cache/nfsmw_dksh.bin" "$dest/cache/nfsmw_dksh.bin"
fi
echo "Output: $dest"
echo 'Copy your extracted Xbox 360 game files into its game/ directory.'
