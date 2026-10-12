# Phase 3 progress

Plan: `IMPLEMENTATION_PLAN.md` §7 phase 3 (core units, orders, movement, visibility). Started 2026-10-12, after the user approved the design (inline design document, 2026-10-12):
- native-shaped SoA state in `game/units`, `game/vision`, `game/ai`, composed by `game/simulation/game.lua`;
- a strictly sequential tick in the original's order;
- the full-game emulator as the oracle at three levels: function probes, the initial state of every mission, and per-frame scenarios.

## Work items

| # | Item | Status | Where |
|---|---|---|---|
| 3.0 | Oracle tooling: emulator state dumps, call traces, the state model, the comparison | done | `tools/research/sc_emulator/state.py`, `calltrace.py`, `tools/native_state.py`, `tools/lua/state_model.lua`, `tools/state_check.py` |
| 3.1 | Units from the CHK: pools, lists, creation, placement, completion, spatial index, counters, vision, detection, initial orders, guard AI records | in progress: 8 of 64 missions equal the original | `game/units/`, `game/vision/`, `game/ai/guard.lua`, `game/simulation/start.lua` |
| 3.2 | Frame loop, orders, dispatchers, command queue | not started | |
| 3.3 | Flingy movement, IScript unit opcodes | not started | |
| 3.4 | Paths: regions, local search, contours, blocked movement | not started | |
| 3.5 | Visibility upkeep, fog presentation | not started | |
| 3.6 | Viewer play mode: selection, orders, health bars | not started | |
| 3.7 | Save round trip, performance check | not started | |

## Oracle (3.0)

**State dumps.** `python -m sc_emulator campaigns --frames 3 --state 1,2,3 --out-root ~/koprulu-private/emu/states` (from `tools/research`, private venv) runs every campaign mission through the game's own start path and dumps the state at the entry of object steps 1–3.
- Step 1 is the call of `PrimeGameLoop 0x445520`: the state that `StartGame` leaves, before any unit, order or AI step.
- A dump holds the whole writable data section of `StarCraft.exe` (every pool, list head, counter and the RNG live there) and the tile flag and megatile heap arrays. About 160 KB compressed; 20 s per mission.

**Call traces.** `python -m sc_emulator mission NAME WARP --calltrace START,STOP` records every atlas function entered between two code addresses, nested by stack pointer. The trace from `StartGame 0x443f40` to the first object step lists the native functions of mission start and their order; it drives the port of 3.1.

**State model.** `tools/native_state.py` decodes a dump into lines `<category> <key> <field>=<value> ...`; `tools/lua/state_model.lua` prints Koprulu's state in the same form. Pointers become record numbers (units 1..1700, sprites, images in the `game/images/core.lua` key space, order nodes, guard records `g<k>`); unit references inside the type-dependent union print as `u<n>`. Categories: global (RNG, counts, list tails), list (unit lists in native order), unit (every CUnit field), order, order_free, sprite and image (row order), counter, pstat (per-player statistics), spatial (both bound arrays), player, guard, tiles.

**Comparison.** `python3 tools/state_check.py` starts each dumped mission in Koprulu with the dump's initial seed and compares the models; `--write-golden` stores the original's per-category CRCs in `tests/golden/oracle/initial_state/` and the equal missions in its `index.lua`, which `tests/lua/test_initial_state.lua` checks on every Lua backend.

Fields the comparison leaves out:
- the image redraw bit (flag 0x01), which only drives the original's dirty rectangles;
- the resource-area tag (+0xd8) of resources: `GenerateResourceAreas 0x497a20` needs the region graph and connected distances (3.4);
- `path` (+0x100), always null before units move.

## Initial state (3.1)

`game/simulation/start.lua` follows `StartGame`/`InitGameWorld` for everything that builds simulation state: guard AI pool, fog init with the 12 vision templates, image and sprite pools, the cursor thingy, order pool, counters, acquisition ranges, unit pool, strength tables, the MTXM tile flags and edge strips, THG2 doodads, MASK, the override sections in dispatch order (after THG2, as in the original), and the UNIT section with record admission, creation, properties and the post-load visibility pass.

Unit creation follows the native chain `CreateMapUnit 0x469460 → CreateUnit 0x424900 → AllocateUnit / InitializeUnit / InitializeUnitType → CompleteUnitWithPlacement 0x424db0 → AttachGuardRecord 0x4b2010`, with flingy init, the IScript walking-speed sampling, speed and energy upgrades, the spatial index (`0x4195a0`, `0x419820`, queries `0x4224c0`/`0x422330`), placement (`0x47dc30` direct candidate, footprint `0x42ed50`, contours `0x48b780`), completion accounting, Show with unit vision (`0x46bbb0` and its four callbacks), sprite visibility, overlap flags, initial orders through the native order pool, strength (`0x4a98e0` with its `sqrt`), damage overlays (`0x422dd0`, RNG source 8) and permanent cloak with detection (`0x41d200`, `0x4325a0`, `0x4316b0`).

**Vision templates** equal the original's generators for all 12 sights at strides 64, 128 and 256 (`tests/lua/test_vision.lua`, CRCs from the recorded original-code evidence).

**Status:** 8 of 64 missions equal the original in every compared field (`tests/golden/oracle/initial_state/index.lua`). The others stop at a contract that is not ported yet, with an explicit error naming it:
- creep (`0x4c0aa0`): Zerg buildings in 25 missions;
- initial burrowed units (`0x46a420` with the tech requirement check `0x407f90`);
- the placement ring search (`0x47e340`) for units placed on occupied or unwalkable spots;
- initial hangar units (`0x46a180`);
- addon and Nydus relations (`0x469810`, `0x4698e0`).
