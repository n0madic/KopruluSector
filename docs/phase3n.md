# Phase 3N progress: the native simulation core

> **Historical record.** Written before phase S ([phaseS.md](phaseS.md)), when the runtime was Defold with Lua. The Defold build server, `tools/native/build.sh` and `tools/spike.sh` named below were removed in phase S; the build is CMake (`CMakeLists.txt`, `tools/product.sh`).

Plan: phase 3N; decision and design: [ADR 0008](adr/0008-native-simulation-core.md), approved by the user on 2026-10-13. The simulation of phases 2–3 moves to `native/kp_core`, C99 in the common subset of C and C++. The Lua core is the line-by-line specification until parity, then it is removed.

## Stages

| Stage | Content | Status | Where |
|---|---|---|---|
| N.0 | library skeleton, local and build-server builds, integer helpers, the three generators, the `.kpd`/`.kpm` loaders | done 2026-10-13: see N.0 below | `native/kp_core/`, `tools/native/build.sh`, `tests/lua/test_kp_core.lua` |
| N.1 | images, sprites, IScript | done 2026-10-13: see N.1 below | `native/kp_core/src/core/kp_images*`, `kp_sprite.c`, `kp_iscript_vm.c`; `src/lua/kp_images_lua.c`; `game/images/native.lua` |
| N.2 | unit creation, pools, lists, the initial state | done 2026-10-13: see N.2 below | `native/kp_core/src/core/kp_game*`, `kp_units.c`, `kp_counts.c`, `kp_strength.c`, `kp_map_units.c`; `src/lua/kp_game_lua.c`, `kp_lua_bridge.c` |
| N.3 | orders, dispatchers, timers, the command queue | done 2026-10-13: see N.3 below | `native/kp_core/src/core/kp_orders.c`, `kp_order_dispatch.c`, `kp_timers.c`, `kp_commands.c`, `kp_step.c`, … |
| N.4 | movement, paths (`kp_path` merged), collisions, the spatial index | done 2026-10-13: see N.4 below | `native/kp_core/src/core/kp_movement.c`, `kp_flingy*.c`, `kp_spatial.c`, `kp_paths.c`, `kp_local_search.c`, `kp_placement.c`, `kp_collision.c`, `kp_repulse.c` |
| N.5 | visibility, fog, detection, creep | done 2026-10-13: see N.5 below | `native/kp_core/src/core/kp_terrain.*`, `kp_fog.c`, `kp_detection.c`, `kp_creep.c` |
| N.6 | saves, dump, draw list, events, viewer play mode, mission manager; Lua core removed; §10 on the device | done 2026-10-13: see N.6 below | `native/kp_core/src/core/kp_start.c`, `kp_rules.c`, `kp_hooks.c`, `kp_dump.c`, `kp_save.c`, `kp_draw.c`, `kp_game_data.c`; `native/kp_core/tools/`; `tools/lua/oracle.lua` |

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

## N.2

**The C game** (`native/kp_core/src/core/kp_game.h`) holds the state the ported modules own: the unit records (`KpUnits`, the 161 fields of `game/units/unit.lua`, records 1..1700), the unit lists, the player unit chains, the counters and per-player statistics, the strength tables and the cloak list. Every game field is an `int64_t` holding exactly the number the Lua game holds, so no field needs a choice of signedness: the Lua code stores both signed values (bounds of -1) and DWORDs (`visibility` 0xffffffff) in them.

Ported line by line:
- `kp_units.c`: `game/units/unit.lua` (pool, creation, completion, images, relocation, move targets, hit points, damage overlays);
- `kp_counts.c`: `game/units/counts.lua`;
- `kp_strength.c`: `game/units/strength.lua`. The type strength keeps the Lua code's `sqrt` in double precision, which IEEE 754 rounds the same on every platform;
- `kp_map_units.c`: `game/units/map_units.lua` (map units, unit records, hangars, the cloak list, burrowing, add-ons, lift-off, minerals, doodads).

Unit removal (`remove.lua`) and ownership transfer (`owner.lua`) move with the selections and orders they depend on, in N.3.

