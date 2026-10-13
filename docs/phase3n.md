# Phase 3N progress: the native simulation core

Plan: `IMPLEMENTATION_PLAN.md` §7 phase 3N; decision and design: [ADR 0008](adr/0008-native-simulation-core.md), approved by the user on 2026-10-13. The simulation of phases 2–3 moves to `native/kp_core`, C99 in the common subset of C and C++. The Lua core is the line-by-line specification until parity, then it is removed.

## Stages

| Stage | Content | Status | Where |
|---|---|---|---|
| N.0 | library skeleton, local and build-server builds, integer helpers, the three generators, the `.kpd`/`.kpm` loaders | done 2026-10-13: see N.0 below | `native/kp_core/`, `tools/native/build.sh`, `tests/lua/test_kp_core.lua` |
| N.1 | images, sprites, IScript | done 2026-10-13: see N.1 below | `native/kp_core/src/core/kp_images*`, `kp_sprite.c`, `kp_iscript_vm.c`; `src/lua/kp_images_lua.c`; `game/images/native.lua` |
| N.2 | unit creation, pools, lists, the initial state | — | |
| N.3 | orders, dispatchers, timers, the command queue | — | |
| N.4 | movement, paths (`kp_path` merged), collisions, the spatial index | — | |
| N.5 | visibility, fog, detection, creep | — | |
| N.6 | saves, dump, draw list, events, viewer play mode, mission manager; Lua core removed; §10 on the device | — | |

## N.0

**Layout.**
- `native/kp_core/src/core/`: the library. Plain C with no Defold or Lua headers:
  - `kp_int.h`: exact x86 arithmetic;
  - `kp_rng`: the game, CRT and portrait generators;
  - `kp_kpf`: the container, with CRC-32 and expectation checks;
  - `kp_dat`: the DAT columns;
  - `kp_mission`: the mission container.
- `native/kp_core/src/lua/kp_core_lua.c`: the binding, standard Lua C API only.
- `native/kp_core/src/defold/kp_core_ext.cpp` and `ext.manifest`: the extension, which registers the global `kp_core`.
- `native/kp_core/tools/kp_load_check.c`: the sanitizer check of the loaders.
- `game/native.lua` finds the module: the extension's global in Defold, `build/native/kp_core.so` elsewhere.

**Local builds** (`tools/native/build.sh`, no build server):
- `build/native/kp_core.so`, built with `-Wall -Wextra -Wshadow -Wconversion -Werror`. The sources are compiled a second time as C++11 (`-fsyntax-only`), since the build server compiles them as C++.
- With `SANITIZE=1`: `build/native/kp_load_check` under AddressSanitizer and UndefinedBehaviorSanitizer. CI runs it over all 104 converted data files (`generated/`, without the old phase-0 spike files).

**Checks** (`tests/lua/test_kp_core.lua`; LuaJIT with and without the JIT, PUC Lua 5.1, the headless Defold engine with the extensions):
- The suites of `game/simulation/int.lua` and `rng.lua` run unchanged on `kp_core.int` and `kp_core.rng`. This covers the integer boundary vectors, the original-code vectors of `Rand15`/`RandRange` (D14) and the 420 portrait cases.
- `kp_core.kpf_parse` returns the same header and section table as `game/data/kpf.lua` for `dat.kpd` and every mission and scenario, with the checksums verified. Malformed files raise the same messages.
- `kp_core.dat` equals `game/data/dat.lua` column by column, with the layout and the requirement tables.
- `kp_core.mission` equals `game/data/mission.lua` for every mission and scenario in every field: META, the raw CHK sections, RULE, WAVS, WALK, RGNS, CONT and PACK.

