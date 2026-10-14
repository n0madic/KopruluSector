# Golden oracle: M0E reference cases

`tests/golden/oracle/` holds versioned original observations for the 1.17.0.1 profile
(`defs/profile/bw-1.17.0.1.json`, executable SHA-256 `94dea448…6ed0`). The M0E milestone
cases live in `tests/golden/oracle/m0e/`. Everything there is generated; do not edit it by hand.

## Files

| File | Content |
|---|---|
| `manifest.json` | One entry per M0E sub-stage E0–E4. Each case lists its sources, inputs, outputs to compare, status, scope and links. |
| `index.lua` | List of the Lua data files, so Lua tests can find them without listing a directory. |
| `e0_clock_emulator.lua` | Full-game emulator, terran-m01 with no input: per-frame RNG state and elapsed-time cadence over 3000 frames. |
| `e1_<scene>.lua` | One file per campaign Town scene: per-worker order/stage/cargo runs, positions after every callback, resource amounts, stock, deposits, WorkerAI service frames, resource areas and RNG. |
| `e2_<profile>.lua` | Paid AI prerequisite chain (`paid`, `cash399`, `academy_unavailable`). Holds the initial, change-point and final states: script PC/wait, elapsed time, stocks, supply, raw and completed counts, spending heap, demand/budget, Town and unit records, plus requirement-gate returns. |
| `e2_spending_leaf.lua` | Original leaf vectors for the spending heap, demand percentages, Town request insertion and the unit-limit predicate. |
| `e3_<family>.lua` | The recorded output tuple of every construction/gas case, with one field label per value. |
| `e3_extractor_completion_emulator.lua` | Full-game emulator, Zerg02: Extractor conversion, return from completion stamping, completed state, extractions and gas deliveries. |

The manifest is JSON because Python tools and the converter's unittest suite consume it. The Lua runtime has no JSON
parser, so the data that Lua tests compare against is exported as Lua table literals. They are
loaded with `loadstring` on LuaJIT, the LuaJIT interpreter, PUC Lua 5.1 and in Defold, where
`tests/golden` is a custom resource. Unit references use the native CUnit pool index
(`(address − 0x5da3a0) / 0x150 + 1`), so free-list order stays comparable.

## Case fields

- `status` says how the observation was made:
  - `original-isolated-complete`: original functions returned completely on declared inputs.
  - `original-isolated-prefix`: execution stopped at a recorded boundary, so only the reached part is established.
  - `emulator-observed`: seen in the full-game emulator, within the limits in [full-game-emulation.md](research/full-game-emulation.md).
  - `static`: read from the instructions only (atlas).
  - `implementation-continuity`: there is no original oracle. The implementation's own uninterrupted run is the reference.
- `inputs.kind` is one of `authentic-records-with-declared-fixture`, `synthetic-fixture`, `full-mission` or `none`. The remaining `inputs` fields are the exact declared inputs: map/record hashes, units, cash, controller/race/script fixtures, cadence and RNG seed.
- `observed` holds the outputs to compare. E3 tuples carry `actual_fields` labels and `actual_source`, the probe line that reads them.
- `evidence` gives the file, its SHA-256, and the case index with the canonical-JSON hash of the case. `emulator_source` gives the hashes of the private emulator output and of its driver.
- `scope` and `missing` state what the case does not establish.
- `ledger` lists the completion-ledger rows R01–R37: the rows that cite the case's research document, plus any explicitly added rows. `atlas` lists the function entries (address, package, name) that implement the contract.
- `golden` and `golden_sha256` give the data files and their hashes.

## Cases

- **E0.** RNG leaf vectors (`../rng_game.lua`), the static frame order and slow-service counters, and static pool/list functions. The emulator shows elapsed time advancing every 16 frames, with RNG checkpoints.
- **E1.** The four natural WorkerAI mining scenes, with conservation checked after every callback.
- **E2.** The three paid-chain profiles and the spending leaf vectors. There is no original observation of the E1+E2 integration scenario (see the notes on the E2 stage).
- **E3.** Six construction/gas families (62 recorded cases) and the emulator's Extractor completion.
- **E4.** Save/load continuity, which is checked against the implementation's own uninterrupted run. The manifest names the callbacks where each required state holds in the recorded cases: walking, carrying, construction, hidden Train child, VM wait and warp.

