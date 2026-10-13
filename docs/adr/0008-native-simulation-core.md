# ADR 0008: Native simulation core (kp_core)

Status: accepted (2026-10-13). The user approved the design, stages N.0–N.6, C99 in the common subset of C and C++, the removal of the Lua core after parity, and closing the §10 tick budget at stage N.6. Supersedes plan decisions D7 (simulation as Lua modules) and D19 (native code only by profiling) for the simulation; [ADR 0007](0007-native-local-path-search.md) is folded in at stage N.4.

## Context

Plan §10 allows 4 ms per simulation step on the target mobile device, where LuaJIT runs as an interpreter (iOS forbids the JIT; Defold's Android engine runs without it too), and no step above 8 ms. After the Lua optimizations of phase 3, the 200-unit check (`bench_200`) on a Galaxy Z Fold7 measures a mean of 3.9 ms and a maximum of 10.1 ms ([phase 3](../phase3.md#performance-37)). That covers only movement, orders and visibility.

- The profile is flat. On the slowest step, IScript takes 22% and the target scans' `QueryUnitsInRect` 18%; the rest spreads over dozens of functions. Moving the spatial index and visibility alone to native code (D19) would gain about 20–30% on those steps.
- Phases 4–9 add combat, triggers, the economy and AI to every unit's step.
- A prototype `QueryUnitsInRect` in C over the Lua tables ran 2.8 times faster than the Lua version, with identical results on all 8,789 calls of the check. Native arrays in C memory remove the table reads too.
- Rewriting the Lua simulation in phase 10 would mean writing phases 4–9 twice. Today the simulation is about 14,000 lines of Lua; by the estimate at decision time, phase 10 would face three to four times that.

## Decision

1. **Scope.** All deterministic simulation moves to the native library `kp_core`:
   - the game, CRT and portrait generators, the step clock;
   - pools and lists;
   - images, sprites and IScript;
   - units, orders, movement and paths (with the `kp_path` search);
   - collisions and the spatial index;
   - visibility, fog and detection;
   - creep, the command queue and events;
   - saves and the state dump of the oracle.

   Lua keeps presentation, UI, the viewer, the mission manager and input (turned into commands). The converter, the `.kpd`/`.kpm` formats and the golden data do not change. Phases 4–9 are written in C directly from the 1.17 disassembly.
2. **Language.** C99 in the common subset of C and C++: the Defold build server compiles the sources as C++ (as it does for `kp_path`).
   - No VLAs, compound literals, `restrict`, or implicit conversions from `void *`.
   - Indices of different pools are distinct types: one-field structs such as `typedef struct { uint16_t v; } UnitId;`.
   - Arithmetic uses explicit-width types and inline helpers (`s16`, `sar`, the 32-bit wraps). Signed overflow is never relied on. The simulation uses no floating point.
3. **Layers.**
   - `libkp_core`: plain C with no Defold or Lua headers, built locally (CMake or `tools/native/build.sh`) for desktop and with the Android NDK.
   - `kp_core_lua.c`: the Lua binding, written only against the standard Lua C API. The same file builds the desktop module (`build/native/kp_core.so` for LuaJIT and PUC Lua) and the Defold extension.
   - A thin Defold extension that registers the module, as `kp_path_ext.cpp` does now.

   Only the extension needs the build server. Desktop tests, sanitizers and device benchmarks run locally:
   - `kp_replay` replays a scenario from its command logs and prints the dump lines and CRCs;
   - `kp_bench` is an NDK executable run on the phone through `adb`.
4. **Where state lives.** One C struct `kp_game` holds a game; a Lua userdata owns it (`__gc`). There are no globals, so two games can coexist (the save test loads one while the other continues).
   - Records live in fixed-capacity arrays indexed by the original's record numbers, with the original's list order:
     - units 1700;
     - images 5000 plus the bar, circle and placement pools;
     - sprites 2500, lone thingies 500;
     - paths, order nodes, the spatial index (3400 entries);
     - the 256×256 tile flags.
   - The step allocates nothing; everything is reserved when the game is created (about 1–2 MB).
   - C parses the `.kpd`/`.kpm` blobs directly (D3); Lua passes the resource strings.
5. **Lua API.**
   - `kp_core.new(blobs, options)`;
   - `game:step(commands)`;
   - `game:events()`: sounds, errors and text from an event ring;
   - `game:draw_list(buffer)`: the native draw list, packed. This is the collection of `presentation/draw_list.lua` moved into C, so presentation no longer walks the sprite and image tables in Lua;
   - queries for the UI and the viewer;
   - `game:save()` and `kp_core.load(bytes, ...)`;
   - `game:dump(category)`: the lines of `tools/lua/state_model.lua`, byte for byte, so the existing golden CRCs keep working.
6. **Order of execution.** Strictly sequential, on one thread: the mission manager calls `step` synchronously, and the step runs in the original's order. The reasons: per-step equality with the original, HTML5 has no threads by default, and the expected step costs a fraction of a millisecond.
7. **Stages.** Each stage is checked against its golden data before the next starts. Until N.6 the Lua core and the C core run side by side on the same commands, and their full category dumps (lines, not only CRCs) are compared after every step, so the first divergence is found to the field. Until N.6 the "C core" is a hybrid: the Lua code of the subsystems not yet ported runs over the ported C subsystems through the Lua binding, which serves the Lua API of the replaced modules (N.1: `game/images/native.lua`, [phase 3N](../phase3n.md)).

   | Stage | Moves to C | Checked by |
   |---|---|---|
   | N.0 | library skeleton, local and build-server builds of all four platforms, integer helpers, the three generators, the `.kpd`/`.kpm` loaders | integer and generator vectors (D14); loaded data against the Lua tables |
   | N.1 | images, sprites, IScript (phase 2) | IScript and health-bar goldens; the 192 draw-list frames of the 64 missions |
   | N.2 | unit creation, pools, lists, the initial state | the initial states of the 64 missions in every category |
   | N.3 | orders, dispatchers, timers, the command queue | the order scenarios at every step; `test_orders_native` (535 + 19 cases) |
   | N.4 | movement, paths (`kp_path` merged, reading the state directly), collisions, the spatial index | every movement scenario at every step, path records byte for byte |
   | N.5 | visibility, fog, detection, creep | tile flags and sprite visibility at every step |
   | N.6 | saves, the dump, the draw list, events, the viewer's play mode, the mission manager; the Lua core and `kp_path` removed | the whole CI, the macOS and HTML5 autotests, the play check, the tick bench on the Fold7 (§10) |

## Failure modes

- **A C crash or memory corruption takes the app down**, not just a Lua error. Mitigations:
  - debug builds check record indices with assertions;
  - desktop CI runs every scenario and all 64 missions under AddressSanitizer and UndefinedBehaviorSanitizer (`kp_replay`);
  - the step has no allocation and no pointers to freed memory.
- **An unported branch of the original** (now `error("... not ported")`) becomes `kp_fail(game, ...)`. It unwinds to the step boundary, marks the game failed, and the binding raises a Lua error with the same text. A failed game is not stepped again.
- **A divergence from the original** is caught by the same oracle (golden data per step) and, until N.6, by the per-step comparison with the Lua core.
- **The build server is unavailable:** only engine builds stop. Local builds, tests and device benchmarks go on.

## What changes in formats

- **Saves start over at version 1:** an explicit little-endian field-by-field encoding of the C state with an FNV fingerprint. The Lua core's saves are not read; no saves have shipped, so the format is not versioned against them (the user's decision of 2026-10-13).
- **Unchanged:**
  - the golden data (`index.lua`: CRCs of the category lines);
  - the scenarios (`tests/scenarios/*.json`);
  - the emulator tools;
  - `.kpd`/`.kpm`;
  - the converter.
- **Removed after N.6:**
  - the Lua simulation under `game/`;
  - the tests of its internals; tests that check through dumps stay;
  - `native/kp_path` as a separate extension;
  - `game/pathing/native.lua`.

  The PUC Lua run stays for the Lua code of presentation and UI.

## Consequences

- Engine builds keep going through the build server; local development does not.
- The plan gains phase 3N (`IMPLEMENTATION_PLAN.md`). D7 and D19 are revised. The §10 tick budget of phase 3 is accepted at N.6.
- Profiling the Lua interpreter (`tools/lua/tick_bench.lua`) gives way to native profilers (Instruments, simpleperf) and `kp_bench`.
