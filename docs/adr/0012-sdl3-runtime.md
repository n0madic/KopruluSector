# ADR 0012: The runtime on SDL3, without Defold and without Lua

Status: accepted (phase S, 2026-10-25). The user approved the plan (inline design document and plan file, 2026-10-25): SDL 3.4.18 as the base, components of the SDL ecosystem where they cover everything the game needs, no Lua anywhere in the project, HTML5 kept as a target. Evidence: [spike 16](../spikes/16-sdl3.md). Supersedes in part [ADR 0002](0002-presentation-backend.md), [ADR 0003](0003-atlases-and-graphics-base.md), [ADR 0004](0004-resource-packs.md), [ADR 0005](0005-render-pipeline-clock-data.md), [ADR 0006](0006-depth-ranked-composition.md), [ADR 0009](0009-fog-of-war-presentation.md) and [ADR 0011](0011-interface-panels-and-layouts.md) (their Defold-specific parts, listed below), and plan §2.

## Context

After phase 9 Defold is a thin shell. The deterministic simulation and the interface's rules are native C (`native/kp_core`, ADR 0008 and ADR 0010) and do not depend on the engine. Defold and Lua hold the presentation (`presentation/`), the render script and its materials (`render/`), the product and the viewer (`main/`, `viewer/`), the mission manager and the campaign (`game/`), the Lua test suites (`tests/lua/`) and the oracle's tools (`tools/lua/`).

The engine's limits shaped a good part of the drawing and the data:
- the restore pass and the rank kept in the index target's G/B (Vulkan does not keep a target's contents across rebinds, ADR 0006 §7a);
- the 1024 render commands of a frame and the composition plan's degraded variants (ADR 0006 §8);
- one texture upload per frame, never on the tick's frame (the fixed 1–3.4 ms cost of Vulkan `set_texture` on the Adreno 830, spike 15);
- the palette as a constant array (ADR 0005);
- single-page atlases packed by bob, with rotated frames and bottom-up UVs (ADR 0003);
- proxies, collection factories, sound components and resource counts as engine limits (ADR 0004);
- the canvas in the render pass because a GUI script has a Lua context of its own and a message carries about 2 KB (ADR 0011);
- mouse buttons sampled once per frame, held keys sent every frame, no way to hide the system cursor;
- the Lua heap and the cost of buffer streams in Lua as budgets (plan §10, ADR 0005).

## Decision

1. **Platform layer: SDL 3.4.18** (`SDL_MAIN_USE_CALLBACKS`, which emscripten and iOS need):
   - the window, the GL context, events, keyboard and text input, mouse, touch;
   - high DPI, the life cycle, files, the preference path.
2. **Audio: SDL3_mixer 3.2.4**, built with only the WAV and Ogg Vorbis (bundled stb_vorbis) decoders:
   - the interface's eight channels, the streams, the music and the click are `MIX_Track`s;
   - DirectSound's volume and pan become exact per-channel gains (`MIX_SetTrackGain`, `MIX_SetTrackStereo`): a pan of +2000 attenuates the left side by 20 dB, which Defold's linear pan could not give.
