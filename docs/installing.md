# Installation

## Browser installer

Open [the installer](https://infriker.github.io/mini-ninjas-nx/) in a modern
desktop browser and select your Xbox 360 ISO or extracted game folder. The
full package checks the executable revision and the required game-file list.
Choose **Create mini-ninjas-nx.zip**, then extract it into `sdmc:/switch/`.

Use a supported full-memory homebrew launch. Hold **R** while starting an
installed game, open the Homebrew Menu, and select **Mini Ninjas NX**.
Launching from Album/applet mode is unsupported.

All ISO reading, hashing and packaging happens locally. The site downloads
only the port release files. It does not upload your game.

## Manual installation

Download `mini-ninjas.nro` and `mini-ninjas-runtime.zip` from the
[latest release](https://github.com/infriker/mini-ninjas-nx/releases/latest).

1. Create `sdmc:/switch/mini-ninjas/`.
2. Copy the NRO there and extract the runtime ZIP into that same directory.
3. Put the contents of your extracted Xbox 360 disc under `game/`, with
   `game/default.xex`, `game/scenes/` and `game/movies/` directly below it.
4. Launch in title takeover mode.

Do not put an extra `mini-ninjas/` or disc-name directory between `game/` and
`default.xex`. The PC game files cannot replace the Xbox 360 files.

Supported `default.xex`: 7,712,768 bytes, Title ID `534307E4`, SHA-256:

```text
2747afb17badc9832ca6fbc8d06ddb309b23205410902180b525a5373c8bdce6
```

## Updates and saves

For v1.0.8, an existing v1.0.3–v1.0.7 or v1.0.8 release-candidate installation
needs only the new `mini-ninjas.nro`. The runtime package is the same as v1.0.7;
existing game files, configuration, saves and shader cache can be reused.
The new grass shader compiles into a separate cache entry when first used.

Close the game before replacing files. The installer's update package replaces
the port NRO, supplied shader package, default settings and notices without copying the game
again. It does not include save files. Keep the existing game data, save
directories and locally created cache when updating; back up the entire
`sdmc:/switch/mini-ninjas/` directory before a major update.

The game creates its own writable data beside the NRO. Do not remove those
directories when installing a new executable. A fresh install and an update
use the same supported game revision.

## Frame limit

The supplied runtime settings target 30 FPS. Edit
`sdmc:/switch/mini-ninjas/minininjas.toml`, beside the NRO:

```toml
minininjas_fps_limit = 30
```

Set the value to `0` to disable the additional frame limit, then restart the
game. Without this file or setting, the NRO retains its uncapped behavior.
The limiter paces game frame submissions; it does not change game time or
the video mode's vblank frequency. A demanding scene can still run below the
selected limit.

Keep a copy of your edited settings before extracting an update, which includes
the default `minininjas.toml`. The source repository's root `minininjas.toml`
is the recompilation input; the runtime template is in `resources/runtime/`.

## Distribution contents

The source repository contains the port code, SDK patches, build tools,
documentation and browser installer. It excludes the original executable,
disc image, extracted game contents and generated PowerPC translation.

The release NRO contains the statically recompiled game program and runtime.
The separate shader package contains the original shader program containers
used for matching, their translated SPIR-V, and compiled Switch shader code.
It is not a collection of game textures, models, levels, audio or movies, and
it cannot run the game without the supported original files. These program
data are not relicensed under the port's GPL license.

Mini Ninjas NX is an independent research project. Supply your own supported
Xbox 360 copy. No game download is provided by this project.
