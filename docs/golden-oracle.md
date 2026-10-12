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

The manifest is JSON because Python tools and pytest consume it. The Lua runtime has no JSON
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
python3 -m pytest -q converter/tests/test_golden_m0e.py
luajit tests/lua/run.lua tests.lua.test_golden_m0e
```

The exporter needs `docs/research/evidence/`, which is gitignored. It checks that:

- every evidence file names the profile executable;
- each probe script and its recorded source dependencies hash-match `tools/research`;
- every case passed with `actual == expected`;
- every atlas address and ledger row exists.

Without the private emulator outputs, the committed emulator-derived files are kept as they are, and the manifest is rebuilt from their recorded summary line. `tools/ci.sh` runs `--check` when the evidence directory exists.
