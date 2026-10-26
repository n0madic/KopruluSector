# ADR 0002: Presentation backend

Status: accepted (phase 0, 2026-10-12); the data layout and draw order were extended by [ADR 0006](0006-depth-ranked-composition.md) in phase 2 (one shared image-data texture, one mesh per material family, composition by depth rank). Evidence: [spike 04](../spikes/04-backend.md), [spike 05](../spikes/05-cursor.md), [spike 06](../spikes/06-palette.md).

Superseded in part by [ADR 0012](0012-sdl3-runtime.md) (phase S, 2026-10-25): the Defold mechanisms (`mesh_texture` through Defold meshes and `resource.set_texture`, the one-upload-per-frame rule (since stage S8 the image data goes up on its tick's frame), the `buffer.create` zeroing) give way to `kp_gfx` on GLES3/WebGL2; the slot layout and the vertex expansion carry over.

## Decision

World images (units, buildings, projectiles, effects, doodads) use **`mesh_texture`**:
- Static meshes of `(slot, corner)` vertices.
- Per-image data in an RGBA32F texture, two texels per slot: position and the delta from the previous tick (1/256 px), then remap row × 65536 + frame code with the flip bit.
  - Slots follow native depth order (D13a).
  - Lua writes them on simulation ticks only, and only values that changed. The upload is on the frame after the tick.
  - The vertex shader interpolates positions with the `interp` constant, so frames between ticks cost one constant per mesh.
- A frame table texture with atlas UV rectangles and sizes.
- Vertex expansion in `render/materials/indexed_vt.vp`.
- Destination-reading images use the same scheme with `indexed_dest_vt.material` in the destination pass.

`sprite_go` (one game object and sprite per image) stays for small sets: UI, cursors, selection markers, a few overlays. For flipbooks, `cursor = (k + 0.5) / n` selects frames exactly.

`mesh_batch` (vertex buffers rewritten from Lua) is rejected.

## Why

| At 5000 images, Lua time per frame | macOS LuaJIT | HTML5 PUC Lua (iOS-interpreter proxy) |
|---|---|---|
| `mesh_texture` | 1.6 ms (p95 2.7) | 2.25 ms (p95 3.0) |
| `sprite_go` | 1.5 ms + 3.9 ms per tick | 2.7 ms + 4.8 ms per tick |
| `mesh_batch` | 6.8 ms | 14.0 ms |

- Only `mesh_texture` meets the §10 budget (≤ 3 ms at 5000 images) on the interpreter proxy.
- `sprite_go` pays per-object engine calls for frame and flip changes every tick.
- `mesh_batch` writes 54 floats per image through buffer streams. Each stream element is a C call ([spike 10](../spikes/10-data.md)).
- A 300-image overlapping scene with flips, player remaps, destination images and depth order gives a byte-identical index target with all three backends, read back on WebGL 2.

## Consequences

- The backend interface (plan D9) is "ordered list of images → slots". The presentation layer sorts by the native depth key and writes slots. Unused slots get a zero-size frame code.
- Frame tables are built per atlas from `resource.get_atlas` geometry. UVs are pixels counted from the bottom; the uploaded texture's top image row is v = 0 (fixed during spike 04).
- Each mesh component needs its own `.buffer` resource. `resource.set_buffer` replaces the shared resource for every user.
- **Measured on a phone (Galaxy Z Fold7, Adreno 830, Vulkan, Lua interpreter).**
  - The first version, which wrote every slot every frame, took 4.5 ms per frame at 5000 images, over budget.
  - The final version (tick-only diff writes, GPU interpolation, deferred upload) takes **0.35 ms mean and 2.22 ms p95** per frame (vsync off); the worst frame is 3.79 ms. That is within the §10 budget. Details are in [spike 04](../spikes/04-backend.md).
  - `sprite_go` (5.7 ms) and `mesh_batch` (11.1 ms) remain slower on the same device.
- `buffer.create` does not zero its memory. Every buffer handed to the GPU is written in full at creation; this was the cause of the earlier parity differences ([spike 04](../spikes/04-backend.md)).
- **Rule:** at most one texture upload per rendered frame, never on the frame that does the tick writes.
  - Defold's Vulkan `set_texture` has a fixed per-call cost of 1–3.4 ms on the phone ([spike 15](../spikes/15-upload.md)).
  - The palette is a render constant ([ADR 0005](0005-render-pipeline-clock-data.md)).
- Re-measure on a mid-range Android device and on iOS. A hybrid with `sprite_go` for static images stays the fallback.
- Vertex texture fetch and RGBA32F sampling require GLES 3 / WebGL 2 ([ADR 0003](0003-atlases-and-graphics-base.md)).
