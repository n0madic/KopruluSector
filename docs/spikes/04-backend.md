# Spike 04: presentation backend benchmark

Plan: phase 0 item 4, D9, D13a, §10 (presentation sync ≤ 3 ms per frame at 5000 images).
Code: `spikes/04-backend/` (`workload.lua`, `backends.lua`, `backend.script`); run with
`tools/spike.sh spikes/04-backend macos|web|android`.

## Workload

`workload.lua` is deterministic, seeded with CRT rand. Each logical tick (42 ms):

- 40% of the images move. Positions are in 1/256 px and presentation interpolates them.
- Every image advances one frame of a 17-frame flipbook and may be flipped.
- Each image has a player remap row.
- 20% of the images use the destination-reading material. They go through a copy of the index target and a table lookup (plan D10).
- 2% of the live images despawn and the same number spawn.
- Draw order is the native depth key: elevation, then y for elevation ≤ 4, with the image index as tie-break.

Every rendered frame writes the interpolated positions, then the render pipeline runs in full:
- index target;
- destination copy and table pass;
- palette pass.

## Backends

| Backend | How it draws | Per-frame Lua work |
|---|---|---|
| `sprite_go` | One game object and sprite component per image (factory). Order comes from z = depth rank. | `go.set_position` per image. Per tick: `cursor` (`(k+0.5)/n`, [spike 05](05-cursor.md)) and `sprite.set_hflip`. |
| `mesh_batch` | Two mesh components (normal and destination). The vertex buffer is rebuilt every frame in depth order. | 6 vertices × 9 floats per image written into buffer streams, then `resource.set_buffer`. |
| `mesh_texture` | Two static meshes of (slot, corner) vertices. Per-image data sits in an RGBA32F texture and the vertex shader expands quads with `texelFetch` (`render/materials/indexed_vt.vp`). | First version: 4 floats per image every frame, then one `resource.set_texture`. Final version (below): writes on ticks only, only changed values; the upload is on the next frame; frames set one interpolation constant. |

## Parity

On HTML5 the index target is read back through Emscripten's GL tables (`spikes/common/readback.lua`).

- **One image.** All three backends give identical bytes. They also equal a CPU render of the frame (index, flip, player remap).
- **Static scene, 300 overlapping images** (including destination images, flips and depth order):
  - `mesh_texture` and `sprite_go` give identical index targets.
  - The earlier "unexplained" `mesh_batch` difference, and the flaky `mesh_texture` results seen while reworking it, had one root cause: **`buffer.create` does not zero its memory**.
    - `mesh_batch` left unused vertices uninitialised; garbage quads were drawn.
    - `mesh_texture` left the zero-size "empty" frame of its frame table uninitialised. Unused slots, and in the diff-write version the values skipped as unchanged, then held garbage. The result depended on what the allocator returned, so it varied between instances in one session.
    - Every buffer is now written in full at creation. Afterwards a parity run of `sprite_go` followed by 6 × `mesh_texture` and 2 × `mesh_batch`, twice, and the default parity run were all byte-identical to `sprite_go`.
    - Parity settings: readback at frame 30 (`spike.parity_frames`); `spike.parity_backends` repeats backends; `spike.benchmark = 0` runs parity only.

Bugs found and fixed while building the parity check:
- Atlas geometry UVs from `resource.get_atlas` are pixels counted from the bottom. The uploaded texture has the image's top row at v = 0.
- Two mesh components must not share one `.buffer` resource, because `resource.set_buffer` replaces it for both.

## Results

Sync is the Lua time per rendered frame. "Tick" is the extra Lua time once per 42 ms tick. Logs are in `build/spikes/04-backend/`.

| Platform, Lua | n | `mesh_texture` sync mean / p95 | `mesh_batch` sync | `sprite_go` sync + tick |
|---|---:|---|---|---|
| macOS M4 Max, LuaJIT, Vulkan | 2000 | 1.08 / 1.68 ms | 2.73 ms | 0.55 + 1.46 ms |
| | 3500 | 1.56 / 2.41 ms | 4.75 ms | 1.02 + 2.66 ms |
| | 5000 | **1.62 / 2.68 ms** | 6.75 ms | 1.50 + 3.89 ms |
| HTML5 headless Chrome, PUC Lua 5.1 (wasm), WebGL 2 | 2000 | 0.98 / 1.0 ms | 5.6 ms | 1.02 + 1.84 ms |
| | 3500 | 1.62 / 2.0 ms | 9.8 ms | 1.92 + 3.32 ms |
| | 5000 | **2.25 / 3.0 ms** | 14.0 ms | 2.68 + 4.78 ms |
| Android emulator (arm64, software Vulkan), LuaJIT | 5000 | 21.9 ms (texture upload 20.0) | 12.0 ms | 2.52 + 6.89 ms |
| Galaxy Z Fold7 (Snapdragon 8 Elite, Adreno 830, Vulkan), LuaJIT **JIT off** | 2000 | 2.05–2.41 / 4.2–4.5 ms (upload 1.1–1.3) | 4.0 ms | 0.6–0.9 + 1.7–2.5 ms |
| | 3500 | 2.93–3.31 / 4.3 ms (upload 1.3–1.5) | 7.2–7.4 ms | 1.1–1.7 + 3.1–4.5 ms |
| | 5000 | **4.50–4.53 / 5.2–5.5 ms (upload 1.74–1.79)** | 10.4–11.8 ms | 1.7–2.6 + 4.7–7.0 ms |