**Build server.** The engine with the extension builds for all four targets: macOS arm64, Android arm64, iOS arm64 and HTML5 (wasm). Every source of `src/core`, `src/lua` and `src/defold` is in the extension library.
- The server compiles the `.c` files as C++; the symbols come out mangled. The core headers therefore declare C linkage, so the linkage is the same under either compiler.
- On the engine with the extensions, the headless Defold run passes all suites, including `test_kp_core`. The project's Defold test bundle carries no converted data, so there the data comparisons are skipped and the integer and generator suites run on the extension.

**Not part of N.0.** The `kp_replay` and `kp_bench` tools of ADR 0008 need the core's step. They come with the stages that port it (N.2–N.4).

## N.1

**The C world** (`native/kp_core/src/core/`) is game/images ported line by line: `kp_images.h` (records, settings, the hook interface), `kp_images.c` (world, list helpers, image operations), `kp_iscript_vm.c` (RunIScript), `kp_sprite.c` (sprites, lone thingies, fog ghosts, selection images). The data loaders `kp_grpmeta` and `kp_iscript` read `grpmeta.kpd` and `iscript.kpd`.
- Records are fixed arrays in the Lua world's key space. Every field is an `int32_t` holding exactly the number the Lua world holds, so the two compare field by field.
- The world calls the unit, order and combat code through one dispatcher (`KpHookCall`), with the hook list of `game/images/iscript.lua`. A missing hook fails with the Lua message.
- Where the Lua code raises an error, the C code calls `kp_images_fail`, which jumps (`longjmp`) to the caller's handler. Record indices read from fields that may hold `KP_NONE`, DAT values and IScript reads are checked; outside them the C code fails instead of reading out of bounds.

**The Lua bridge** (`src/lua/kp_images_lua.c`, `game/images/native.lua`). Until the unit code moves to C, Lua code drives the C world through the API of `game/images/world.lua`:
- the world is a userdata with every `World` method; `w.img`, `w.spr`, `w.thg` and `w.fog` are tables of field arrays, each a proxy over the C records; the lists, the rows and `w.vgrp` are proxies too;
- the hooks are the Lua functions of `w.hooks`, called through `lua_pcall`; an error a hook raises comes back to the calling method and is raised with the same value;
- the C world copies its data from the Lua tables of `opts.dat`, `opts.grp` and `opts.iscript`, one copy per set of tables (`kp_core.images.data`). Synthetic test data and a game's effective DAT therefore match exactly;
- `opts.rng` must be a `kp_core.rng.game` generator, shared with the Lua code.

Two test entry points exist in both worlds. `World:set_dat(table, column, index, value)` patches a DAT value as the original-code probes patch the loaded DAT (`tests/lua/iscript_trace.lua`). A world's `play_probe` field is called first by `image_play` with the requested animation, where the order probes break at `0x411730` (`tests/lua/test_orders_native.lua`).

**Checks** (`tests/lua/test_kp_core.lua`; the module swaps of N.0, extended to several modules and to reloading the modules that capture them):
- the image suites run unchanged on the C world: `test_images`, `test_selection`, `test_draw_list`, `test_health_bar` (13,846 cases), `test_iscript_golden` (every animation of every image and the synthetic programs against the original code);
- hybrid games, the Lua unit code over the C world (the path search in Lua, since `kp_path` reads the Lua world's tables until N.4), against the original's golden states: `test_initial_state` (64 missions), `test_scenarios` (every step), `test_orders_native`;
- the Lua game and the hybrid game run side by side on every scenario's logs; their full dump lines, image bounds included, are equal after every step;
- the 192 captured frames of the 64 missions: `KP_IMAGES=native tools/compose_capture.py` (C world in `tools/lua/capture_draw_list.lua`) gives 0 differing pixels in 7,903 images, as the Lua world does;
- CI runs `test_kp_core` once more with the module built under AddressSanitizer and UndefinedBehaviorSanitizer (`build/native/asan/kp_core.so`, the ASan runtime preloaded): no findings.

The C world suites run only with the LuaJIT JIT: the C world is the same under every runtime, and the proxies make the suites slow on the interpreters (the image suites take about a minute, the side-by-side run about another).
