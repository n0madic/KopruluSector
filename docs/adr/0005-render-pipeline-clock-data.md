# ADR 0005: Render pipeline, game clock, data access and saves

Status: accepted (phase 0, 2026-10-12). Evidence:
- [spike 06](../spikes/06-palette.md), [spike 13](../spikes/13-screen.md) for the render pipeline;
- [spike 14](../spikes/14-clock.md) for the clock;
- [spike 10](../spikes/10-data.md) for data access;
- [spike 12](../spikes/12-save.md) for saves.

Superseded in part by [ADR 0012](0012-sdl3-runtime.md) (phase S, 2026-10-25): the render script, the palette as a constant array, Lua's data access rules and the clock's `socket.gettime` give way to `kp_gfx` (the palette a 256×1 RGBA8 texture since stage S8: WebGL2 guarantees only 224 fragment uniform vectors), C data access and SDL's clock; the pipeline's order stays.

## Render pipeline (D10, D20)

`render/koprulu.render_script` runs these steps every frame:

1. **World images → index target.** RGBA8, because Defold has no R8 render-target format; the index is in R.
   - Source remap uses a `remap_table` texture (256 × rows): player colours 8..15, selection 1..8, shift.
   - Atlases are single-channel with index 0 transparent ([ADR 0003](0003-atlases-and-graphics-base.md)).
2. **Destination pass.** The index target is copied, then destination-reading images draw table lookups `tables[row (+src)][dst]`: shadow, fire/blood, trans50, ofire WarpIn and the other blend banks.
   - Distortion (methods 3/8) and the threshold pass follow the kernels proven in spike 06.
   - **Exact interleaving** in depth order needs one copy per run of destination images. Phase 2 replaced the single run by composition by depth rank: destination layers and distortion windows ([ADR 0006](0006-depth-ranked-composition.md)).
3. **Palette pass.** Index → RGB through the `palette_colors[256]` fragment constant array. Callers send `kp_palette {rgb = 768 bytes}`; palette cycling sends a new palette. The palette state (the original's cycles on real time, brightness gamma, grayscale and fades) is `presentation/palette.lua` (phase 2).
   - It is not a texture. On a real Adreno 830 (Vulkan) a texture upload costs 1–3.4 ms per call, independent of size; one palette upload measured 8.1 ms with vsync on. Updating the constant costs 0.014 ms in Lua plus 0.084 ms in the render script ([spike 15](../spikes/15-upload.md)).
   - **Exactness:** on WebGL 2 every pixel of the RGB target equals `palette[index]` of the index target. Two random palettes were checked, 853×480 pixels with all 256 indices, 0 mismatches.
4. **Scaling.**
   - `integer`: nearest at an integer factor, with the world target sized to the window.
   - `sharp`: fixed visible height plus a sharp-bilinear upscale.

   The default is integer when `window_h ≥ 2 × 480`, else sharp. The visible-area policy is a product question (§12.3).
5. **GUI** in window pixels.

All lookups use `texelFetch`. Byte exactness against the original was shown for 8,117 blitter and 720 effect cases on WebGL 2.

## Game clock (D8)

`game/simulation/clock.lua`:
- the speed table `0x503360`;
- `max_catchup = 4`;
- pause/resume on window focus events.

It is driven by measured wall time (`advance_wall(socket.gettime())`). Engine `dt` is not used: Defold reports about 0.01 s after a 1 s stall. Resume resets both the accumulator and the wall reference.

## Data access (D3, D4, §4.3)

- **Byte code and strings:** Lua strings read with `string.byte`.
- **Hot DAT columns:** SoA tables, unpacked once.
- **Large mutable grids:** packed representations chosen in phase 3. Never one Lua table entry per cell: a 1M-entry table costs 16 MB on PUC Lua / HTML5.
- **Buffer streams** are never a Lua access path: about 6× slower than `string.byte` with the JIT, and no faster on interpreters (Android runs LuaJIT with the JIT off).
- **Containers** are loaded with `sys.load_buffer_async`, then the file string is released after section extraction. Resident heap ≈ file size, transient ≈ 2× file size.
- **Custom resources** are compressed by the Defold archive (a structured mission compresses about 3×). The container stays uncompressed.

## Saves (D17)

- Binary files with `io.open` at `sys.get_save_file` paths work on macOS, Android and HTML5, including persistence across launches.
- On HTML5 the writer calls `sys.save` on a small settings table after a save file, so the in-memory file system is flushed to IndexedDB.
- Atomic replace (temp file + rename) is required in the save format work (M0E E4).
- `sys.save` is used for settings and progress only.