3. **Drawing: GLES 3.0 / WebGL2 through `SDL_GL_*` on every platform**, behind a narrow backend interface:
   - `kp_gfx` has two layers:
     - the passes (the render script's port);
     - a backend of 12–15 operations: texture, rectangle upload, render target, vertex buffer, draw with constants, target copy, readback, restore after a lost context.
   - One GLSL ES 3.00 source per shader; on desktop GL only the `#version` line differs (4.1 core on macOS, 3.3 core on Windows and Linux).
   - **SDL_GPU is not used for now:** it has no WebGL or WebGPU backend, and HTML5 is a target. A native SDL_GPU backend (Metal, Vulkan, D3D12) under the same passes is a deferred stage (S9), started only for a reason: Apple removing GL, GL losing to it in measurements, Windows drivers.
   - On Apple the system GL is used; ANGLE (GLES over Metal) is the fallback.
4. **PNG:** `SDL_LoadPNG_IO` / `SDL_SavePNG` of SDL 3.4. They keep 8-bit grayscale and paletted images as `SDL_PIXELFORMAT_INDEX8` with the exact indices (spike 16), so no SDL_image is needed.
5. **No other third-party libraries.**
   - GL is loaded by a small generated table over `SDL_GL_GetProcAddress`.
   - The test suites read JSON with their own reader.
6. **No Lua.**
   - The presentation becomes `native/kp_present` (plain C, no SDL or GL).
   - The application becomes `native/kp_app`: the session (the manager, the clock passed in), the glue and game flow, input, audio, the virtual file system, the profile and the saves.
   - The campaign, the save container and the remaining data readers move into `kp_core`.
   - The tests become `tests/c` (`kp_test`), the oracle's tools become `kp_oracle` subcommands with the same protocols, and the golden files become JSON.
   - The Lua binding (`native/kp_core/src/lua`), the Defold extensions and the toolchain's bob, dmengine, LuaJIT and Lua 5.1 are removed once the replacement passes the oracles (stage S7).
7. **Build:** CMake for every target. SDL3 and SDL3_mixer are subprojects built from the pinned source archives (`tools/toolchain/fetch.sh`, checked by SHA-256).
   - macOS, Windows and Linux: Ninja.
   - iOS: Xcode generated by CMake.
   - Android: SDL's Gradle template, with `libSDL3.so` shared because SDLActivity loads it before `libmain.so`.
   - Web: emscripten with WebGL2, IDBFS and a 1 MB stack.

## Consequences

- **Gone with Defold** (stage S8, done 2026-10-26: each item measured and checked under the oracles, [phaseS.md](../phaseS.md#s8-defolds-limits-removed)):
  - the restore pass and the rank in G/B: GL keeps a target's contents, and the depth target has 24 bits;
  - the command budget and the degraded plans;
  - the deferred-upload rule: an RGBA32F 256×73 upload with a canvas rectangle costs 0.065 ms on macOS and 0.061 ms mean (0.165 ms p95) on the Galaxy Z Fold7 through GLES 3.2, against 1.67 ms mean (5.51 ms p95) for one Defold `set_texture` there (spikes 15 and 16);
  - single-page atlases: WebGL2 has texture arrays;
  - the palette as constants: a 256×1 RGBA8 texture, since WebGL2 guarantees only 224 fragment uniform vectors and the array needed 256 (the frame time did not change);
  - the minimap chunks and message sizes, `shared_state`, the per-mesh `.buffer` resources, the engine's resource limits;
  - the mouse sampling, the held-key filter, the hidden-cursor TODO, the `dt` after a stall.
- **What remains a GL rule:** a bound render target cannot be sampled, so the destination copies stay (as blits).
- **Web:**
  - audio starts only in a visible page with the user's gesture: SDL 3.4 resumes its AudioContext on the first input (`autoResumeAudioContext`);
  - a lost WebGL context is restored: nothing is drawn while it is lost, then `kp_gfx_reset` makes the resources again and the world uploads its CPU copies;
  - IDBFS is mounted and synchronised asynchronously, and every save write is followed by `syncfs(false)`;
  - emscripten's default 64 KB stack is too small (spike 16 crashed in `glGetString`).
- **Data:**
  - Defold's resources (`.atlas`, `.go`, `.collection`, `.sound`, tilemaps, texture profiles) give way to pack containers (`PACK`: atlas pages, frame table, sounds) that the converter writes with its own atlas packer;
  - `grpmeta`, `sounds.kpd` and `kpm` `PACK`/`WAVS` refer to the new packs;
  - saves keep their format byte for byte.
- **Budgets:** plan §10's Lua heap and GC rows are replaced by the native memory of a mission.