**The hybrid game** (`src/lua/kp_game_lua.c`). `kp_core.game.install(Game)` replaces the ported methods of the Lua Game table with C functions over a `KpGame`, created on the first call and kept as the game's `native` field:
- the C state is served with the shape of the Lua tables: `g.u[field][n]`, `g.lists.<name>`, `g.player_units`, the counters, `g.pstat`, `g.strength` are proxies; the C scalars (`units_allocated`, `error_code`, `cloaked_head`) go through the Game metatable;
- the C code calls the methods still in Lua by name (`KP_LUA*`), reads Lua state not ported yet by path (`KP_GET`: `players[p].controller`, `orders.order[node]`), and reads the DAT values from `g.dat` at each use (`KP_DAT`). The tests change DAT tables during a game, and the mission's rules replace columns, so a C copy would go stale;
- `src/lua/kp_lua_bridge.c` holds the proxies of both bridges; the image world finds its userdata through the registry, so its hooks also run when a game method reaches them.

**Checks:** the hybrid suites of N.1 now run with these modules in C: the initial states of the 64 missions, every scenario step, the order cases, and the Lua game line by line at every scenario step; again under ASan and UBSan.

## N.3

Ported line by line:
- `kp_orders.c`: the order node pool and the unit order queues (`game/units/orders.lua`);
- `kp_order_dispatch.c`: the main, hidden and secondary dispatchers with the ported orders and the native addresses of the others (`order_dispatch.lua`);
- `kp_timers.c`, `kp_targets.c`, `kp_requirements.c` (the DAT requirement evaluator, CanIssueOrder, CanUseTech), `kp_owner.c`, `kp_remove.c` (hiding, removal, the pending pass), `kp_selection.c`;
- `kp_formation.c` (`group_formation`, `formation_point`; the destination adjustments of the pathing stay in Lua until N.4) and `kp_commands.c` (the turn's commands);
- `kp_step.c`: the object step, the unit update, the cloak list, the frame, plus the trigger clocks, the AI counters and the guard records;
- `kp_directions.c`: the flingy direction tables, generated from `game/data/directions.lua`.

The game's scalar fields (`frame`, `turn`, `power_dirty`, …) are now one table of `KP_SCALARS`: a number or a boolean, or nil before the Lua code would set it (`resource_areas` used all three states until phase 7 replaced it with the resource area records `KpAreas`). The order pool, the AI counters, the guard records and the trigger clock are served to Lua as record proxies (`kp_bridge_push_record`), the selections as arrays.

A DAT value the Lua tables lack reads as `KP_NIL`, which compares unequal to every number as nil does in Lua (the order cases pass order numbers beyond the table).

**Checks:** the hybrid suites (the order scenarios at every step, `test_orders_native`, the initial states, the Lua game line by line) pass with these modules in C, under ASan and UBSan too.

## N.4

Ported line by line:
- `kp_spatial.c`: the spatial index (`game/units/spatial.lua`) and the rectangle queries of `game/vision/detection.lua` (`units_in_rect`, `unit_box_within`; the rest of detection is N.5);
- `kp_flingy.c`, `kp_flingy_move.c`: the flingy records, speeds and steps (`flingy.lua`, `flingy_move.lua`) and the flingy helpers of the IScript hooks (`hooks.lua`);
- `kp_movement.c`: the movement states (`movement.lua`); state 13 (0x42be70) still fails with its native address as in Lua;
- `kp_paths.c`: the path pool, the region graph, the region search and the route (`game/pathing/pool.lua`, `heap.lua`, `regions.lua`, `route.lua`); `kp_local_search.c`: the local path search (`local_search.lua`), which replaces `native/kp_path` (the old module is removed with the Lua core at N.6);
- `kp_placement.c`: placement and the contour checks (`placement.lua`); `kp_formation.c` gains the destination adjustments;
- `kp_collision.c`, `kp_repulse.c`: the unit and terrain collisions and the air density grid (`collision.lua`, `repulse.lua`).

The C game copies the mission's RGNS and contours from `g.regions` and `g.contours` when it is created. The path pool is the 1024 × 128-byte native block (`path_u8`/`u32`/`point`… are C methods); the spatial index, the prepared step and the air density grid are served to Lua as `g.spatial`, `g.mv` and `g.density` (an array of the 0xab × 256 cells). `tools/lua/state_model.lua` reads the density cells by index, which gives the same lines for the Lua table and the C array.

Fidelity notes from the port: a region id, path record, contour index or density cell outside its range fails in C where the Lua code would read or write nil (only reachable from an invalid state); `extent_ctx` reads 0 before its first set where Lua would fail.

**Checks:** the hybrid suites (the scenarios at every step, `test_orders_native`, the initial states, the Lua game line by line) pass with these modules in C, under ASan and UBSan too.

## N.5

Ported line by line:
- `kp_terrain.h`/`kp_terrain.c`: the tile state (`g.tiles`, `g.megatiles`, `g.raw_tiles`, the creep backup and border maps, the receding-creep record), the tileset copied from `g.tileset_data` (CV5, VF4, megatile flags) and the terrain lookups of `game/simulation/game.lua` (`megatile_at`, `cv5_megatile`, `minitile_flags`);
- `kp_fog.c`: tile flags, the vision templates and RevealSightArea, the unit sight and vision, the sprite visibility (`game/vision/fog.lua`);
- `kp_detection.c`: detection, target forgetting and cloaking (the rest of `game/vision/detection.lua`);
- `kp_creep.c`: the creep engine (`game/terrain/creep.lua`).

The Lua fog module caches reveals (`reveal_applied`, `reveal_rows`, `reveal_cache`, `reveal_blocking`) that replay what a direct reveal clears; the C reveal is the direct RevealSightArea, so those caches have no C state and `visibility_bits_set`/`reveal_cache_flush` do nothing in C. The C images world reads the tile flags of the C game (`KpImages.tile_ctx`), the tile arrays are served to Lua as `g.tiles` and its siblings (0..0xffff), and the creep variants draw from the C CRT generator (`g.crt` of the hybrid games is `kp_core.rng.crt`).

Fidelity notes: the C tile arrays hold 256 × 256 entries, so a tile beyond the map reads 0 where the Lua table holds nil (only reachable from an unclamped creep area at the map's edge); unit sounds of BeginCloak stay with the events of N.6 (the Lua game's `play_sound` is never set; done in N.6,
`kp_play_unit_sound`). With the Lua core gone, the out-of-range cases of these notes fail or read 0 only from invalid
states; the original is the one reference.

**Checks:** the hybrid suites (the initial states of the 64 missions, the scenarios at every step, `test_orders_native`, the Lua game line by line) pass with these modules in C, under ASan and UBSan too.

## N.6

Steps: (1) the native game: start, DAT, hooks, events, dump; (2) the draw list in C and the viewer's play mode over
the native game; (3) saves (a new format, version 1); (4) the Lua core, `native/kp_path` and the tests of their internals removed, the
suites on the native game; (5) `kp_replay`, `kp_bench` and the §10 tick bench on the Fold7; (6) the mission manager.

### Step 1: the native game

`kp_core.game.new(blobs, options)` starts a game with no Lua code: blobs are the `dat.kpd`, `grpmeta.kpd`,
`iscript.kpd`, mission `.kpm` and tileset `.kpd` contents, options the seed and the expansion byte (the game and CRT
generators seeded as `tools/lua/state_model.lua` start_mission does). The game table serves the C state as a hybrid
game does (`g.u`, `g.lists`, `g.tiles`, …, `g.img` over the game's images world, `g.rng`, `g.crt`).
- `kp_start.c` ports `game/simulation/start.lua` (players, alliances and vision, the tile import of
  `game/data/terrain.lua`, doodads, the fog mask, the acquisition ranges, preplaced units and their relations);
  `kp_rules.c` the UMS override effects of `game/data/rules.lua`, applied to the game's own copy of the DAT.
- The players' state (controllers, races, forces, alliances, shared vision, upgrades, techs) is C state in every game;
  the hybrid game moves the Lua tables into it and serves proxies of the same shape.
- KP_DAT reads the columns of the game's own DAT through an enum of the 51 columns the C code uses
  (`kp_dat_columns.h`); a hybrid game still reads the Lua tables.
- `kp_hooks.c` is the images world's hook dispatcher into the C game (`game/units/hooks.lua` image_hooks).
- Events: a ring of sounds, error notifications and text lines (`game/simulation/events.lua`), drained with
  `g:events()`.
- `kp_dump.c` prints the lines of `state_model.lua` M.lines (`g:dump(drawn)`).
- `tools/lua/oracle.lua` holds the runs (runner, advance, the command and interface logs, the category CRCs) for the
  Lua and the native game, and starts native games; `state_model.lua` keeps the Lua game's start and lines.

**Checks** (`test_kp_core`): native games equal the original's golden CRCs at every recorded step of every scenario
and in the initial state of all 64 missions; the native dump equals the Lua game's `state_model` lines at every step
of every scenario; the event ring carries the sounds, errors and text lines; clean under ASan and UBSan.

### Step 2: the viewer's play mode on the native game, the draw list's collection in C

The viewer's play mode (`viewer/play_world.lua`) starts its game with `kp_core.game.new` from the data and mission
files (the viewer keeps their bytes next to the parsed tables) and moves the game's sound events into its scene ring
after every step; the tick bench (`viewer.tick_bench`) runs the native game through `tools/lua/oracle.lua`. The play
check of `tools/ci.sh` and `test_play_world` run the native game; both are skipped without the kp_core module.

`kp_draw.c` collects the draw list (`w:draw_collect(first_row, methods, out)`): the sprites of the drawn rows visible to
the local player in depth-key order and their images from the tail to the head, skipping hidden images and those whose
draw method has no material, as a flat array (per sprite 4 values, per image 12) that `presentation/draw_list.lua`
turns into draws. The 192 captured frames of the 64 missions (`tools/compose_capture.py`) still give 0 differing
pixels in 7,903 images; the terrain differs only by the creep edges, as before.

### Step 3: saves

`kp_save.c`: `g:save([mission[, data]])` returns the game's mutable state and that of its images world in sections of
little-endian fields (the scalars, units, lists, counters, orders, selections, the requirement offsets, AI counters,
the spatial index, the prepared step, the path pool and the search contexts with the region graph's run-time slots,
the air density, the tile arrays and the receding creep, the players, guards, trigger clock, results, both
generators, then the world's records, lists and contexts), headed by the magic `KPSV`, the version (1: the format starts over, no saves have shipped), the mission's
name (by default the game's `options.name`) and the data's identity, and closed by an FNV-1a fingerprint.
`g:load(bytes[, mission[, data]])` checks all of them
and restores the fields into a game of the same mission and data, fresh from its start or at any later step (a dry
walk first checks the whole save, so a refused save changes nothing); the static data the start rebuilds,
the remembered-terrain set and the event ring stay out. One walk over the state serves both directions.
`test_save` saves every scenario in the middle, loads it into a fresh game, checks the round trip byte for byte and
the continuation against the original's CRCs at every recorded step, and the initial states of all 64 missions;
damaged, truncated and foreign saves are refused.

### Step 4: the Lua core removed

The Lua simulation is gone: `game/units`, `game/pathing`, `game/vision`, `game/terrain`, `game/ai`, `game/triggers`,
`game/save`, the game, start, step, commands, pool, snapshot and generator modules of `game/simulation`, the Lua images
world (`game/images/world.lua`, `iscript.lua`, `sprite.lua`), `tools/lua/state_model.lua`, `native/kp_path` and the
hybrid half of the binding (`kp_core.game.install`, the Lua callbacks of the C game). What stays in Lua is the data
readers (`game/data`), the images constants (`game/images/core.lua`, `image.lua`, `selection.lua`), the mission
manager's clock and event ring, the integer helpers, presentation, the viewer and the tools.
- The oracle suites run on native games on every runtime, every case: `test_initial_state`, `test_scenarios`
  (category CRCs computed in C, `g:dump_crcs`), `test_references`, `test_save`, `test_orders_native` (the probe's table
  bytes written into each game's own DAT through `g.dat`; the special cases start the map without its units,
  `no_units`), `test_play_world`.
- The image suites (`test_images`, `test_selection`, `test_iscript_golden`, `test_health_bar`, `test_draw_list`,
  `test_browser`) and the viewer's map scene run on the C images world (`game/images/native.lua` world();
  `tests/lua/native_only.lua` skips them without the module); `test_rng` checks `kp_core.rng`.
- `tools/ci.sh` runs every suite under ASan and UBSan; `tools/lua/start_state.lua` and `tick_bench.lua` run native games.

The draw list's collection moves into C (`kp_draw.c`, the world's `draw_collect`): the sprites the local player sees in
the order of the native depth key and each sprite's drawable images from the tail to the head, as a flat array of
their fields. `presentation/draw_list.lua` builds materials, ranks, layers and composition windows from it without
walking the sprite and image tables; `test_draw_list` and the composition of the 192 captured frames check it.

### Step 5: the native tools and the §10 tick bench

`tools/lua/export_replay.lua` writes a scenario's golden file as a replay (KPRP 1 text: the mission, the seed, the
command and interface logs with the interface's game calls, a critter's death of its answers and a modeless menu's pause among them, the original's
CRCs; `native/kp_core/tools/kp_runner.h`). `kp_replay ROOT REPLAY [STEP]`
replays it on a native game and compares the dump CRCs at every recorded step, each step against its own run of the
step-sorted CRCs (or prints the dump lines at STEP; turns take up to `KP_TURN_COMMANDS` commands, as the binding's;
a category of the original that the dump no longer prints counts as a difference, and a replay without steps or CRCs
is refused);
`kp_bench ROOT REPLAY [RUNS]` times the object steps. `tools/native/build.sh` builds both for the desktop,
`tools/ci.sh` replays every scenario, and `tools/native/android_bench.sh [SCENARIO [RUNS]]` builds them with the
Android NDK and runs them on the phone through adb.

Result on 2026-10-13, Galaxy Z Fold7 (`bench_200`, 200 units, 760 steps): `kp_replay` equals the original at every
step on the phone (also `marine_group`, `rescue_fog`); `kp_bench` over 10 runs: mean 0.096 ms, median 0.057 ms, p99
0.48 ms, max 2.8 ms (one outlier; the other runs' maxima stay near 1 ms). The Lua core measured a mean of 3.9 ms and a
maximum of 10.1 ms on the same check; §10 (4 ms per step, none above 8 ms) holds with a wide margin.

### Step 6: the mission manager

`game/simulation/manager.lua` is the one owner of a mission's native game (plan D7, D8; ADR 0008 §6): it starts and
primes the game, runs its steps synchronously on the wall clock (`game/simulation/clock.lua`: the speed table, at
most `max_catchup` steps per frame, pause and resume), queues the player's commands for the next step, hands the
game's sounds, error notifications and texts to the presentation and saves and loads the game at a step boundary.
The events stay in kp_core's ring until the presentation drains them (`manager.events:drain`, the drain of
`game/simulation/events.lua`): `g:events(out)` takes them with `kp_event_take` into one reused flat array, 7 values
per event (`kp_core.game.EVENT_FIELDS`), with no table per event or per step (the former `g:events()` built both:
about 47 KB of garbage over the 760 steps of `bench_200` without a single event, more with every sound once combat
arrives). The ring fails when full or when more than 64 texts wait (texts are cut at 255 bytes, as the original's
256-byte message buffer), so the presentation drains it every frame. A game without a presentation starts with the
ring off (`kp_core.game.new` option `events = false`, `KpStartInput.no_events`) and records no events at all: the
oracle's games (`tools/lua/oracle.lua` start_mission, unless a test asks for `events`; its runner then hands them to
`run.on_event` after every step) and the games of `kp_replay` and `kp_bench`. `game/simulation/events.lua` keeps the
event kinds and the Lua ring of the viewer's animated map scene. A step that raises stops the game and keeps its last
state: the manager runs no further steps, has no save of it (the failing step stopped half-way) and reports the
interpolation fraction 1, so the presentation shows that state still. The viewer's play mode is a manager (`viewer/play_world.lua` adds the picking, the
selection and the fog); the viewer's animated map scene and the image browser tick on a clock of their own.
`test_manager` checks the clock, the queue, the event forwarding and a stopping step on a stand-in core, and that a
loaded save continues as the saved game. A load restores into the manager's own game (kp_core loads into a game at
any step), so the game and the world the presentation holds stay the same objects; the manager counts the loads
(`loads`) and its owner restarts the presentation's motion (`presentation/images.lua` restart), so the loaded state
shows at once without gliding from the state before. The data identity of saves is the game's own by default
(`g.data_identity`, the CRC-32s of its five files, computed at the first save or load from the game's copies; dat.kpd
and the tileset at the start). The viewer reads the data files for each play-mode game (the game copies what it
keeps) instead of holding their bytes, and names its saves by the mission's path. The campaign's mission script (plan §7
phase 4) drives the manager.

