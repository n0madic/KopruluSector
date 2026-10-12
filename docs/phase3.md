# Phase 3 progress

Plan: `IMPLEMENTATION_PLAN.md` §7 phase 3 (core units, orders, movement, visibility). Started 2026-10-12, after the user approved the design (inline design document, 2026-10-12):
- native-shaped SoA state in `game/units`, `game/vision`, `game/ai`, composed by `game/simulation/game.lua`;
- a strictly sequential tick in the original's order;
- the full-game emulator as the oracle at three levels: function probes, the initial state of every mission, and per-frame scenarios.

## Work items

| # | Item | Status | Where |
|---|---|---|---|
| 3.0 | Oracle tooling: emulator state dumps, call traces, the state model, the comparison | done | `tools/research/sc_emulator/state.py`, `calltrace.py`, `tools/native_state.py`, `tools/lua/state_model.lua`, `tools/state_check.py` |
| 3.1 | Units from the CHK: pools, lists, creation, placement, completion, spatial index, counters, vision, detection, creep, requirements, initial orders, guard AI records | done: the initial state of all 64 missions equals the original | `game/units/`, `game/vision/`, `game/terrain/creep.lua`, `game/ai/guard.lua`, `game/simulation/start.lua` |
| 3.2 | Frame loop, orders, dispatchers, command queue | started: scenario oracle (CHK substitution, command log, per-step dumps), call trace of the idle object step | `converter/scenarios.py`, `tools/research/sc_emulator/scenario.py`, `tests/scenarios/` |
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

**State model.** `tools/native_state.py` decodes a dump into lines `<category> <key> <field>=<value> ...`; `tools/lua/state_model.lua` prints Koprulu's state in the same form. Pointers become record numbers (units 1..1700, sprites, images in the `game/images/core.lua` key space, order nodes, guard records `g<k>`); unit references inside the type-dependent union print as `u<n>`. Categories:
- global: RNG, counts, list tails, the cloaked-unit list head;
- list: unit lists in native order, including the per-player lists;
- unit: every CUnit field;
- order, order_free: queues and the free list;
- sprite and image, in row order;
- counter, pstat: per-type counts and the per-player statistics;
- spatial: both bound arrays;
- player, guard: controllers, shared vision, guard AI records;
- tiles: the tile flag DWORDs;
- terrain: MTXM words, the creep engine's backup words and border bytes, the display megatiles.

**Scenarios (level C).** A spec in `tests/scenarios/<name>.json` names a base campaign map, the slot controllers, the units, the frame count and (later) the inputs. `converter/scenarios.py` rewrites the base CHK (OWNR and UNIT from the spec, THG2 sprites only, empty TRIG and MBRF); `python3 -m converter scenarios --data DIR` writes `generated/scenarios/<name>.kpm`. `python -m sc_emulator scenario <spec>` starts the base mission in the emulator, replaces the map at every `ChkLoadSections 0x4c1200` call with the same CHK, dumps the state before every object step and logs every executed game command at `RecordCommand 0x478830` (`commands.jsonl`, with the frame counter and the issuing player) for Koprulu to replay. `tools/state_check.py --root .../emu/scenarios` compares a scenario's initial state; the first spec, `idle_marines`, starts equal. In single player the only commands without input are the sync commands 0x37.

**Comparison.** `python3 tools/state_check.py` starts each dumped mission in Koprulu with the dump's initial seed and compares the models; `--write-golden` stores the original's per-category CRCs in `tests/golden/oracle/initial_state/` and the equal missions in its `index.lua`, which `tests/lua/test_initial_state.lua` checks on every Lua backend.

Fields the comparison leaves out:
- the image redraw bit (flag 0x01), which only drives the original's dirty rectangles;
- the resource-area tag (+0xd8) of resources: `GenerateResourceAreas 0x497a20` needs the region graph and connected distances (3.4);
- `path` (+0x100), always null before units move.

## Initial state (3.1)

`game/simulation/start.lua` follows `StartGame`/`InitGameWorld` for everything that builds simulation state: guard AI pool, fog init with the 12 vision templates, image and sprite pools, the cursor thingy, order pool, counters, acquisition ranges, unit pool, strength tables, the MTXM tile flags and edge strips, THG2 doodads, MASK, the override sections in dispatch order (after THG2, as in the original), and the UNIT section with record admission, creation, properties and the post-load visibility pass.

Unit creation follows the native chain `CreateMapUnit 0x469460 → CreateUnit 0x424900 → AllocateUnit / InitializeUnit / InitializeUnitType → CompleteUnitWithPlacement 0x424db0 → AttachGuardRecord 0x4b2010`, with flingy init, the IScript walking-speed sampling, speed and energy upgrades, the spatial index (`0x4195a0`, `0x419820`, queries `0x4224c0`/`0x422330`), placement (`0x47dc30` direct candidate, footprint `0x42ed50`, contours `0x48b780`), completion accounting, Show with unit vision (`0x46bbb0` and its four callbacks), sprite visibility, overlap flags, initial orders through the native order pool, strength (`0x4a98e0` with its `sqrt`), damage overlays (`0x422dd0`, RNG source 8) and permanent cloak with detection (`0x41d200`, `0x4325a0`, `0x4316b0`).

**Vision templates** equal the original's generators for all 12 sights at strides 64, 128 and 256 (`tests/lua/test_vision.lua`, CRCs from the recorded original-code evidence).

**Requirements.** `EvaluateDatRequirements 0x407570` with the order (`0x407bb0`) and technology-use (`0x407f90`) wrappers, and the unit production table lookup `0x4b3850`. The bytecode tables are built into `StarCraft.exe`; the converter copies them into `dat.kpd` (`RQOR`, `RQRS`, `RQTU`, `RQUP`, `RQUN`, [kpd format](formats/kpd.md)).

**Creep** (`game/terrain/creep.lua`): the creep engine's mutators, border shapes and ranks, initial placement `0x4c0aa0` and spread `0x4c0e60` (game RNG source 26), and the game callbacks (display megatile, tile flags, receding buckets). Creep tile variants come from the CRT `rand()` seeded with the game seed at start; the call trace of a Zerg start shows no other CRT draw before creep. Terrain height (`0x445690`) reads the tile word under the creep (the backup map), which decides line of sight next to creep.

**Status:** all 64 missions equal the original in every compared field (`tests/golden/oracle/initial_state/`). Each step was found by the comparison and fixed in the native code path, for example:
- the dump point (the first object step runs inside `PrimeGameLoop`);
- the carrier and reaver hangar capacity upgrades (43 and 36; the decompiler's branch order was misleading, the disassembly settles it);
- terrain height under creep.

Contracts that the initial states do not reach raise a named error instead of doing nothing: hallucinated map units (`0x430a00`), ownership transfer of addons (`0x426ed0`), additional placement points (`0x4928f0`), building ghosts of fog (`0x417b10`), selection-dependent sprite changes, paths.
