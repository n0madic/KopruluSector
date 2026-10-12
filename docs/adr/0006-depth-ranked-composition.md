# ADR 0006: World composition by depth rank

Status: accepted (phase 2, 2026-10-12). The design was approved by the user before implementation. Evidence: the viewer's composition check ([viewer](../../viewer/README.md), `tools/viewer_compose_check.py`).

## Context

The original draws sprites in ascending native depth key (D13a). Within a sprite it draws images from the tail to the head, each through its draw method. Some methods read the pixel already in the buffer: shadow (10), the blend tables (9), fire (17), and the cloak and EMP distortions (2–8).

Before this decision, [ADR 0002](0002-presentation-backend.md) used `mesh_texture` with one mesh per atlas, with the slots of a mesh in depth order. That ordered images within an atlas but not across the six atlases of a mission. It also drew all destination images as one run after the rest (a TODO in the render script).

## Decision

1. **Rank.** Every tick `presentation/draw_list.lua` builds the native draw list.
   - It first refreshes the image bounds of every sprite in the map rows (`RefreshRowSpriteBounds 0x4165d0` → `0x40ee70`), as the original's draw callback does before each redraw. Bounds follow the current frame, offsets and sprite position; without the refresh, animated frames with other GRP rectangles were drawn at their creation bounds.
   - It takes the sprites visible to the local player, ordered by `GetSpriteDepthKey 0x4160b0`. Equal keys are ordered by record: the native key ORs the sprite index into its low 9 bits, and the heap order for equal keys is not established.
   - It takes each sprite's images from the tail to the head, skipping hidden ones.
   - Each image gets its position in this list, its **rank**. The vertex shader turns the rank into depth.
2. **Overwriting images.** Methods 0/1, 13, 14 and 16 draw with depth test GREATER and depth writes on. Whatever mesh an image is in, the pixel ends up with the image that is last in the native order.
3. **Destination images.** Methods 9, 10 and 17 draw after all overwriting images, with depth test GREATER and depth writes off.
   - A destination image changes a pixel only where no later image has overwritten it, which is exactly the sequential result.
   - The images are split into **layers**: an image's layer is one more than the highest layer of the earlier destination images it overlaps.
   - Before each layer the index target is copied, and the layer reads that copy. Overlapping shadows therefore stack in native order.
4. **Distortion images** (cloak, EMP; methods 2–8) read displaced pixels, so depth cannot order them. Each one will close a rank **window**: overwriting images are drawn per window (the vertex shader collapses ranks outside the window), then the index target is copied and the distortion is drawn. The window mechanism is in the shader and the render script, but these methods are not drawn yet.
5. **Families and shared data.**
   - One mesh per material family (`world` and `world_dest` for now), all reading **one** RGBA32F image-data texture. That keeps one upload per frame (spike 15).
   - Each family owns a fixed slot range. An image keeps its slot while it stays in the family, so a still image costs no writes.
   - One frame table covers all loaded atlases, and its entries carry the atlas index. The fragment shaders choose among six atlas samplers that the render script binds by name.
6. **Depth range.** One constant, `presentation/backend/world_depth.lua`, which the render script passes to the shaders as a uniform.
   - Clip z = (rank + 1) / 8192. This lies inside both the GL [−1, 1] and the Vulkan/Metal [0, 1] clip ranges and increases with the rank in both.
   - WebGL gives a 16-bit depth buffer. With 65,536 ranks, neighbouring ranks fell into one depth level and a shadow failed to darken the image just before it. With 8,192 ranks there are 4 levels per rank, above the 5,220 image records.

7. **Lookup rows and precision.**
   - Every blend bank spans 256 rows. The decoded PCX rows follow the native allocation's leading row, and identity rows (keep the destination) pad the rest. The original reads unrelated heap memory past the decoded rows (`red` and `green` have one); the padding keeps every row a source byte or fire parameter can select inside its bank.
   - The atlas coordinates and the table row reach the fragment shaders as `highp` varyings: fp16 `mediump` cannot address single texels of a 4096-wide atlas.
   - The shaders sample six atlases; `frame_table` refuses more instead of dropping images.

## Verification

The HTML5 autotest logs the index target CRC and the draw list of the top-left view of all 64 missions, at creation and after 48 ticks. `tools/viewer_compose_check.py` draws the same views on the CPU from the retail GRPs, tilesets and PCX tables, applying the kernels of the original blitters in sequence.

All **128 views are equal**: 2,369 drawn images at creation, of which 1,219 normal, 1,148 shadows and 2 blends. Destination images reached up to 11 layers.

`tests/lua/test_draw_list.lua` checks the draw list without the engine: order, bounds refresh, layers, skipped images, interpolation identity. `tests/lua/test_slots.lua` checks the slot allocator.

The draw list itself comes from the simulation. Its IScript state is checked against the original code ([phase 2](../phase2.md)), and the depth key follows `0x4160b0`.

## Consequences

- **Cost.** One full-target copy per destination layer per frame. Dense bases reach 6–11 layers because the layers are assigned over the whole map.
  - TODO: assign layers only among the images meeting the view (plus a margin), which bounds the copies by what is on screen.
  - The presentation sync of about 1,400 images takes up to 1.9 ms per tick on macOS. Mobile measurements are still missing (deferred, as in phase 0).
- **Rank churn.** Ranks shift when sprites reorder or images appear, which costs one float write per shifted image. Gap-based rank labels are the optimisation if the per-tick write count becomes the bottleneck.
- **Methods still to add:**
  - 2–8 (distortion windows and the threshold pass);
  - 11 (health bars);
  - 12 (warp texture);
  - 13 (selection colours, needs the tselect rows);
  - 15 (size outline).
- **Player colours.** The viewer maps owner p to tunit row p. Missions with a `COLR` section remap players to colour slots; that belongs to the player setup of phase 3.
- **Legacy batches.** The phase-0 batches (`mesh_texture`, `indexed_vt.material`) stay for spikes 04, 05, 13 and 15. The product render script still draws their `kp_world` tag without depth, after the terrain.
