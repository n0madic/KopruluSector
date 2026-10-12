# Spike 05: exact frame selection in `sprite_go`

Plan: phase 0 item 5. Code: `spikes/05-cursor/`.

## Method

`make_frames.py` builds four flipbooks with 17, 68, 229 and 391 frames (common GRP frame counts). Frame k of each is a solid block of colour index `(k % 255) + 1`.

The spike creates one sprite per frame of every flipbook, 705 sprites in total, each with playback rate 0. It sets `cursor` with four candidate formulas.

On HTML5 it reads every sprite's pixel back from the index render target (`spikes/common/readback.lua`) and compares it with the expected frame. The formulas run through the same engine code on every platform; macOS and the Android emulator ran without errors, but pixels are read back only on HTML5.

## Results (HTML5, WebGL 2)

| `cursor` for frame k of n | Wrong frames out of 705 |
|---|---:|
| `(k + 0.5) / n` | **0** |
| `k / n` | 25 (float rounding at frame boundaries) |
| `(k + 1) / n − 1e-6` | 0 |
| `k / (n − 1)` | 0 |

## Conclusion

Use `cursor = (k + 0.5) / n`. It sits in the middle of the frame's interval, so it is exact with a margin of `0.5/n` against float rounding. `k / n` is not exact.

Frame selection happens in the engine's C++ animation code. That code is the same on every platform, so a margin of half a frame is independent of the GPU.

The chosen world backend (`mesh_texture`, [ADR 0002](../adr/0002-presentation-backend.md)) selects frames from a frame table and does not use `cursor`. This result applies to `sprite_go` objects (UI and small sets).
