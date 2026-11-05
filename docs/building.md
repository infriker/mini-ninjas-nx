# Building Mini Ninjas NX

This builds the Switch NRO from the supported Xbox 360 executable. The source
repository does not include the original executable, game data or generated
game translation. You need your own supported game copy and a shader package
from the project release. The package contains original Xbox shader programs
and their translated SPIR-V; it is not a standalone copy of the game.

## Requirements

- Linux x86-64 with an x86-64-v3-capable CPU, Docker, Git and Python 3; Windows users may use Docker
  Desktop with a Linux container and a checkout mounted at `/project`.
- The extracted Xbox 360 game, including the supported `default.xex`. The
  tested executable SHA-256 is
  `2747afb17badc9832ca6fbc8d06ddb309b23205410902180b525a5373c8bdce6`.
- The matching release shader package, extracted to a separate directory. Its
  `minininjas_shaders.dslib` SHA-256 is
  `9f9f3a51fce53fa3989099b1a3cb9cbdfc6141b80c215511979479a4fb5938cb`.
- Network access for the SDK and its pinned submodules, plus ample disk space
  for the SDK, generated C++, container image and build output. Do not commit
  any of these private inputs or outputs.

The following commands use the repository root as the working directory.
Inside Docker they use `/project`; substitute the actual checkout path when
mounting it. On Windows, Docker Desktop must have permission to access that
directory. The Docker base is pinned by digest; Debian packages are installed
at build time and are not a bit-reproducible snapshot.

```sh
docker build -t mini-ninjas-nx-build -f tools/Dockerfile .
docker run --rm -it -v "$(pwd):/project" -w /project mini-ninjas-nx-build bash
```

Copy your own extracted files to `assets/game/`; at minimum the executable
must be at `assets/game/default.xex`. `scripts/generate.sh` verifies its exact
hash before code generation. All game files are needed to run the result.

Inside the container:

```sh
bash scripts/prepare-sdk.sh
bash scripts/build-host.sh
bash scripts/generate.sh
bash scripts/build-switch.sh
```

`prepare-sdk.sh` refuses to overwrite an existing `sdk/`; it verifies the
patch checksums, checks out `nx-mod/rexglue-sdk-nx` at commit
`678f3ff20ef4229b2653410da7e8328973b2f081`, applies the patches in order,
and initializes the required SDK submodules at their pinned commits. The Switch setup
later fetches SPIRV-Cross and uam at the revisions recorded in the SDK's
`tools/switch_setup.py`. Keep the host build before Switch setup: the latter
overlays Switch-specific FFmpeg configuration on the dependency tree.

The generated translation is patched at six checked coroutine sites, then
copied into `generated-perf/` with the guarded helper/direct-call pass. No
guest-stack nonvolatile optimization is applied. These directories, the dumped
image `assets/default_image.bin`, SDK checkout, game files and build output
are local artifacts. Regeneration refuses existing output directories so they
cannot be accidentally replaced.

`build-switch.sh` builds with native 4x MSAA and the full-frame
scene mode enabled. The output is `build/switch/mini-ninjas.nro`. Compiler,
dependency or platform differences can change the resulting binary hash; a
successful build alone does not establish visual correctness or performance.

Version 1.0.7 enables deferred colour/depth clears, useful-row clears,
duplicate-clear elimination, compatible full depth image swaps and deferred
single-sample front-buffer copies. Resolution and native 4x MSAA are unchanged.
The depth swap requires a following clear; mismatched formats, partial depth
resolves and MSAA resolves retain the copy path. A front-buffer snapshot stays
valid after presentation: a later read or source write materializes its copy,
while a complete replacement can discard it. This conservative lifetime rule
means front-buffer savings depend on the game's actual pass ordering.

It also enables early depth tests for compatible effects
with no depth/stencil writes, and skips the regular fragment shader when no
colour is written and neither alpha testing, shader discard nor shader depth
output requires it. These use the SDK's guarded paths alongside the clear/copy
changes. Run `python3 tools/test_gpu_shader_guards.py` to check the actual
shader-selection guards before a console comparison.

Run `python3 tools/test_gpu_targets.py` after preparing the SDK for CPU image
tests of the actual clear and snapshot control flow (requires a host C++
compiler with AddressSanitizer and UBSan). This does not validate deko3d
commands or establish a console FPS improvement. The published 1.0.7 has the
same executable code as the Switch-tested 1.0.7-rc.2, with only the NACP display
version changed. Modified builds still require console testing.

