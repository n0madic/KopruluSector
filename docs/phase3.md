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
| 3.2 | Frame loop, orders, dispatchers, command queue | in progress: the frame loop, the order dispatchers, idle orders, the movement states of units at rest, unit timers, target acquisition, alliances, the AI and trigger counters, turns, the local selection, the select, Stop and Hold Position commands and orders are ported; `idle_marines` (61 steps) and `marine_commands` (91 steps) equal the original at every step. Next: more commands and their orders | `game/simulation/step.lua`, `commands.lua`, `game/units/movement.lua`, `timers.lua`, `targets.lua`, `order_dispatch.lua`, `selection.lua`, `game/ai/step.lua`, `game/triggers/clock.lua`, `tests/scenarios/` |
| 3.3 | Flingy movement, IScript unit opcodes | not started | |
| 3.4 | Paths: regions, local search, contours, blocked movement | not started | |
| 3.5 | Visibility upkeep, fog presentation | not started | |
| 3.6 | Viewer play mode: selection, orders, health bars | not started | |
| 3.7 | Save round trip, performance check | not started | |

## Oracle (3.0)

**State dumps.** `python -m sc_emulator campaigns --frames 3 --state 1,2,3 --out-root ~/koprulu-private/emu/states` (from `tools/research`, private venv) runs every campaign mission through the game's own start path and dumps the state at the entry of object steps 1–3.
- Step 1 is the call of `PrimeGameLoop 0x445520`: the state that `StartGame` leaves, before any unit, order or AI step.
- A dump holds the whole writable data section of `StarCraft.exe` (every pool, list head, counter and the RNG live there) and the tile flag and megatile heap arrays. About 160 KB compressed; 20 s per mission.

**Call traces.** `python -m sc_emulator mission NAME WARP --calltrace START,STOP[,WINDOWS]` (or `scenario SPEC --calltrace ...`) records every atlas function entered between two code addresses, nested by stack pointer, for WINDOWS windows in a row. The trace from `StartGame 0x443f40` to the first object step lists the native functions of mission start and their order; it drove the port of 3.1. The block hook is added when a window opens, so the translation cache is flushed then: Unicorn does not apply a new block hook to code it has already translated, and the traces before that fix missed functions such as `StepObjectSprite 0x418b20`.

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

**Scenarios (level C).** A spec in `tests/scenarios/<name>.json` names a base campaign map, the slot controllers, the units, the frame count and the player's inputs (selection boxes, hotkeys, right clicks at given frames; `tools/research/sc_emulator/scenario.py`). `converter/scenarios.py` rewrites the base CHK (OWNR and UNIT from the spec, THG2 sprites only, empty TRIG and MBRF); `python3 -m converter scenarios --data DIR` writes `generated/scenarios/<name>.kpm`. `python -m sc_emulator scenario <spec>` starts the base mission in the emulator, replaces the map at every `ChkLoadSections 0x4c1200` call with the same CHK, dumps the state before every object step, logs every executed game command at `RecordCommand 0x478830` (`commands.jsonl`, with the frame counter, the turn counter `0x672658` and the issuing player) and every change of the local player's interface selection at `SetLocalSelectionArray 0x49cab0` (`ui.jsonl`) for Koprulu to replay. The interface draws the selection circles at once; the select command it sends runs a few frames later. In single player the only commands without input are the sync commands 0x37.

**Comparison.** `python3 tools/state_check.py` starts each dumped mission in Koprulu with the dump's initial seed and compares the models; `--write-golden` stores the original's per-category CRCs in `tests/golden/oracle/initial_state/` and the equal missions in its `index.lua`, which `tests/lua/test_initial_state.lua` checks on every Lua backend.

`--step N`, `A-B` or `all` compares object steps: Koprulu runs the same steps (`tools/lua/state_model.lua` `runner`: PrimeGameLoop's two steps, RunGame's first turn, then per loop iteration the interface events, the turn, the frame count and the steps) and replays the run's `commands.jsonl` and `ui.jsonl` on the way; a mission reports its first differing step. With `--root ~/koprulu-private/emu/scenarios --step all --write-golden` the per-step CRCs and the command log of each scenario go to `tests/golden/oracle/scenarios/`, which `tests/lua/test_scenarios.lua` replays step by step on every Lua backend.

Fields the comparison leaves out:
- the image redraw bit (flag 0x01), which only drives the original's dirty rectangles;
- from step 3 on, the image bounds (+0x1c/+0x1e): the original's drawing refreshes them for the rows in view (`0x445de0`), so they depend on the camera; Koprulu's presentation refreshes them itself;
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

## Frame loop (3.2)

