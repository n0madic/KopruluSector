# ADR 0007: Native local path search (kp_path)

Status: accepted (phase 3, 2026-10-13); folded into the native simulation core at its stage N.4 (`native/kp_core/src/core/kp_local_search.c`, [ADR 0008](0008-native-simulation-core.md)); `native/kp_path` and its switches (`KP_PATH`) were removed with the Lua core at stage N.6. The user approved the design and the use of the Defold build server before implementation. Evidence: the 200-unit performance check ([phase 3](../phase3.md#performance-37)) and the comparison of the native and Lua searches on every search of every scenario (`KP_PATH=check`).

## Context

Plan §10 allows 4 ms per simulation step on the target mobile device, where LuaJIT runs as an interpreter, and no step above 8 ms. In the `bench_200` scenario the LuaJIT interpreter averaged 4.25 ms per step, with a maximum of 50.9 ms. The local path search (`LocalPathSearch 0x491f50`) costs 2–5 ms per call. A group that receives an order searches 12–16 paths in one step. Plan D19 and §7 phase 3 item 3 allow `kp_path` over the same data, with the same traversal order, once profiling shows the need.

## Decision

1. **Where state lives.** The game state stays in the Lua tables. `kp_path.search(game, context, next_region, gx, gy)` reads what it needs from them:
   - the terrain contours, copied once per contour table (keyed by the tables and `contours.version`);
   - the spatial index, copied at the start of each call;
   - the fields of the units it meets, read once per unit and call;
   - the region map, read straight from the mission's RGNS string.

   The search scratch is static C memory reset by each call, as the original's globals: nodes, heap, local edges, swept areas and region marks. Results go into the same context fields the Lua code writes. Saves leave out the Lua scratch (`rg`, `lg`, `visited`; save version 2), so a save does not depend on which search ran.
2. **Same results.** `native/kp_path/src/kp_path_core.c` ports `game/pathing/local_search.lua` line by line:
   - 64-bit integers with the explicit 16- and 32-bit wraps of `game/simulation/int.lua`;
   - the same heap, scan orders and tie rules.

   The Lua code stays the reference and the fallback.
3. **Selection.** `game/pathing/native.lua` finds the module:
   - in Defold, the extension `native/kp_path` registers it in every script context;
   - elsewhere, it loads `build/native/kp_path.so` (`tools/native/build.sh`) if present.

   `KP_PATH=lua` forces the Lua search. `KP_PATH=check` runs both on every search and raises at the first difference.
4. **Order.** The call is synchronous, at the same point of the step as the Lua search; there are no threads.

## Consequences

- Every engine build of the project goes through the Defold build server (`build.defold.com`, or `BUILD_SERVER`). `tools/spike.sh` with `SPIKE_NATIVE=1` runs the viewer on such an engine. `KOPRULU_NATIVE_ENGINE=1 tools/ci.sh` runs the Defold tests on it.
- The extension builds for macOS, iOS, Android and HTML5 (wasm). The stock engines of `tools/toolchain` lack it and run the Lua search.
- CI compares both searches on every search of all scenarios (LuaJIT with the JIT on). The interpreter run uses the native search, PUC Lua the Lua search.
- TODO: contour changes of buildings and doors (phase 6) must bump `contours.version`; region changes make parts of `rg` game state again.
