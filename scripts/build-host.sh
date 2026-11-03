#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test -d sdk/thirdparty/FFmpeg/libavcodec || { echo 'Run scripts/prepare-sdk.sh first' >&2; exit 1; }
cmake -S sdk -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="${CC:-clang-19}" -DCMAKE_CXX_COMPILER="${CXX:-clang++-19}" \
  -DCMAKE_C_FLAGS=-march=x86-64-v3 -DCMAKE_CXX_FLAGS=-march=x86-64-v3 \
  -DREXGLUE_USE_VULKAN=ON -DREXGLUE_ENABLE_TRACY=OFF \
  -DREXGLUE_ENABLE_PERF_COUNTERS=OFF -DSDL_UNIX_CONSOLE_BUILD=ON \
  -DSDL_X11_XSCRNSAVER=OFF -DSDL_X11_XTEST=OFF -DSDL_WAYLAND_LIBDECOR=OFF
cmake --build build/host --target rexglue -j "${JOBS:-4}"
sdk/out/linux-amd64/rexglue --version
