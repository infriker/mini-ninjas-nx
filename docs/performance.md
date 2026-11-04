# Performance

| Gameplay on Nintendo Switch | CPU clock | GPU clock | `minininjas_fps_limit` |
|---|---|---|---|
| 25–30 FPS, depending on the scene | Stock | Stock | `30` (default) |
| Stable 30 FPS | 1224 MHz | Stock | `30` |
| 60 FPS target | 1785 MHz | 614 MHz | `0` (uncapped) |

Set the clocks on the console. To change the frame limit, edit
`minininjas.toml` beside the NRO and restart the game.

## One full-screen scene

The Xbox 360 renderer divides the main 1280×720 scene into four vertical
320×720 regions to work within Xenos EDRAM. Replaying those regions on Switch
repeats geometry submission, state processing and resolves.

I changed the game's scene-mode table before rendering initialization, retaining
1280×720 and 4× MSAA while selecting one full-screen region. The runtime gives
that exact mode full-size color and depth storage. It does not discard three
quarters of the draw stream or stretch a 320-pixel image.

Shadows, UI, post-processing and other required passes still run. A single scene
region does not mean a single GPU draw call.

The original refraction shaders assumed a quarter-width horizontal coordinate.
For the full-frame mode, the verified shaders scale the final horizontal sample
coordinate by four, after the existing ripple calculation. Vertical coordinates
and the environment cubemap lookup are unchanged.

The title-scene comparison used the same emulator configuration and shader
cache, with the last steady ten-second window from each run:

| Renderer metric | Four regions | One region |
|---|---:|---:|
| Draw calls per frame | 546 | 147 |
| Copies per frame | 10 | 4 |
| Uploads per frame | 2.04 MB | 1.15 MB |
| Peak command-buffer usage | 192 KB | 64 KB |
| Renderer image memory | 75 MB | 140 MB |

Both title-scene runs reached 60 FPS. These numbers establish less submission
work, not a measured gameplay FPS multiplier. Full-size multisampled buffers
use more memory than the old strip buffers.

## CPU work

- Inline suitable generated save/restore helpers. Rewrite direct PowerPC
  branch-and-link calls to known, unhooked entries across translation units.
  Shared tails, indirect dispatch, coroutine and unwind boundaries retain the
  original calling convention. Link the guest functions together with GCC LTO.
- The profile recorder captures guest branch frequency and samples native CPU
  stacks. Profile-use builds apply the recorded frequencies and let GCC group
  hot code. See [building.md](building.md) for recording and import.
- Native hook `sub_82417FF0` handles large aligned fills in ordinary guest
  RAM. The CPU stack samples placed it in the audio worker. It preserves the
  original access widths, register results and short/unaligned fallback.
- An optional ARM64 NEON implementation of the sampled VMX ramp,
  `sub_82501EF0`, can be tested with `minininjas_native_ramp = true` in the
  runtime TOML. Restart after changing it. Its isolated 1 KiB benchmark was
  slower in Eden, so the shipped default uses the original routine. Both hooks
  are checked against their generated originals with
  `python3 tools/perf/test_native.py`. Add `--bench` for timings or
  `--switch build/native-test.nro` for the target-side test.
- The audio workers synchronize through two alternating guest flag words in
  `sub_824F9340`. Each worker publishes its arrival and briefly spins. If
  the group is still incomplete, it sleeps until the last arrival wakes it.
  Each completed round releases its own waiters even if the next round has
  already started. Set `minininjas_worker_wait = false` in the TOML beside
  the NRO and restart to compare with the original spin loop. The default
  is `true`. The notification-based approach also appears in the
  [NFSMW frame handoff](https://github.com/StevensND/nfsmw-nx/blob/main/app/src/nfsmw_espera_fotograma.cpp).
- The register optimization keeps PowerPC integer, floating-point, vector and
  condition registers in C++ locals across operations in five profiled game
  routines and their direct leaf callees. The compiler can then keep live
  values in ARM64 registers. The pass writes back visible results at returns
  and materializes the context at calls into code outside the selected region.
  Guest stack accesses, FPSCR and coroutine save/restore keep their original
  behavior. The method was adapted from
  [Dante's Inferno](https://github.com/VasilkovND/dantes-inferno-nx/blob/main/dante/CPU_OPTIMIZATION.md).
  Set `minininjas_local_regs = false` beside the NRO and restart to run the
  original bodies with the same worker wait. The default is `true`. For an
  A/B run, use identical clocks, frame limit, save and camera route.
- Publish primary PM4 ring progress at completed packet boundaries, using the
  game's writeback interval with a 32-word minimum. Wait for upload reads
  before returning space, and sleep until the producer advances on a partial
  packet.
- Process consecutive GPU register writes in batches without losing register
  side effects.
- Reuse unchanged vertex/index uploads and host bindings; invalidate them at
  guest synchronization points.
- Keep exact content fingerprints for geometry. Correctness checks are not
  replaced with unsafe sampled hashes.
- Maintain cache eviction incrementally rather than sorting the whole cache.
- Remove routine profiling, packet counters, debug overlays and shader dumps
  from the public application.

## GPU work

Native hardware 4× MSAA replaces the expensive sample-space scene path. The
single-region mode eliminates the inter-strip clears and most repeated
resolve/packing operations while retaining the game's separate frame clear.
Compiled shaders are cached on the SD card; the release supplies a precompiled
cache to reduce initial compilation work.

### Partial Submit

Mini Ninjas sends a batch to deko3d after 128 emitted guest draws by default.
The CPU continues recording in the same work slot. Its final fence still owns
the command memory, uploads and descriptors. The next list rebinds the game's
descriptor tables, render targets, MSAA and draw state. The draw count resets
when a full work slot starts, including at a new frame.

Set `deko_partial_submit_draws = 0` in the runtime `minininjas.toml` beside
the NRO for a baseline. Compare 128, 256 and 512 on the same route with the
same clocks and FPS limit. Restart after each change.

Gameplay NROs compile out the PGO recorder, CPU stack sampler, libnx service
timings, per-draw CPU and GPU timings and Partial Submit statistics. Saved
diagnostic settings cannot re-enable them. The draw interval still works.

In a `train` build, add `deko_partial_submit_stats = true` for a diagnostic run. The numbered
game log under `logs/` reports FPS, frames exceeding 33.3 ms, draws per frame
and actual partial submissions every ten seconds. `previous_game_fence_ready`
counts completed previous game submissions at a non-blocking poll before the
first split, at draw 128 or the configured interval if smaller. Shorter work
slots have no poll. `game_gap_ms/work` measures the GPU timestamp gap between
full game submissions, averaged over measured work slots. It can include
presentation and frame pacing. Compare these figures alongside frame times.

The draw-count experiment was inspired by the [Gears of War 2 renderer patch](https://github.com/VasilkovND/gears2-nx/blob/main/gow2/sdk-patches/0039-partial-submits-during-the-frame.patch).
