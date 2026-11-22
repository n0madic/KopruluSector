# ADR 0028: A native SDL_GPU backend under kp_gfx's passes

Status: accepted (2026-11-22). The user asked for stage S9 of [phase S](../phaseS.md#s9-sdl_gpu-backend) and
approved the plan on 2026-11-22. Amends [ADR 0012](0012-sdl3-runtime.md) decision 3, which deferred this stage.

## Context

ADR 0012 put the drawing on GLES 3.0 / WebGL2 / desktop GL behind a narrow backend interface
(`native/kp_gfx/kp_gfx_internal.h`, the `kp_gb_*` operations). It deferred a native SDL_GPU backend (Metal, Vulkan,
D3D12) to stage S9. SDL 3.4.18's SDL_GPU is already built into every desktop product (`SDL_GPU:BOOL=ON`).

SDL_GPU 3.4.18 has no ETC2 or EAC formats. The phones' HD sets keep their normal maps and occlusion in EAC and their
tiles and water in ETC2 ([ADR 0013](0013-remastered-data-source-and-hd.md)). SDL_GPU has no WebGL or WebGPU backend either.

## Decision

1. **Desktops only.** Metal on macOS, Vulkan on Windows and Linux. iOS and Android stay on GL (no ETC2/EAC), the web
   always draws with WebGL2. Windows takes Vulkan only (SPIR-V); D3D12 is a `TODO`.
2. **The passes stay as they are.** `kp_gfx_backend.c` dispatches the `kp_gb_*` operations to the GL or the SDL_GPU
   backend by `KpGfxApi.backend`. The application owns the device and the window's claim, as it owns the GL context.
   The present, the swap interval and the version string go through the backend.
3. **The memory layout is GL's.** The generated vertex stages negate `gl_Position.y`; SDL_GPU puts NDC y +1 at the
   viewport's first memory row on Metal and on Vulkan (a negative-height viewport). So a target's row 0 is where GL has
   it, `gl_FragCoord` reads as GL's, readback rows come bottom-up, and the host's code does not change. The window is an
   offscreen RGBA8 target, blitted to the swapchain flipped vertically. GL takes a viewport larger than its target (the
   passes set one, e.g. a 2562-pixel world on a 2560-pixel window); SDL_GPU and Metal do not. The backend clamps the
   viewport to the target and gives the vertex stage the mapping of GL's viewport into it (`kp_viewport`, after the
   constants): (1, 1, 0, 0) when nothing is clamped.
