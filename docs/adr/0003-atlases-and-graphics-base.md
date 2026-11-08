# ADR 0003: Indexed atlases and the minimal graphics base

Status:
- Atlas format: accepted (phase 0, 2026-10-11).
- Minimal graphics base: accepted for implementation (phase 0, 2026-10-12) on the evidence of spikes 04 and 06. It answers plan §12.1, which the user may still override.

Superseded in part by [ADR 0012](0012-sdl3-runtime.md) (phase S, 2026-10-25): bob's atlas packing (single pages, rotated frames, bottom-up UVs) and the texture profiles give way to the converter's own packer and `PACK` containers; the L8 index atlases and the GLES3/WebGL2 base stay.

Evidence:
- Spike 07 (atlases on Vulkan, WebGL 2, forced WebGL 1 and the Android emulator).
- Spike 08 (page sizes of whole race packs).
- Spike 09 (index render target and palette pass on WebGL 2 and WebGL 1).

## Decision: atlases

- **Encoding.** A GRP frame becomes a single-channel index image (texture format `LUMINANCE`, uncompressed, no mipmaps, no premultiplied alpha, nearest filtering). Index 0 means transparent.
  The converter decodes RLE into index plus opacity (`converter/formats/grp.py`). It accepts this encoding only when no frame draws an opaque index 0 (`OpaqueZeroError`).
  Stock 1.17 data satisfies this for all 833 `images.dat` GRPs ([evidence](../research/evidence/grp-opaque-zero.json)). The rendering is therefore exact, and plan D10's "index 0 may be opaque" stays representable.
- **Fallback for opaque index 0.** If a pack contains such a frame, the converter emits a second L8 atlas with the opacity mask, packed from same-size images. The material binds it as a second sampler.
  Identical packing was verified on every backend. Exactness was verified on Vulkan, WebGL 2 and WebGL 1.
- **Not used:** `LUMINANCE_ALPHA`. On Vulkan the alpha channel is not the mask, and on both Vulkan and WebGL the stored luminance values change.
- **Frame images** are the GRP frame rectangles. Offsets and canvas sizes go to `grpmeta.kpd`.
  Presentation places a frame so that its top-left is on a whole pixel (half-pixel center for odd sizes). Full-canvas frames with trimming cost 6× the texture memory and are not used.
- **One page per pack, at most 4096².** Every race fits one page: Terran 2048², Protoss 4096×2048, Zerg 4096². A pack that outgrows 4096² is split into more atlases, not pages.
  Paged atlases (texture arrays) stay allowed by D11 but are not used.
  - They work on Vulkan and WebGL 2.
  - On WebGL 1 they load as plain 2D textures and render garbage without any error.
  - They save nothing here.

## Decision: minimal graphics base = GLES 3 / WebGL 2 (and Vulkan/Metal)

The atlas decision above does not depend on the base: single-page L8 atlases and the RGBA8 index target with a palette pass are pixel-exact on WebGL 1 too. The base therefore only matters for:

- **D10 destination effects** (shadow, blend tables, cloak/EMP distortion). These need a readable copy of the index target. Single-channel (R8) render targets, `texelFetch` and integer-exact sampling are GLES 3 / WebGL 2 features. On GLES 2 / WebGL 1 they would need RGBA8 targets and ES 1.00 shader variants.
  Spike 06 implemented them with RGBA8 targets and `texelFetch` and matched the original byte for byte (8,117 blitter and 720 effect cases on WebGL 2).
- **The world-image backend.** [ADR 0002](0002-presentation-backend.md) chose `mesh_texture`, which needs vertex-texture fetch and RGBA32F textures. GLES 2 / WebGL 1 do not guarantee either.
- **One shader dialect.** `game.project` already sets `[shader] exclude_gles_sm100 = 1`, so bob emits no GLSL ES 1.00 variants. The phase-0 spikes override this to probe WebGL 1.
  Without ES 1.00 variants a WebGL 1 context cannot create any material ("Unable to get a valid shader from a ShaderDesc"). That is a hard failure, not a degradation.
- **Coverage.** WebGL 2 is available in all current desktop and mobile browsers, including Safari 15+. GLES 3 is available on practically all Android devices still in use.
  Defold uses Vulkan or Metal where available (the Android emulator also selected Vulkan).

Decision: the minimal base is **GLES 3 / WebGL 2**, keep `exclude_gles_sm100 = 1`, and keep atlases WebGL 1-compatible anyway (single page, L8), since that costs nothing.
If the user requires GLES 2 / WebGL 1:
- turn the ES 1.00 variants back on;
- keep D10 on RGBA8 targets;
- re-evaluate spike 06 effects on that base.

## Consequences

- Converter: `converter/emit/atlas.py` writes INDEX/MASK frame PNGs, `.atlas` files and texture profiles. Pack grouping follows ADR 0004.
- Runtime: sprite/mesh materials sample `.r` only. A mask sampler is added only for packs that need opaque index 0.
- Not verified here: iOS, real Android GPUs, the GLES backend on Android (the emulator ran Vulkan), desktop OpenGL.