## Extractor completion trace

The Extractor probe stops before the Windows import `GetLastError` (`0x4d7466`), which CRT `rand`
reaches during final creep stamping. `tools/oracle/emu_m0e_extractor.py` obtains the missing
observation in the full-game emulator on the same authentic record: the Zerg02 geyser (UNIT
record 14, type 188 at (960, 256), amount 4851) and player 3. It sends window messages only. The
manifest's `probe_comparison` shows that the conversion state equals the probe's (hp 19488, build
time 599). Completion follows 607 frames later, as the probe's 607th continuation callback does,
and the completed state matches (hp 192000, order 23, amount 4851). `0x43c220` returns after 16
`GetLastError` calls. Each later delivery adds 8 gas and takes 8 from the geyser. One input
difference remains: the emulator's Drone carries no item.

## Regenerating

```sh
# Optional, private: re-run the emulator trace (about 4 minutes)
~/koprulu-private/venv/bin/python tools/oracle/emu_m0e_extractor.py
# Rebuild the manifest and data; --check only compares
python3 tools/oracle/export_m0e_cases.py [--check] [--emu-root ~/koprulu-private/emu]
python3 -m unittest converter.tests.test_golden_m0e
luajit tests/lua/run.lua tests.lua.test_golden_m0e
```

The exporter needs `docs/research/evidence/`, which is gitignored. It checks that:

- every evidence file names the profile executable;
- each probe script and its recorded source dependencies hash-match `tools/research`;
- every case passed with `actual == expected`;
- every atlas address and ledger row exists.

Without the private emulator outputs, the committed emulator-derived files are kept as they are, and the manifest is rebuilt from their recorded summary line. `tools/ci.sh` runs `--check` when the evidence directory exists.

# Golden oracle: full-game emulator traces

`tests/golden/oracle/missions/` holds the reference traces of plan §0.1 for whole missions. They are exported from the private outputs of the full-game emulator ([full-game-emulation.md](research/full-game-emulation.md)) by `tools/oracle/export_emulator_traces.py`.

| File | Content |
|---|---|
| `manifest.json` | Per catalog mission, the 3,000-frame run without input after Start (`missions/<run>`). See below. |
| `m1_wasteland.lua` | The M1 reference, a scripted victory in `terran/terran01` without cheats (`h03/terran-wasteland`): every per-frame digest (5,632 frames), the input log (53 events), the recorded events and the outcome (result 3 at frame 5,586). |
| `terran01_no_input.lua` | Every per-frame digest and sound request of the catalog Wasteland run without input. |
| `index.lua` | The Lua data files. |

**Manifest contents per run:**
- catalog mission, CD and warp token;
- initial CRT seed and game speed;
- SHA-256 of the trace and sound logs;
- the digest every 500 frames plus the last frame;
- sound request counts per hooked function;
- the hashes and sizes of the captured PNG frames.

The M1 entry adds the outcome and the input-log hash. `lua_sha256` records the SHA-256 of each Lua data file.

**Game pixels and full traces.** Game pixels are not redistributed: PNGs are referenced by hash. Full traces of the other runs stay private; the manifest pins them by hash.

**Digest.** Taken at `0x444f76` after the trigger scheduler, once per frame. Its fields (frame, RNG, unit counts, unit hash, countdown, elapsed seconds, trigger waits, minerals, gas, trigger results, virtual time) are specified in the manifest's `digest` entry. The simulation computes the same digest from phase 3 to compare against these traces. Virtual time `t` and UI cadence are emulator properties, not game state.

**Regenerating:**

```sh
(cd tools/oracle && python3 export_emulator_traces.py [--check] [--emu-root ~/koprulu-private/emu])
```

Without the private outputs, `--check` verifies the committed files against each other: the manifest is complete and every Lua data file matches its hash in `lua_sha256`. `tools/ci.sh` always runs it. The Lua suite `tests/lua/test_golden_missions.lua` loads both traces on every backend and in Defold, and checks frame continuity and the recorded outcome.

# Golden oracle: state-model goldens

