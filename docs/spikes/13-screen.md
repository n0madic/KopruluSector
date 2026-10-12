# Spike 13: adaptive screen

Plan: phase 0 item 13, D20, §2.1. Product code: `render/koprulu.render_script`. Spike: `spikes/13-screen/`.

## Pipeline under test

World images are drawn into an RGBA8 index target. A palette pass then turns it into colour, and the result is scaled to the window in one of two modes:
- **integer**: nearest at an integer factor `s = round(window_h / visible_height)`. The world target grows with the window, `ceil(window / s)`.
- **sharp**: a fixed visible height (480) is rendered, then upscaled with a sharp-bilinear pass. Nearest inside each texel, a 1-output-pixel blend across texel edges.

The GUI draws on top in window pixels. The index-target size follows the window on every resize (`render.set_render_target_size`).

## Results

**macOS.** `window.set_size` was cycled through 1280×720, 1920×1080, 720×1280 (portrait), 1000×1000, 2560×1080 and 1366×768, in both modes.
- Every resize arrived as `WINDOW_EVENT_RESIZED`.
- The render target was resized; for example, sharp mode at 2560×1080 gives a 1506×480 world at scale 3.4.
- The bottom-anchored panel and the top-right panel kept their anchors in every size.
- A capture of integer mode (`build/spikes/13-screen/shot_integer_crop.png`) shows the correct palette, player-colour remap per sprite, flips and frame selection.

**Android emulator.**
- The rotations portrait → landscape → portrait → landscape arrived as resize events while running.
- Layout recomputed from 1080×2400 (integer: world 216×480 at ×5) to 2400×1080 (sharp: world 1067×480 at ×2.25).
- Anchors held.

**HTML5 (WebGL 2).** Both modes ran at the 1050×591 canvas.

| Frame time | macOS (120 Hz) | HTML5 (60 Hz) | Android emu |
|---|---|---|---|
| integer | 8.33 ms | 16.67 ms | 8.3–9.2 ms |
| sharp | 8.33 ms | 16.67 ms | 9.4–10.9 ms |

On the Galaxy Z Fold7 (Snapdragon 8 Elite, Adreno 830, Android 16, Vulkan), portrait 1080×2520, vsync off: integer mode (world 216×504 at ×5) 0.48–1.14 ms per frame, sharp mode (world 206×480 at ×5.25) 2.22–2.31 ms. The sharp-bilinear pass at full panel resolution costs about 1.5 ms of GPU-bound frame time there. Rotation was not exercised on the phone.

Desktop and browser are vsync-bound; the difference between the modes is one extra full-screen pass and one render target, invisible at these frame rates.

**Quality at fractional scales** (analytic, the same for any GPU): nearest sampling makes source pixels unequal.

| Scale | Pixel widths with nearest | Unevenness |
|---|---|---|
| ×1.23 | 1 or 2 px | 2:1 |
| ×2.25 | 2 or 3 px | 1.5:1 |
| ×3.4 | 3 or 4 px | 1.33:1 |

This shimmers when the camera scrolls. Sharp bilinear keeps every source pixel the same size, with a one-pixel soft edge.

## Decisions

- Both modes stay in the render script: `kp_scale_mode {mode, visible_height}`.
- **Default.**
  - **integer** when `window_h / 480 ≥ 2` (desktop and most phones in landscape), so no blur.
  - **sharp** for smaller or fractional cases (browser windows, small screens).
- **Policy left to the product (§12 question 3).** Whether the visible height is fixed (the original's 480 including the console) or the visible area is limited on wide screens. The pipeline supports both.
- **Engine findings.**
  - `window.set_size` works in points, and the render target sees physical pixels (Retina ×2).
  - The display caps the window height (1632 px here).
  - A render script can answer a GO's message with `msg.post(sender, …)`; this is how the spike read its statistics.

## Not measured

- iOS and a mid-range Android device; rotation on the phone.
- GPU cost of the upscale pass on mobile.
- Split-screen modes.
- The sharp-mode window capture: the macOS display slept and captures failed for the rest of the session.
