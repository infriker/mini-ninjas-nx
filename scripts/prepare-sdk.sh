#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test ! -e sdk || { echo 'sdk/ already exists; use a fresh checkout' >&2; exit 1; }
sha256sum -c patches/SHA256SUMS
git clone --no-checkout https://github.com/nx-mod/rexglue-sdk-nx.git sdk
git -C sdk checkout --detach 678f3ff20ef4229b2653410da7e8328973b2f081
git -C sdk apply --check ../patches/rexglue-nx.patch
git -C sdk apply ../patches/rexglue-nx.patch
git -C sdk apply --check ../patches/rexglue-nx-new.patch
git -C sdk apply ../patches/rexglue-nx-new.patch
git -C sdk apply --check ../patches/runtime-logging.patch
git -C sdk apply ../patches/runtime-logging.patch
git -C sdk apply --check ../patches/audio-client-lifetime.patch
git -C sdk apply ../patches/audio-client-lifetime.patch
git -C sdk apply --check ../patches/minininjas-depth-bias.patch
git -C sdk apply ../patches/minininjas-depth-bias.patch
git -C sdk submodule update --init --depth 1 --jobs "${JOBS:-4}" \
  thirdparty/cli11 thirdparty/libmspack thirdparty/FFmpeg thirdparty/tomlplusplus \
  thirdparty/simde thirdparty/xxHash thirdparty/spdlog thirdparty/fmt thirdparty/utfcpp \
  thirdparty/imgui thirdparty/o1heap thirdparty/inja thirdparty/sdl3 \
  thirdparty/vulkan-headers thirdparty/vulkan-memory-allocator \
  thirdparty/spirv-headers thirdparty/spirv-tools thirdparty/glslang
echo 'Pinned SDK and submodules ready. Build the host tool before applying the Switch overlay.'