4. **One shader source.** The GLSL ES 3.00 of `kp_gfx_shaders.c` is rewritten line by line as Vulkan GLSL 450
   (`kp_gfx_gpu_glsl.c`):
   - the constants become one uniform block, each `KP_U_*` at 16 times its number;
   - a sampler takes a binding in the stage's set in the order declared;
   - an unsigned integer sampler becomes a `utexture2D` (`GL_EXT_samplerless_texture_functions`, so `texelFetch` and
     `textureSize` read as before), bound after the samplers as a storage texture. SDL_GPU refuses integer formats in
     samplers;
   - the varyings take the vertex stage's output order.

   `kp_gfx_gpu_gen` compiles the text with glslang, checks it with spirv-val and converts it to MSL with SPIRV-Cross
   (MSL 2.1, bindings as decorated). Where Xcode's Metal toolchain is (`xcodebuild -downloadComponent MetalToolchain`),
   it also builds each stage's metallib (`metal -std=macos-metal2.1 -mmacosx-version-min=15.0`, then `metallib`: the
   same bytes from any path); without it the generator keeps the metallib of an unchanged MSL from the file it was built
   with, and a stage whose MSL changed has none, with a warning that the toolchain builds the better file. The output,
   `kp_gfx_gpu_shaders.c`, is committed: the platforms' builds and the hosted CI need none of these tools (the phones'
   and the web's builds leave the metallibs out, `KP_GFX_NO_METALLIB`). test_gfx checks the file against the transform
   (each stage's reflection and the SHA-256 of its text) and each metallib against its MSL (the SHA-256 it was built
   from, Metal's library magic).
5. **Every slot a stage declares is bound.** A slot the pass does not bind reads a zero texel of its kind, as on GL.
   glslang keeps unused samplers, and SDL_GPU checks that each is bound.
6. **The pipelines are built when the backend starts**, from a table of the draw states the passes use. A failure there
   (or no device, no window claim, no storage support for the integer formats) falls back to GL before the first frame.
7. **Choice and default.** `--gfx auto|gl|gpu` and the video option Renderer (Auto, OpenGL, and Metal or Vulkan: the
   profile's `VOPT`, read before the drawing starts; `--gfx` is a given flag, which OK does not keep unless the dialog
   changed it). A change is taken at the dialog's OK (decision 9). `auto` is GL until the oracles and the benchmarks show, per
   platform, that SDL_GPU draws the same frames and is not slower; GL stays the fallback. Since stage S9.7 `auto` is
   SDL_GPU on macOS (S9.6: the same frames, fewer cycles with vsync); Windows and Linux stay on GL until measured on
   real hardware. A software Vulkan device (lavapipe, SwiftShader: the CPU's drawing, many times slower than GL on the
   machine's GPU) draws only under a given `--gfx gpu`, for the checks in a container; auto and the option's Vulkan
   want a hardware device and fall back to GL without one (`kp_flow_software_gpu`). `--gfx-debug` turns on SDL_GPU's
   checks.
8. **The frame check against GL** runs one script on the fixed clock under each backend and compares the frames
   (`tools/gfx_compare.py`): the index path's byte for byte, the HD path's within one level a channel. Geometry whose
   edges fall on pixels' centres is avoided in the passes: each API breaks such ties its own way (the vector text's
   shadow was moved to whole window pixels for it).
9. **The renderer switches in a running game.** The dialog's OK asks the host (`KpFlowHost.renderer`), and the next
   frame starts with the switch: the drawing is detached (`kp_gfx_detach`: the backend's textures, targets and pipelines
   freed, the palette and gamma kept; the effects are the host's each frame), the GL context or the GPU device and the
   window destroyed, a window made at the old one's place, size and fullscreen or maximized state
   (`SDL_CreateWindowWithProperties`), and the new API attached (`kp_gfx_attach`). The window is always made anew: a
   Metal or Vulkan window takes no GL context through SDL's public API. The video options the new API offers are
   computed first, then the world's art is chosen again by them and the API's texture formats (`kp_world_reload_art`:
   an HD set of BC pages on Metal may be ASTC's elsewhere), without resetting the gfx just attached (GL would build
   its backend twice); the interface's HD art and a playing cinematic reload as after a lost context. A failed start falls back to the API that drew before; when neither starts the app stops with a
   message box.

## Consequences

- A second backend: every change to the passes' draw states extends the pipeline table, and every new shader goes
  through the transform's regular declarations. test_gfx fails when the generated file is stale.
- Metal takes the metallibs built offline (fast math, as SDL's compile of the MSL): on a cold Metal cache (the first
  start after an update) the stages and pipelines take 0.26 s where compiling the MSL took 4.6 s (M4 Max). A stage
  without a metallib, or one Metal refuses (an older macOS than the file's 15.0), compiles its MSL as before.
- Without vsync in a window Metal presents at the display's refresh (S9.6), where GL's swap does not wait: a run that
  measures frames without vsync shows SDL_GPU's frames at the refresh rate (headless runs present nothing).
- Switching the renderer recreates the window: a fullscreen or maximized window comes back at its current size, since
  SDL 3 has no public getter of the windowed geometry to restore after leaving it. Metal to GL takes about 60 ms, GL to
  Metal about 20 ms (M4 Max, warm caches); the frames after a switch are the unswitched run's byte for byte (the index
  path).
- `kp_gb_alive` is always 1 on SDL_GPU: a lost device is not reported on desktops, so GL's context-loss path stays GL's.