`tests/golden/oracle/initial_state/` (the start of each of the 64 catalog missions) and `tests/golden/oracle/scenarios/` (every object step of each scenario of `tests/scenarios/`) hold the CRC-32 of each category of the original's state model, written by `tools/state_check.py --write-golden` from the full-game emulator's state dumps ([phase 3](phase3.md), [phase 4](phase4.md)). `tests/lua/test_initial_state.lua` and `tests/lua/test_scenarios.lua` check Koprulu's `game:dump_crcs` against them on every runtime, `kp_replay` in CI.

The categories are those of `tools/native_state.py` and `native/kp_core/src/core/kp_dump.c`: the unit, order, path, sprite, image, counter, spatial, player, selection, guard, tile and terrain records of phase 3; since phase 4 also `bullets` and `bullet` (the Bullet pool), `ai_region` (the computer players' AI region records that differ from their start), `ai_reach` (the military reachability cache), `ai_player` (the PlayerAi fields modelled so far) and `power` (the psi providers in list order, head `0x665b54`, next `+0xfc`; a Pylon's previous link `+0xf8` overlays its rally point, which both sides leave at 0, and its Psi field sprite `+0xd0` is the union word `s<sprite>`). From the third step on, the fields the original's drawing changes are left out on both sides: the image bounds (`0x445de0`) and the unit's minimap notices: the blink counter `+0x8a` (`0x46e490`), its color `+0x8b` and the silence of the under-attack notices `+0x87` (`0x446f00`, the camera and the idle loop `0x446ff0`).

A dump made before a category existed lacks it; rerun the emulator for that scenario (`python -m sc_emulator scenario SPEC`) before writing its golden.

# Golden oracle: original-code hashes of runtime modules

Phases 2 and 4 check runtime code against the original 1.17 functions running in Unicorn: a probe and a Lua harness run the same scenario and fold the state or the bitmap into the same 32-bit hash, `h = (h·31 + v) mod 2^32`. The golden files hold the scenario's parameters and the hashes, no retail bytes.

| File | Probe | Lua test | Content |
|---|---|---|---|
| `iscript/cases.lua` | `tools/research/probe_native_iscript_117.py` | `tests/lua/test_iscript_golden.lua` (harness `tests/lua/iscript_trace.lua`) | Per image animation of the retail script (2,917) and per synthetic program: completed frames and the hash of the image, sprite, RNG and event state after each of 80 frames of `UpdateThingies` |
| `health_bar/cases.lua` | `tools/research/probe_native_health_bar_117.py` | `tests/lua/test_health_bar.lua` | Per unit type with a health bar (207): the maximum energy from `0x434940` and one hash over its cases (hit point, shield, energy and timer levels, owners, hallucination, crops: 13,846) of the bitmap `DrawHealthBar 0x464d20` leaves |
| `damage_table.lua` | `tools/research/probe_damage_table_117.py` (on the DAT of an expansion game's state dump, `--state`) | `tests/lua/test_damage_table.lua` | Per upgrade level of the target's owner (0 and 3) and unit type (228): the CRC-32 of the hit points and shields `WeaponDamage 0x41dfc0` takes from a target of that type, for each of the 130 weapons at its base damage |

Both tests need `generated/data` and skip without it.

**Regenerating** (private venv, retail directory):

```sh
python tools/research/probe_native_iscript_117.py --data DIR --golden tests/golden/oracle/iscript/cases.lua --evidence docs/research/evidence/native-iscript-probes.json
python tools/research/probe_native_health_bar_117.py --data DIR --golden tests/golden/oracle/health_bar/cases.lua
python tools/research/probe_damage_table_117.py --exe DIR/StarCraft.exe --state EMU/scenarios/medic_heal/states/s00001.state --out tests/golden/oracle/damage_table.lua
```

`--rows FILE` (damage table) writes the uncompressed rows; `--case IMAGE ANIM` (IScript) and `--type T` (health bars) print the per-frame values or per-case hashes of one case for debugging.

**Renderer frames.** The phase 2 acceptance compares Koprulu's draw list and kernels with frames of the original renderer captured by the full-game emulator (`tools/research/sc_emulator/capture.py`, `tools/compose_capture.py`; [phase 2](phase2.md#acceptance-item-7)). The captures contain game pixels and stay private; `tools/ci.sh` compares them when `KOPRULU_CAPTURES` names their directory.