`game/simulation/step.lua` follows the frame of the game loop `0x444e30`: the turn's commands, the frame count, `StepGameObjects 0x445070`, then the trigger step `0x4be500`, each with the generator enabled (`0x4781c0`). `PrimeGameLoop 0x445520` runs two object steps before the first frame, without a frame count or a trigger step; the dumps 1 and 2 are those steps. The object step:
- `RecedeCreepTick 0x4b4010` with the receding pool's counters (`game/terrain/creep.lua`);
- `StepAi 0x48dd90` and `StepAiTimers 0x48df50` (`game/ai/step.lua`): the region cursor, the spending and town cursors and the under-attack reset of unit status 0x80 run in every game;
- every 100 steps `RefreshRememberedTerrainOffscreen 0x40af30` and `ResetTileVisibility 0x46c180`, after which the units reveal their sight again;
- `UpdateUnits 0x438ef0`: the order and secondary timer phases, `UpdateDisruptionWebCoverage 0x438cd0`, movement (`PrepareUnitMovement 0x43a5f0`, `ExecuteUnitMovement 0x42ad30` with the states Init, AtRest, Dormant and CheckIllegal, `IsPositionBlocked 0x42eab0`), the revealers' sight, sprite visibility and detection, then per unit `UpdateUnitTimers 0x43a320` (energy, shields, spell timers, acid spores), `DispatchMainOrder 0x439420`, `DispatchSecondaryOrder 0x439310` and the sprite's IScript; then the cloak list, Protoss power and the hit history slot;
- bullets and independent flingies (none yet), then the lone thingies.

The order dispatchers (`game/units/order_dispatch.lua`) carry the native function of every order of their three switches. Ported: Guard `0x41e700` (RandRange on source 29, then PlayerGuard, ComputerAI or GuardPost), PlayerGuard `0x41ee10` with `AcquireBestTarget 0x431c40` (`game/units/targets.lua`: seek ranges and their upgrade table, weapons, `CanAttackTarget 0x41da50`, `IsEnemyUnit 0x4290e0`, the candidate filters of `0x432280`), Nothing, and the original's empty orders. The trigger step (`game/triggers/clock.lua`) keeps its clocks and the elapsed seconds that the AI timers read.

**Turns and commands.** A loop iteration runs the turn (`TurnManagerStep 0x476c00`: its commands, then the turn counter), the frame count, then the object and trigger steps; RunGame runs one turn before the loop. `game/simulation/commands.lua` executes a turn's commands: the sync command 0x37 changes no game state; select 0x09 (`0x472fc0`), Stop 0x1a (`0x473440`) and Hold Position 0x2b (`0x4727c0`) are ported, with `IssueCommand 0x42a8b0` for unqueued orders and `CanIssueOrder` per selected unit; every other command raises with its handler from the executor's switch.

**Selection** (`game/units/selection.lua`): the local interface selection (`SelectUnitsLocally 0x49cf80`, `SetLocalSelectionArray 0x49cab0`, `SelectUnitIntoSlot 0x422eb0`, `AddSelectionCircle 0x423060`, `ShowSelectionHealthBar 0x417110` with the health-bar and circle allocators of phase 2, circle colours of `RefreshAllianceColors 0x41d7b0`); each player's command selection (`0x6a5ef0`), the control groups (`0x527140`) and the recent multi-unit selections stamped with the turn (`0x464360`, `0x4643c0`). The orders Stop (`0x428a10`) and Hold Position (`0x41e9b0`) stop the unit (`StopMovementAndFace 0x43a4e0`, `StopFlingy 0x40ce10`); Hold Position seeks with `AcquireRandomTarget 0x431b70`.

Alliances and shared vision come from `InitAlliancesAndVision 0x477610` (`game/simulation/start.lua`): every player allied with itself and with player 11, computers with each other (value 2), rescuable and neutral players with everyone, then each force's alliance and vision among its human players; a slot's force is its FORC entry plus one, 0 for empty slots. The initial states of all 64 missions also equal the original in these fields and in the step counters (category `step`).

**Status:** `tests/scenarios/idle_marines.json` (five Marines and an SCV of the human player) equals the original at all 61 dumped steps and `marine_commands.json` (the same units; the player box-selects the Marines, orders Stop, then Hold Position) at all 91 steps, in every compared field (`tests/golden/oracle/scenarios/`). The campaign missions stop at step 2 on contracts of later items: 59 at the AI (resource areas `0x497a20`/`0x496c80`, region updates of computer players `0x4b0ce0`), 5 at movement states of moving or flying units (`0x41a250`, `0x40cf50`).

Contracts the scenarios do not reach raise with their native address, among them: every order other than Guard, PlayerGuard, Nothing, Stop and Hold Position; queued (shift) commands (`0x429650`); replacing a local selection (`0x423000`); dead units in a selection; movement states other than 0, 1, 6, 9, 10 and 12, flingy steps (`0x40bcd0`) and moving a blocked unit (`0x42ce60`); unit collisions (`0x42ebf0`); scoring an attack candidate (`0x41fe60`, `0x48d2e0`, `0x4319d0`) and issuing the attack (`0x4202c0`); dying, hidden and subunit updates; the cloak list upkeep; Protoss power; spell timer expiries; computer players' AI; triggers, countdown timers and leaderboards; all game commands but sync, select, Stop and Hold Position.
