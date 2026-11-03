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
