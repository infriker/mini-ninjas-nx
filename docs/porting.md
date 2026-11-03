# Porting notes

I used the Xbox 360 executable as the input to static recompilation, with
ReXGlue supplying the guest runtime and the Switch fork supplying the Horizon
and deko3d foundations. The game files remain external to the port.

## What helped

**Dante's Inferno NX** provided a useful starting point for the GPU command
reader, shader-library loading and CPU helper transformations. I adapted that
code to Mini Ninjas' actual command stream, shader interfaces and runtime
behavior instead of treating the game as an interchangeable executable.

**NFSMW-NX** demonstrated how a game's render-mode configuration can be changed
before target creation. **MarathonRecomp** reinforced the need to coordinate
the region count with target sizes, clears and resolves. These references
helped guide the one-region rendering work; the addresses, surface layout and
mode signature used here come from Mini Ninjas itself.

**F.E.A.R. NX** supplied an Eden storage-buffer descriptor fix and the public
repository/installer pattern. Its installer in turn credits Project8 SW and
NFSMW-NX. Those attributions are retained in the installer notices.

## Recompilation and runtime

- Recover indirect function targets and shared tails needed by menus and levels.
- Preserve live guest register state across those boundaries.
- Bridge the game's coroutine transitions to the Switch runtime.
- Fix the loader and memory behavior required for level loading.
- Optimize generated helper calls without rewriting the game's logic or
  weakening synchronization.
- Keep active audio callbacks alive during client teardown and reject late
  submissions to a removed driver. This prevents a null-driver crash when
  skipping the opening movie before its audio callback finishes.

## Rendering

- Adapt the PM4 command reader and shader lookup to the game's shader containers
  and runtime-patched vertex fetch instructions.
- Match shader vertex inputs and formats, including the skinning bone indices.
- Correct resolve placement and remove the seams left by assembling horizontal
  regions into the final image.
- Use native 4× MSAA and replace four scene regions with one 1280×720 region.
- Keep full-size color/depth storage scoped to the verified Mini Ninjas mode,
  leaving unrelated render passes under their existing rules.
- Retain cache freshness, barriers, bounds and format checks that affect
  correctness.

## Public release

The release keeps the working CPU and rendering optimizations, adds the Mini
Ninjas NX icon, and removes the investigation tools from the published tree.
It has no debug overlay, service shortcuts, periodic performance reports or
raw shader dumps. Warnings and errors remain available when a required file
is missing or invalid.

The browser installer checks the supported executable and assembles the SD
card package entirely on the local computer. Neither GitHub Pages nor this
repository receives the selected game files.