For the 1.0.8 MSAA/target-size changes, also run
`python3 tools/test_gpu_msaa_extents.py` in the build container. It needs the
host C++ compiler and devkitPro's `uam`; `UAM` can override the compiler path.
See [performance.md](performance.md#v108-gpu-optimizations) for
the changed paths and their console-test limitations.

For the 1.0.8 grass shader, the test requires the separately obtained release
shader library, a host C++ compiler, and SPIR-V Tools. It extracts two modules
into a temporary build directory; no game shader is embedded in the tests:

```sh
mkdir -p build/foliage-test
g++ -std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -no-pie \
  -Isrc -Ishaders -Isdk/thirdparty/xxHash tools/test_foliage_shader.cpp \
  shaders/ds_shader_library.cpp -o build/foliage-test/check
build/foliage-test/check /path/to/minininjas_shaders.dslib build/foliage-test
spirv-val build/foliage-test/grass-original.spv
spirv-val build/foliage-test/grass-patched.spv
python3 tools/test_foliage_shader.py build/foliage-test/grass-original.spv \
  build/foliage-test/grass-patched.spv
```

The tests compare the original module's control flow and output against the
production patch, check incompatible/truncated modules and all other library
entries, and verify that the cache key changes. Texture sample values are
inputs to the interpreter; GPU filtering and console frame times are not
simulated.

### CPU builds and on-console profiling

The release build uses bounded GCC LTO on the translated game functions. Set
`LTO_JOBS=2` on a small build machine. Set `MINININJAS_LTO=OFF` for a
comparison build. An existing checkout with the old local-call optimization
can reuse its patched `generated/` tree with these commands. Keep this source
path for both recording and profile use.

```sh
mkdir -p build
node tools/perf/optimize_codegen.cjs generated build/generated-cpu
export MINININJAS_GENERATED_DIR="$PWD/build/generated-cpu"
```

1. Use `bash scripts/build-switch.sh release` to build the FPS comparison NRO
   at `build/switch/mini-ninjas.nro`.
2. Run `bash scripts/build-switch.sh train` and install
   `build/switch-train/mini-ninjas.nro` in the usual game directory. This
   build collects guest branch counters and CPU stacks. Keep its matching
   unstripped `build/switch-train/mini-ninjas` ELF for address lookup.
3. Play representative levels, including combat and animation. The first PGO
   checkpoint is due 30 seconds after the first guest swap. Further checkpoints
   are due every two minutes while frames are presented, on reported focus loss
   and on a normal close. Each launch gets its own `pgo/run-N/` folder next
   to the NRO. A completed `profile-K.pgo` contains the whole session up to
   that point. Use only the highest numbered completed file from each run.
   Copy `logs/rex/rex_perfil.log` before the next recorder launch to retain
   the CPU stacks. The startup report and the ten-second intervals use offsets
   relative to this exact ELF.
4. In the same devkitA64 container import one completed stream per session.
   The importer checks every unit, reconstructs GCC's per-run maximum from
   the actual branch counters and can merge multiple runs from the same NRO.

```sh
python3 scripts/import-profile.py build/pgo-data /absolute/path/to/run-N/profile-K.pgo
bash scripts/build-switch.sh use "$PWD/build/pgo-data"
```

The resulting `build/switch/mini-ninjas.nro` uses the recorded branch
frequencies and GCC's profile-based function ordering. Both gameplay modes
(`release` and `use`) exclude the recorder, service-call timing wrappers,
CPU stack sampler and Partial Submit reports at build time. Keep the compiler,
generated sources and source paths identical. Missing or incompatible
translation-unit profiles fail the build. To look up a sampled `imagen+0x...`
offset, pass its hexadecimal part to
`aarch64-none-elf-addr2line -fCe build/switch-train/mini-ninjas 0x...`.

The register-localization pass generates a second implementation for the
targets in `tools/perf/local_regs.json`. Profile recording uses their original
bodies. The additional bodies are compiled separately using the same floating
point and aliasing settings and participate in the gameplay LTO link. To
compare register state and memory with the original code on a Linux host, run
`python3 tools/perf/test_local_regs.py build/generated-cpu` in the build container.

To assemble the installable application files from the locally built NRO and
the separately extracted release shader package:

```sh
bash scripts/assemble.sh /absolute/path/to/shader-package
```

It verifies the library hash and writes `build/package/switch/mini-ninjas/`,
refusing to replace an existing output directory. An included shader cache is
copied if present; a cache is optional and missing variants compile on demand.
Copy your own extracted game contents into the package's `game/` directory,
then copy `switch/mini-ninjas/` to the SD card. The package does not copy your
game files for you.

## Shader package

The release shader library contains the original shader program bytes used
for exact matching, together with translated SPIR-V. It is a separate runtime
input and does not include the original executable, textures, models, levels,
audio or video. A fully verified tool for rebuilding the complete shader
library solely from game files is not currently available. Building the NRO
does not require the shader package, but running the application does.

See [RELINKING.md](RELINKING.md) for the SDK and LGPL dependency sources.
