# Performance

On my Nintendo Switch at stock clocks, gameplay runs at **40–60 FPS**. The
result depends on the scene and camera direction. The port leaves clocks to
the system and does not impose a 30 FPS limit.

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

- Inline suitable generated save/restore helpers and direct local calls while
  preserving register context across shared tails and indirect calls.
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
