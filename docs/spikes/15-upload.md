# Spike 15: texture upload cost

Added after phase 0, when the first physical device became available.

Why: spike 09 measured 8.1 ms per 256×1 palette upload on a Galaxy Z Fold7 (Adreno 830). Spike 04 measured 1.7 ms for its larger RGBA32F data texture.

Code: `spikes/15-upload/`. It uses the product render pipeline with 60 indexed images. Run it with `tools/spike.sh spikes/15-upload macos|web|android`.

The variant table below is from the first version of the spike, when the palette was still a texture bound by the render pipeline.

## Method

Each variant runs 240 measured frames after 30 warm-up frames, with vsync off. Only the `resource.set_texture` call is timed; buffers are prefilled. The variants:

| Variant | Texture |
|---|---|
| `palette_bound_every_frame` | the palette sampled by the palette pass |
| `palette_bound_every_8` | the same, every 8th frame |
| `palette_spare_every_frame` | a 256×1 RGBA8 texture that no draw uses |
| `palette_swap_every_frame` | two palettes: write the one the previous frame did not use, then rebind it |
| `data_spare_every_frame` | an unused RGBA32F 1024×4 texture, the size class of the `mesh_texture` data |

## Results (`set_texture` mean / p95 / max)

| Variant | macOS M4 Max (Vulkan via MoltenVK) | Galaxy Z Fold7 (Adreno 830, Vulkan) |
|---|---|---|
| palette, bound, every frame | 0.44 / 0.60 / 1.45 ms | 1.67 / 5.51 / 6.14 ms |
| palette, bound, every 8th frame | 0.37 / 0.60 / 0.61 ms | 1.65 / 5.60 / 6.23 ms |
| palette, unused texture | 0.48 / 0.88 / 1.50 ms | 2.50 / 5.85 / 6.59 ms |
| palette, double-buffered | 0.47 / 0.61 / 1.15 ms | 2.82 / 5.52 / 6.01 ms |
| RGBA32F 1024×4, unused | 0.52 / 0.61 / 0.92 ms | 3.34 / 5.57 / 5.94 ms |

The cost is the same whether or not a draw samples the texture. It does not drop with double buffering, and it barely depends on size (1 KB vs 64 KB). It is a **fixed per-call cost**, not a read-after-write hazard or a bandwidth cost.

The source of Defold 1.13.2 explains it. `VulkanSetTextureInternal` → `CopyToTexture` (`engine/graphics/src/vulkan/graphics_vulkan.cpp`) does the following on every call:
- creates a new host-visible staging buffer with its own memory allocation;
- allocates a one-off command buffer;
- records layout transitions and the copy;
- submits it with a new fence.

The staging buffer is released later through the fence. On the Adreno driver these allocations and the submit cost milliseconds.

With vsync on, spike 09 saw 8.1 ms per upload. That is about one 120 Hz display interval, so the call can also block on frame pacing.

## Palette as a render constant

Final form of the spike: the product palette path, `kp_palette` to the render script with the constant array `palette_colors[256]`, against texture uploads. Each case runs every frame.

| Case, per frame | HTML5 | Galaxy Z Fold7 |
|---|---|---|
| 256×1 palette texture `set_texture` | ~0.05 ms | 1.18 ms mean, p95 2.17 |
| RGBA32F 1024×4 `set_texture` | ~0.05 ms | 2.14 ms mean, p95 5.28 |
| `kp_palette` post (Lua) + constant update (render script) | 0.03 + 0.09 ms | **0.014 + 0.084 ms** |

**Byte check (HTML5, WebGL 2).** The palette pass is read back from the RGB target and the index target. Every pixel must equal `palette[index]`.
- Two random palettes were checked: one set before the run, one sent a few frames before the check.
- Each check covered 853×480 pixels and all 256 indices; images use 16 remap rows that spread the indices.
- Result: 0 mismatches for both palettes. If the update were lost, the second palette would mismatch almost everywhere, so the check also covers the update path.

## Consequences

- **At most one texture upload per rendered frame** for the whole presentation. Per-frame data that would otherwise need a second texture goes into the same data texture ([ADR 0002](../adr/0002-presentation-backend.md)).
- **The palette is a render constant** (`palette_colors[256]`, implemented in `render/`), so palette cycling needs no upload ([ADR 0005](../adr/0005-render-pipeline-clock-data.md)).
- Static lookup textures (remap rows, blend tables) are uploaded once per mission. Remap rows that change during play must be batched into one upload per frame.

## Not measured

- The OpenGL ES backend: Defold picks Vulkan on this phone.
- iOS (Metal through MoltenVK has the same code path).
- A mid-range Android device.
- An engine-side fix, such as a persistent staging ring. It would need a native extension or an upstream change.
