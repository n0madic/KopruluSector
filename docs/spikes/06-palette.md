# Spike 06: palette composition (D10), byte-checked against the original

Plan: phase 0 item 6, D10.

Code:
- `spikes/06-palette/`: blitter set (`prepare.py`, `case.fp`, `verify.*`) and effect shader (`effects.*`);
- `spikes/06-effects/`: effect set driver;
- `spikes/06-palette/native_check.py`: native-platform capture.

Product shaders built from the result live in `render/materials/`.

## Method

`prepare.py` and `prepare_effects.py` rebuild the exact inputs of every drawing case of the original-code probes:
- [pixel blitters](../research/native-pixel-blitters.md): 8,117 cases;
- [pixel effects](../research/native-pixel-effects.md): 720 cases.

The inputs are frame, crop, mirror, destination, lookup rows, threshold and baseline buffer. The scripts read the retail GRP/PCX/WPE resources through the profile resolver and check them against the evidence manifests. They keep the SHA-256 of the whole guarded result buffer that the **original code produced**.

`--cpu-check` recomputes every case with the kernel semantics the shaders implement. It matches **all 8,117 and all 720 original hashes**. A deliberately corrupted model is rejected (1,848 differences), so the check has teeth.

The GPU runs every case in one tile of a 2560×2560 RGBA8 index target:
- blitter set: 8×8 tiles of 320×320;
- effect set: 4×5 tiles of 640×480.

The passes per frame are:
1. The **baseline** shader writes the probe fixture.
2. A **copy** pass snapshots the target as the destination.
3. One mesh draws each case's stages with **`texelFetch`** lookups.

The kernels, all writing the index into R:

| Kernel | Original | Shader |
|---|---|---|
| Foreground through the map: identity, player colours 8..15, selection 1..8, shift | `0x405e1e`, `0x40e950`, `0x40e900`, `0x40ea60` | `out = remap[row][src]` |
| Source/background blend: ofire, gfire, bfire, bexpl, trans50, red, green, each with its leading row | `0x4067f6` | `out = table[bank + src][dst]` |
| Shadow (dark row 18) and fire background remap (ofire bank) | `0x406950`, `0x40eb20` | `out = table[row][dst]` |
| Cloak/EMP distortion: brightness or raw displacement, in place, wrap at `0x4b000` | `0x4063b5`, `0x406533` | sample `dst[(d ± disp) mod 0x4b000]`, following the chain through pixels of the same image processed earlier |
| Brightness threshold, signed compare (methods 2/4/5/7) | `0x406235` | `m = map[src]`, write when `sgn(bright[m]) > sgn(arg)` |
| Warp texture (method 12) | `0x40e800` | where the main image is opaque: texture frame pixel at the same crop position (column reversed when mirrored), scratch byte `0xA5` under transparent texture pixels |
| Size outline (method 15) | `0x40e9d0` | colour on the crop's retained frame edges |
| Transparent packets | all | `discard` |

On HTML5 the index target is read back through Emscripten's GL tables. Each tile is cut out in original top-down order between its two 16-byte `0xE7` guards and hashed with WebCrypto.

## Results

| Set | Platform | Result |
|---|---|---|
| Blitters, 8,117 cases (all recorded GRP frames in both orientations, crops, authored RLE boundaries and opaque index 0, player/selection/shift maps, shadow, fire, every stock blend row against every background) | HTML5, WebGL 2, headless Chrome | **8,117 / 8,117 byte-identical** to the original. Drawing time 2.9 s for 127 batches. |
| Effects, 720 cases (authored packet/crop/threshold cases, 640×480 wrap, Ghost/Phoenix/Zealot frames with cloak, detected and EMP stages, all 21 warp-texture frames in 3 crops and both orientations, size outline crops and colours) | HTML5, WebGL 2 | **720 / 720 byte-identical**. |
| Both sets | Galaxy Z Fold7 (Snapdragon 8 Elite, Adreno 830, Android 16, Vulkan) | Shaders compile and every case draws: 8,117 blitter cases in 1.0 s (127 batches), 720 effect cases in 0.3 s. No byte check (no Lua readback). |
| Both sets | macOS (Vulkan/Metal), Android emulator (Vulkan) | Shaders compile and every case draws: 8,117 blitter cases in 1.2 s on macOS and 2.2 s on Android; the 720 effect cases in 0.4 s and 0.6 s. No byte check: Lua has no pixel readback (see below). |

Negative controls:
- A blend shader reading one row off gives 1,352 mismatches.
- A distortion shader using only the pre-image snapshot (no chain) **also matches 576/576**.

The second control shows that the recorded cases do not separate "snapshot" from "in-place chain". This follows from the geometry. The only pixels already written by the same image are earlier in traversal order. A displaced sample, at most 255 bytes away, reaches such a pixel only when the image is wider than the 640-pixel row minus the displacement, or when it straddles the wrap seam. The product shader keeps the chain because it is exact in those cases too, and it costs one loop iteration when the chain is empty.

## Consequences for the product pipeline

- **Index target, palette pass and destination copy are exact** with RGBA8 targets and `texelFetch`, on GLES 3 / WebGL 2 class APIs.
  - This sets the minimal graphics base; see [ADR 0003](../adr/0003-atlases-and-graphics-base.md).
  - `render/koprulu.render_script` implements the index target → destination copy → table pass → palette pass.
- **Run splitting.** A destination-reading image must see every image drawn before it.
  - Exact composition needs one copy before each *run* of destination images in depth order. Within a run, the images must not overlap each other's footprints and displacement windows.
  - The current render script draws destination images as one run after the others (TODO in the script).
  - The run count per frame of real scenes is to be measured on emulator traces in phase 2. Each copy is one full-screen pass.
- **Viewport width.** The original wraps distortion at the 640×480 screen buffer. With a wider target, "d ± disp" uses the target's own stride. Results equal the original exactly only for a 640-wide viewport; elsewhere this is the documented generalisation.
- **Opaque index 0.** It is supported through a mask channel; the blitter set's authored cases include it. The product atlases use L8 with index 0 transparent ([ADR 0003](../adr/0003-atlases-and-graphics-base.md)), and a mask sampler is the fallback.

## Not covered

- **Method 11 (HP bars)** is a UI wrapper with no buffer-level evidence in this batch; it is covered by the health-bar research in phase 9.
- **Native byte check on macOS.** `native_check.py` captures the window with `screencapture -l` and compares tiles.
  - With the display awake it runs, but 0 of 8 tiles match. The capture is converted to the display colour profile, and the conversion is lossy: the baseline ramp 0, 1, 2, … reads back as 58, 59, 60, 61, 62, 62, 63, ….
  - Byte comparison needs an unconverted readback: engine-side, or ScreenCaptureKit in the window's colour space.
- **Byte check on a mobile GPU.** The phone draws every case, but nothing reads the target back. iOS was not available.
