# ADR 0006: World composition by depth rank

Status: accepted (phase 2, 2026-10-12). The design was approved by the user before implementation. Evidence: the viewer's composition check ([viewer](../../viewer/README.md), `tools/viewer_compose_check.py`) and the comparison with the original renderer's frames (`tools/compose_capture.py`, [phase 2](../phase2.md#acceptance-item-7)).

Superseded in part by [ADR 0012](0012-sdl3-runtime.md) (phase S, 2026-10-25): the restore pass, the rank in the index target's G/B and the 1024-command budget existed for Defold's Vulkan backend and command limit; stage S8 removed them under the same oracles (2026-10-26): the index target is R8 with a 24-bit depth buffer holding the ranks across the copies, every distortion that needs a copy gets one and every destination layer is drawn (no degraded plans). The composition's rules (ranks, windows, destination layers, the copies' reach) stay.

## Context

The original draws sprites in ascending native depth key (D13a). Within a sprite it draws images from the tail to the head, each through its draw method. Some methods read the pixel already in the buffer: shadow (10), the blend tables (9), the cloak blends (5–7), fire (17), and the cloak and EMP distortions (2–4, 8).

Before this decision, [ADR 0002](0002-presentation-backend.md) used `mesh_texture` with one mesh per atlas, with the slots of a mesh in depth order. That ordered images within an atlas but not across the six atlases of a mission. It also drew all destination images as one run after the rest.

## Decision

1. **Rank.** Every tick `presentation/draw_list.lua` builds the native draw list.
   - It reads the image bounds, which the simulation refreshes for every row at the end of its tick (`World:refresh_bounds`, `RefreshRowSpriteBounds 0x4165d0` → `0x40ee70`). The original's draw callback refreshes the rows from 4 above the view's top to the map's bottom before each redraw (`0x445de0`). Refreshing every row once per tick gives the same bounds for every drawn sprite and keeps the simulation's records independent of the camera; the draw list writes nothing back.
   - It takes the sprites visible to the local player (vision byte `0x672668`, `0x416a00`), ordered by `GetSpriteDepthKey 0x4160b0`. Equal keys are ordered by record: the native key ORs the sprite index into its low 9 bits.
   - It takes each sprite's images from the tail to the head, skipping hidden ones.
   - Each image gets its position in this list, its **rank**. The vertex shader turns the rank into depth.
2. **Families.** The draw method picks a material family and a kind inside it (`draw_list.METHODS`):

   | Family | Methods | Pass |
   |---|---|---|
   | overwrite | 0/1, 13 (tselect rows), 14, 16; 12 warp texture; 15 size outline; 11 health bar | depth test GREATER, depth writes on |
   | destination | 9, 10, 17 (tables); 6 trans50; 5/7 trans50 blend then the threshold pass | after the window's overwrite pass, test GREATER, no depth writes, in layers |
   | distortion | 3 (by brightness), 8 (by raw index), 2/4 (then the threshold pass) | reads a copy of the target, writes depth |

   Whatever mesh an overwriting image is in, the pixel ends up with the image that is last in the native order.

   trans50 is stateful: when DrawSprite meets a sprite whose main image draws with method 5..7, `0x416bd0` overwrites trans50 rows 8..15 with the rows of the owner's player colours, and they stay so for the images drawn after it, in later frames too. The draw list carries that owner, and trans50 is read at the source mapped through its colour map. Sprites more than 4 rows above the view's top row are not drawn at all (`0x445de0`), so they do not change it either.
3. **Destination layers.** A destination image changes a pixel only where no later image has overwritten it, which is the sequential result.
   - Its layer is one more than the highest layer of the earlier destination images of its window whose rectangle meets it.
   - Before each layer the index target is copied, and the layer reads that copy.
   - Only destination images within `LAYER_MARGIN` (128 px) of the view get a layer; the others are not drawn. The presentation replans when the view leaves that margin, and on any camera move while a distortion reads pixels within that margin (`distortions_near`).
4. **Distortion windows.** A distortion copies the target pixel `d` bytes ahead (behind when mirrored) in row-major order, wrapping at the end of the target, the analogue of the original's 640×480 linear wrap (`0x4063b5`, `0x406533`). The original reads the buffer it writes, but always at addresses its traversal has not written yet, so a copy gives the same pixels.
   - Everything ranked before a distortion is composed (overwrite pass and destination layers), then the target is copied, then the distortion reads the copy: a composition **window** ends at each copy.
   - A later distortion reads the same copy when no image ranked after the copy meets the pixels it reads (its rows, `d` up to 8 by brightness or 255 by raw index beyond its side) and those stay inside the view. Otherwise it opens a window of its own.
5. **Shared data.**
   - One mesh per family, all reading **one** RGBA32F image-data texture: one upload per frame (spike 15). Each family owns a fixed slot range; an image keeps its slot while it stays in the family.
   - One frame table covers all loaded atlases, sampled through six atlas samplers bound by name.
   - Virtual frames (health bars, boxes) have no atlas frame; their size travels in the image data.
   - Health bars are drawn on the CPU by `presentation/health_bar.lua` (checked against `0x464d20`) into slots of a small texture (`presentation/bars.lua`) uploaded with the image data; the overwrite material copies them at their rank.
   - A slot not written in a tick is freed with the zero-size frame and layer 0, so a freed virtual slot no longer carries a frame size. The overwrite family has a slot for every image record; when the destination (3,000) or distortion (1,000) slots run out, the remaining images of that family are left out and counted (`presentation/images.lua` `overflow`, reported by the viewer).
   - The three fragment programs share the atlas samplers and lookups through `render/materials/world_common.glsl` (`#include`).
   - The composition plan (windows and layers) travels with the uploaded image data (`presentation/backend/world_plan.lua`, read by the render script).
6. **Depth range.** One constant, `presentation/backend/world_depth.lua`, which the render script passes to the shaders as a uniform.
   - Clip z = (rank + 1) / 8192. This lies inside both the GL [−1, 1] and the Vulkan/Metal [0, 1] clip ranges and increases with the rank in both.
   - WebGL gives a 16-bit depth buffer. With 65,536 ranks, neighbouring ranks fell into one depth level and a shadow failed to darken the image just before it. With 8,192 ranks there are 4 levels per rank, above the 5,220 image records.
7. **Lookup rows and precision** (`presentation/world_lookups.lua`).
   - Every blend bank spans 256 rows. The decoded PCX rows follow the native allocation's leading row, and identity rows (keep the destination) pad the rest. The original reads unrelated heap memory past the decoded rows; the padding keeps every row a source byte or fire parameter can select inside its bank.
   - Remap rows: identity, player colours, shift, the three tselect rows, and the brightness of every palette index (`0x445f30`) for the cloak thresholds.
   - The atlas coordinates and the table row reach the fragment shaders as `highp` varyings: fp16 `mediump` cannot address single texels of a 4096-wide atlas.
7a. **Target switches on Vulkan** (added 2026-10-13). Defold's Vulkan backend (1.13.2, still so in 1.14.0) does not keep a render target's contents when the target is bound again. Render targets made in Lua start their colour with `DONT_CARE` (`render_script.cpp`), and depth is never loaded (`graphics_vulkan.cpp`). So on Apple GPUs the tiles drawn after a copy lost their colour, and depth was undefined: units vanished or showed as black tile-sized blocks. WebGL keeps the contents, which is why the HTML5 checks never saw it. Since then:
   - every copy is followed by a restore pass (`render/materials/restore.fp`) that writes the copy back and resets depth to the cleared value;
   - the depth the passes after a copy need goes with the colour: G and B of the index target hold rank + 1 of the image that last wrote depth there (`world_common.glsl`; terrain and clear 0);
   - destination layers and distortions compare their own rank against the copy in the shader, where they used to rely on the depth buffer. A destination image keeps the rank under it, because it writes no depth in the original order.
   - Depth writes stay on through the composition: a destination layer's images do not overlap, and every later image has a higher rank.

   The copy costs 8 commands with the restore; a window costs 10 and a layer 9.
8. **Command budget.** Defold 1.13's render script records at most 1024 commands per frame (`engine.cpp`, not configurable); a full buffer raises and drops the rest of the frame. Copies therefore change only the render target (the copy material ignores view and projection).
   - The draw list builds only plans that fit: `world_plan.commands` counts a plan's windows and layers with the command counts of the render script's `draw_images`, and `PLAN_BUDGET` is what the frame leaves for them.
   - Past the budget a distortion that would open a window reads the last copy, and a destination image that would add a layer takes the window's top layer. Both are counted in `state.degraded`; the viewer reports when a plan becomes degraded and logs the count in its composition check.
   - The render script draws every plan as it is. The plan it draws is the plan the draw list built, logged and checked.

## Verification

- **GPU against the original kernels.** The HTML5 autotest logs the index target CRC and the draw list of the top-left view of all 64 missions: at creation, after 48 ticks, and after the draw effects (`viewer/effect_scene.lua`: cloak, decloak, cloak blends, EMP, warp texture, outline, selection circles, health bars) at 12 and 40 more ticks; plus twelve images of the animation browser. `tools/viewer_compose_check.py` draws the same views on the CPU from the retail GRPs, tilesets and PCX tables, applying the original kernels in sequence (distortions in place, in the original's traversal order). All 268 views are equal.
- **Vulkan.** On macOS (Vulkan through MoltenVK) the same autotest reads the index target from the window. `viewer/autotest_native.settings` runs an engine with the project's native extensions. The palette pass draws an identity grey palette without GUI, and `native/kp_capture` returns the texels. The terrain, sprite and composition checks agree with the CPU there as well (`KOPRULU_VIEWER=1 KOPRULU_NATIVE_ENGINE=1 tools/ci.sh`).
- **Draw list against the original renderer.** `tools/compose_capture.py` rebuilds the original's own frames, captured by the full-game emulator right after `DrawSortedSprites 0x415b30`, from the captured image and sprite records through `draw_list.lua` and the same kernels over the original's terrain, and compares every pixel of the 640×400 viewport ([phase 2](../phase2.md#acceptance-item-7)).
- `tests/lua/test_draw_list.lua` checks the draw list without the engine: order, bounds, layers, windows and copy sharing, the command budget, replans, skipped images, interpolation identity; `tests/lua/test_slots.lua` the slot allocator; `tests/lua/test_world_images.lua` the backend's slot writes, freed slots, full families and uploads (Defold calls stubbed); `tests/lua/test_health_bar.lua` the bars against the original code.

## Consequences

- **Cost.** One full-target copy per destination layer and per distortion window per frame. Layers count only images near the view. A cloaked unit's shadow lies in the pixels its body reads, so each cloaked unit takes a window (23 at most in the autotest).
  - The presentation sync of about 1,400 images takes up to 2 ms per tick on macOS. Mobile measurements are still missing (deferred, as in phase 0).
- **Rank churn.** Ranks shift when sprites reorder or images appear, which costs one float write per shifted image. Gap-based rank labels are the optimisation if the per-tick write count becomes the bottleneck.
- **View edges.** The original clips images to its 640×400 viewport and wraps distortions in its 640×480 bitmap. Here the view is the target: a distortion at the view's left or right edge reads the opposite edge of the next row, and a health bar partly outside the view is drawn in part (the original drops a bar whose rows are clipped). While the camera moves between ticks, a distortion near an edge may read a copy planned for the previous view until the replan.
- **Player colours.** The viewer maps owner p to tunit row p. Missions with a `COLR` section remap players to colour slots; that belongs to the player setup of phase 3.
- **Legacy batches.** The phase-0 batches (the spikes' own `mesh_texture` in `spikes/04-backend/backends.lua`, `indexed_vt.material`) stay for spikes 04, 05, 13 and 15; the product module `presentation/backend/mesh_texture.lua` was removed, its atlas frame reader moved to `world_images.lua`. The product render script still draws their `kp_world` tag without depth, after the terrain.