Window events go to the clocks (`Clock:on_window`, plan D8): iconifying suspends the clock and the return ends the
suspension with the time away discarded; a suspension never ends the game's own pause (`pause`/`resume`). On Android
and iOS losing the focus pauses the game (autopause), and it stays paused until its owner resumes it; the viewer,
without a pause menu yet, resumes when the focus returns. The viewer forwards the events to its clock and to the
game's manager. The viewer shows a game's error notifications (stat_txt strings from `strings.kpd`) and text
lines for 7 s, four at most, without the strings' colour and format codes (`strings.plain`; debug text until the
game's interface, plan §7 phase 9).

The binding runs every game and images-world method in a protected call (`game_call`, `world_call`): whatever raises
inside it, a C failure or a Lua error of the Lua API or of a hook, leaves the failure handlers and the Lua states as
the call found them before the error goes on to the caller; string arguments and command bytes must be Lua strings.
`g.local_player` sets the game's and its world's local player with the vision derived from it (`kp_set_local_player`;
a replay's world keeps the replay's vision); `g.local_vision` and `g.data_identity` are read-only, and a game's world
refuses a local player of its own.

Unit sounds: `kp_unit_sound` (PlaySoundAtUnit `0x446940`) puts a sound event (`KP_EVENT_SOUND_UNIT`, the unit, its
sprite's position and its type at the call: a cancelled Zerg building sounds before it is a Drone again) when the local player hears it (the unit is theirs or its sprite is seen; in a replay, seen by
the world's vision, which plays the part of `0x67cc84`), and a sound without a unit as `KP_EVENT_SOUND_SCREEN` (no
position); every sound event's `a` carries the call's enable and minVolume and the talking request that follows it
(`kp_game.h`); BeginCloak `0x41d200` plays its sound through it (`test_kp_core` unit_sounds). The volume by the
distance from the screen, the per-sound minimum and the talking request belong to the interface (plan §7 phase 9D).

The game's own deselections tell the interface what the network gets: `KP_EVENT_SELECTION_SENT` carries the local
selection the call left (11 bits a unit, four in each of a, b and c, so that a Lua number holds each field whole),
since one step can take several units out one by one (a Stasis Field: each removal is its own selection command);
`KP_EVENT_SELECTION_GONE` (RemoveFromSelections `0x49c8b0`) takes a unit out of the interface's network mirror
(`0x67261c`, `0x472100`: the last unit takes its place). `kp_critter_annoyed` is the game call of PlayUnitSelectSound
`0x446c30` that kills a critter selected too often (KillUnit `0x42a4d0`, the lone sprite 267 at elevation 4); its
death sound plays inside the interface's call (`kp_event_peek`).

The Defold suites on the engine with the native extensions (`tools/ci.sh` writes `build/tests_data.settings` from
game.project's custom resources) carry
the generated data, scenarios and missions, so every oracle suite also runs on the extension's build of kp_core (157
of 157, no skips, about 80 s). `tools/native/android_bench.sh` picks the phone by its hardware serial when adb lists it
under several wireless-debugging names.
