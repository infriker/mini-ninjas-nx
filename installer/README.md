# Mini Ninjas NX browser installer

The installer runs locally in a modern desktop browser. Select your own Xbox
360 disc image or an extracted game folder. The supported executable is
identified by SHA-256; a full package also requires the game files listed in
`lib/game-files.js` at their expected sizes. Game data is read and packaged on
your computer; it is not uploaded. The resulting ZIP is streamed to the browser
download and extracts into `sdmc:/switch/mini-ninjas/`.

An update package requires only the supported `default.xex` and does not copy
game data. It includes the release NRO and runtime files. Keep your saves and
back up any configuration you changed before updating.

## Publishing the site

Publish `mini-ninjas.nro` and `mini-ninjas-runtime.zip` as release assets, then
set the exact release tag and lowercase SHA-256 values for both assets in
`release/manifest.json`. The Xbox executable fingerprint is independent of the
port release and must remain the supported one. The Pages workflow downloads
the pinned assets, checks both hashes, and builds the site. A missing hash or
tag makes the workflow fail instead of publishing an unverified installer.

Commit the manifest to `main`, then run **Actions → Publish installer → Run
workflow** with `main` selected. Deployment uses the main branch allowed by
the GitHub Pages environment; publishing a release tag alone does not deploy
the installer.

The runtime ZIP may contain files at its root or under `mini-ninjas/`. It must
include `minininjas_shaders.dslib`; `shadercache_dksh.bin`, `minininjas.toml`,
license files and a `LICENSES/` directory are allowed. The archive must not
contain game files, an NRO, saves, logs or cache directories. The site builder
emits a hash for each runtime file, which the browser checks before packaging.

For local site preparation, run `prepare_site.py` with `--nro`, `--runtime` and
`--output` paths. Use a fresh output directory and serve it over localhost;
service workers and streaming downloads require a secure browser context. See
`NOTICES.md` for source attribution and licensing.
