# ADR 0009: Fog of war as the original draws it, and its wider views

Status: accepted (phase 8, 2026-10-18). The user chose the original's fog drawn pixel for pixel (inline design document, 2026-10-18). Evidence: `tools/fog_check.py` against the full-game emulator's fog captures (`tools/research/sc_emulator/fogcap.py`), and the viewer's play check, which reads the view back with and without the fog and compares it with the CPU reference.

Superseded in part by [ADR 0012](0012-sdl3-runtime.md) (phase S, 2026-10-25): the fog's Defold texture uploads and the minimap chunks give way to `kp_gfx`; the fog's drawing stays the original's.

## Context

Plan phase 8 item 5 asks for the fog of war of the original: smoothed edges, the explored-but-unseen darkening, the minimap's fog, and updates only where something changed. The original builds it in `mask.cpp` (atlas P23 `0x46b910`–`0x46c050`, P24 `0x46c1e0`–`0x46c560`) for its fixed 640×400 game view:

- `SampleAndSmoothFogGrid 0x46b9e0` turns a 24 × 17 grid of tiles, from the tile left of and above the camera's, into levels (0 unexplored, 15 explored, 31 seen) and blurs them (1 2 1 / 2 4 2 / 1 2 1, ÷ 16);
- `BuildFogLevelMap 0x46b910` expands 21 × 14 of the blurred tiles into an 88 × 60 map of one level per 8 × 8-pixel block;
- `DrawFogOverlay 0x46c3c0` darkens each block of the view through the tileset's `dark.pcx` row of its level, interpolating between the block's corner levels per pixel;
- the minimap composes its fog from the tile flags on a one-second timer (`0x46cd10`, `0x46cdc0`, `0x46cea0`).

Koprulu's view is not fixed: the adaptive screen (plan §2.1) shows any number of game pixels, and the camera may stop at any pixel.

## Decision

1. **Where it lives.** The fog is presentation state, outside the game and its saves. `native/kp_core/src/core/kp_fog_view.c` holds the original's grids and level map for one view (`KpFogView`) and builds them from the game's tile flags each frame; `kp_core.game.fog_view()` exposes it to Lua. The CPU darkening `kp_fog_view_shade` is the reference of the shader.
2. **Drawing.** `presentation/fog.lua` keeps the level map in a texture (one texel per block, the level in R; only the changed blocks are uploaded again) and the 32 `dark.pcx` rows in another. The render script's fog pass (`render/materials/fog.fp`) copies the composed index target and writes the darkened index back, after the world images and before the palette pass, as the original darkens its view after the sprites and before its interface.
3. **640 × 400.** With a 640 × 400 view and a camera on the 8-pixel grid, the grids, the level map and every pixel are the original's (checked by `tools/fog_check.py`).
4. **Wider views (approximation).** A view of W × H game pixels uses grids of ⌈W/32⌉ + 4 by ⌈H/32⌉ + 4 tiles and a level map of (columns − 2)·4 by (rows − 2)·4 blocks; the tiles outside the map are clamped to its edge as the original's sampling pointer stops at an edge. The blocks stay on the map's 8-pixel grid, so a camera between grid positions shifts the fog with the map instead of with the screen. Nothing of the original covers these cases; they keep its rules where they apply.
5. **Minimap.** `kp_fog_minimap` composes the minimap's fog by the original's rules (the three map-size cases, the unexplored colour and `dark.pcx` row 15), checked against the original's composed minimap image.

## Consequences

- The fog is exact for the original's view and an approximation (plan §8 `approximation`) for wider ones; a narrower view is a part of the wider rule.
- A frame costs one copy and one full-screen pass of the index target for the fog, plus the level-map update in C.
- The fog is presentation: saves do not hold it, and it is rebuilt from the game's tiles on the next frame after a load.
- The original refreshes the minimap's fog once a second; Koprulu composes it when the presentation asks for it (the timer is the UI's, phase 9).
