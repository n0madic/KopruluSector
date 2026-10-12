# Spike 07: indexed GRP atlases

Plan: phase 0 item 7, D10, D11, §12.1. Decision record: [ADR 0003](../adr/0003-atlases-and-graphics-base.md).

## Method

- `converter/formats/grp.py` decodes GRP RLE into palette indices plus an opacity mask.
  Transparency comes from the packet type, as in [native blitters](../research/native-pixel-blitters.md#rle-and-clipping-semantics).
  With `KOPRULU_ORIGINAL` set, `converter/tests/test_formats.py` checks the frame and opaque-pixel counts of every Terran GRP against [grp-opaque-zero.json](../research/evidence/grp-opaque-zero.json).
- `python3 -m converter.emit.spikes --data <app> atlas` writes the frames of all 98 Terran unit GRPs from `images.dat` (3,510 frames) to `generated/spikes/07-atlas/`. Six atlas variants are built from them (`converter/emit/atlas.py`):

  | Variant | Frame image | Encoding | Pages |
  |---|---|---|---|
  | `la_crop` | frame rectangle | `LUMINANCE_ALPHA`: L = index, A = opacity | single |
  | `index_crop` | frame rectangle | `LUMINANCE`: index; 0 means transparent | single |
  | `mask_crop` | frame rectangle | `LUMINANCE`: opacity, used as a second sampler with `index_crop` | single |
  | `index_crop_paged1024`, `mask_crop_paged1024` | frame rectangle | as above | 1024² pages (texture array) |
  | `index_canvas_trim8_paged2048` | full GRP canvas, 8-vertex trim | index | 2048² pages |

  The texture profile is uncompressed, with no mipmaps and no premultiplied alpha. Materials sample with nearest filtering.
  `INDEX` is valid only when no frame draws opaque index 0. The emitter raises `OpaqueZeroError` otherwise. In stock data no GRP does (833 GRPs, 0 such pixels).
- `spikes/07-atlas` draws frame 0 of 8 GRPs from each variant. The probe shaders write the decoded index as gray and discard transparent pixels, over a blue clear color.
  `check_capture.py` searches each expected frame near its position in the capture. A frame passes only if every opaque pixel has gray == index and every transparent pixel shows the clear color.
- Captures:
  - macOS: `capture_macos.sh` (window capture, 2× Retina).
  - Web: `web_capture.py` runs headless Chrome with an injected probe. The probe can hide WebGL 2 to force WebGL 1 and reads the canvas back through `preserveDrawingBuffer`.
  - Android emulator: `adb screencap`, visual check only (the screen is scaled, not pixel-exact).
- `measure_build.sh` measures bob time and archive size.

## Results

Raw logs are in `build/spikes/07-atlas/`:
- `macos-vulkan.{log,png,check.log}`
- `web-webgl2.*`, `web-webgl1.*`
- `android.log`, `android-capture.png`
- `build-measure.log`

**Data volume (Terran).** 98 GRPs, 3,510 frames and 1,701,114 opaque pixels. Frame rectangles hold 3,666,625 px; full canvases hold 21,024,496 px (5.7× more).

**Textures and archive** (bob 1.13.2, Apple M4 Max):

| Atlas | Texture | Pages | `.texturec` | gzip |
|---|---|---|---|---|
| `index_crop` (L8) | 2048×2048 | 1 | 4.19 MB | 1.12 MB |
| `mask_crop` (L8) | 2048×2048 | 1 | 4.19 MB | 0.25 MB |
| `la_crop` (LA8) | 2048×2048 | 1 | 8.39 MB | 1.34 MB |
| `index_crop_paged1024` | 1024×1024 array | 4 | 4.19 MB | 1.03 MB |
| `index_canvas_trim8_paged2048` | 2048×2048 array | 6 | 25.2 MB | 1.02 MB |

- Each `.a.texturesetc` (geometry of 3,510 frames) is about 0.83 MB (gzip 0.20 MB).
- A clean release `--archive` build of all six atlases (21,060 frame PNGs) takes 4.6 s. A no-op rebuild takes 1.3 s, and a rebuild with one atlas touched takes 1.3 s.
- `game.arcd` with all six atlases is 7.24 MB.
- `resource.get_atlas` takes 2–8 ms per atlas.

**Whole-game estimate** (frame rectangles, all 832 distinct `images.dat` GRPs). Megapixels per group:

| Group | Megapixels |
|---|---|
| Terran | 3.67 |
| Protoss | 6.22 |
| Zerg | 11.18 |
| Neutral | 3.12 |
| Thingy | 12.75 |
| Bullet | 0.34 |
| Total | 37.3 (20,598 frames) |

Spike 08 packed each race into one page: Terran 2048², Protoss 4096×2048, Zerg 4096².

**Render exactness** (8 GRPs per row):

| Backend | `index_crop` | `index+mask` | `index+mask` paged | canvas+trim paged | `la_crop` |
|---|---|---|---|---|---|
| macOS Vulkan (MoltenVK, M4 Max) | 8/8 exact | 8/8 | 8/8 | 7/8¹ | fails: alpha ignored, index values changed |
| WebGL 2 (headless Chrome, ANGLE Metal) | 8/8 | 8/8 | 8/8 | 7/8¹ | fails: index values changed |
| WebGL 1 (WebGL 2 hidden) | 8/8 | 8/8 | **fails**: atlas loads as a 2D texture (`type=0`), garbage | **fails** | fails |
| Android emulator (Defold picked Vulkan on SwiftShader) | visually correct | visually correct | visually correct (`type=1`) | visually correct | mask ignored |

¹ The 128×128 tank canvas overlaps the row above, so 5 (web) or 58 (2×) clear-color pixels are covered by a neighbor. Every opaque pixel matches.

Other observations:
- The index and mask atlases packed identically on every backend (`layout_match true`). The second sampler can therefore reuse the first atlas's UVs.
- Odd-sized frames need a half-pixel sprite center so that the frame's top-left lands on a whole pixel. Without it, nearest sampling shifted one column (`terran_phoenix` 47×28 at 1×).
- On WebGL 1 a paged atlas fails silently: no error, wrong pixels.

## Conclusion

1. Encode unit atlases as single-channel index textures (L8), with 0 = transparent, verified per frame by the converter. If a pack ever needs an opaque index 0, fall back to an identically packed L8 mask atlas bound to a second sampler. Both forms are exact on Vulkan, WebGL 2 and WebGL 1.
2. Do not use `LUMINANCE_ALPHA`. Its channel mapping differs between the Vulkan and GL backends, and the luminance values are altered.
3. Use frame rectangles (offsets in `grpmeta`), not canvas+trim: the canvas variant needs 6× the texture memory.
4. Paged atlases work on Vulkan and WebGL 2 but break silently on WebGL 1. They are not needed: each race fits one ≤4096² page.
5. Bob time and archive size are not a risk at this scale: about 5 s for 21k frames, 1–1.4 MB compressed per race atlas.

## Not measured

- iOS (no simulator runtimes, no devices).
- Pixel output on a real Android device. On the Galaxy Z Fold7 (Snapdragon 8 Elite, Adreno 830, Android 16, Vulkan) every variant loads and `resource.get_atlas` returns the same layout as on the emulator (3.0–6.8 ms per atlas); no capture was taken.
- The OpenGL ES backend: Defold picks Vulkan on both the emulator and the phone.
- Desktop OpenGL: `--graphics-adapter` had no effect with the bundled macOS engine.
- GPU memory, measured directly.
- Bob time for all 20,598 frames: only extrapolated from Terran.