Notes:
- On macOS the frame time is pinned to the 120 Hz display (8.3 ms) because vsync could not be disabled, so GPU cost is not visible in it. Setup of 5000 images takes ≤ 18 ms with every backend.
- **Android runs LuaJIT with the JIT disabled** (`jit.status()` is false in the Defold arm64 Android engine), so the phone measures the interpreter, as on iOS.
- On the phone `mesh_texture` exceeds the 3 ms budget at 5000 images: 2.7–2.8 ms of Lua plus 1.7–1.8 ms in `resource.set_texture`. The upload is a fixed per-call cost of Defold's Vulkan backend, not a size cost ([spike 15](15-upload.md)). The other backends are worse on the same device (`sprite_go` 6.4–9.5 ms, `mesh_batch` 10–12 ms), so the decision stands; the budget is met by cutting the Lua side and the number of uploads (ADR 0002).
- The Android emulator renders in software. There the RGBA32F upload of `mesh_texture` dominates: about 20 ms for 40 KB of texels. Without the upload the Lua side is 1.8 ms. The emulator is a functional check only. A real GPU uploads this per frame cheaply, but that is **unmeasured**.

## Final `mesh_texture` (tick writes, GPU interpolation)

The first device run put `mesh_texture` over budget (4.5 ms at 5000 images on the phone). The backend was reworked:

- **Data layout:** two texels per slot.
  - Texel 1: `(x, y, x − previous x, y − previous y)` in 1/256 px.
  - Texel 2: `(remap row × 65536 + frame × 2 + hflip)`.
  - Every value is an exact float32 integer.
- **Ticks:**
  - Positions are interpolated in the vertex shader: `pos − (1 − alpha) · delta`.
  - Lua writes slots only on simulation ticks. A shadow table skips unchanged values, because each stream element is a C call.
  - A still image changes nothing in its position texel.
- **Frames between ticks** set only the `interp` constant: no writes, no upload.
- **Upload:** the texture upload of a tick happens on the next frame, so the Lua writes and the fixed-cost `set_texture` ([spike 15](15-upload.md)) never share a frame.
  - The tick frame draws the previous tick at `interp = 1`. Those final positions equal the new tick's positions at alpha 0, so the picture holds for one frame and never jumps.
  - A pending upload waits at most one frame.

Presentation work per rendered frame: sync, plus tick writes and the upload charged to the frame where they run. Same workload: every image changes its animation frame every tick, and 40% move.

| Platform | n | mean | p95 | max | tick writes | upload |
|---|---:|---:|---:|---:|---:|---:|
| HTML5 (PUC Lua, WebGL 2; 1 ms timer) | 5000 | 0.84 ms | 2.0 ms | 4.0 ms | 2.12 ms | ~0 |
| Galaxy Z Fold7 (JIT off, Vulkan, vsync off) | 2000 | 0.06 ms | 0.54 ms | 1.90 ms | 0.61 ms | 1.57 ms |
| | 3500 | 0.21 ms | 1.60 ms | 2.44 ms | 1.41 ms | 1.67 ms |
| | 5000 | **0.35 ms** | **2.22 ms** | 3.79 ms | 2.23 ms | 2.04 ms |

- **Budget:** at 5000 images both the mean (0.35 ms) and the p95 (2.22 ms) per frame are within the 3 ms budget of §10.
  - The worst single frame, 3.79 ms, is an upload frame. Defold's `set_texture` cost varied between 1.0 and 3.4 ms across device runs for the same data; it is the engine's fixed cost ([spike 15](15-upload.md)).
  - At a 60 Hz display, one frame in every 2.5 pays about 2.2 ms of tick writes and the next pays the upload.
- **Parity:** all three backends give an identical index target on WebGL 2 (see Parity).
- Logs: `build/spikes/04-backend/android-device-final.log` and `web.log`.

## Decision

[ADR 0002](../adr/0002-presentation-backend.md):
- `mesh_texture` is the world-image backend. In its final form it is the only candidate within budget at 5000 images on PUC Lua and on a phone running the interpreter (0.35 ms mean, 2.22 ms p95).
- `sprite_go` remains for small sets: UI, cursors, a few effects.
- `mesh_batch` is rejected.

## Not measured

- iOS (no device or simulator runtime). One physical Android device (a flagship) only; no mid-range device.
- The GPU time of the passes (vsync-limited desktop).
