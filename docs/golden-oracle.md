# Golden oracle: JSON goldens, kp_oracle and kp_test (phase S)

> **`tests/golden/` is not in the repository.** It is git-ignored (`/tests/golden/` in `.gitignore`): the goldens are
> recorded from the original game by the private emulator and are not published. The C tests, the converter tests and
> `tools/ci.sh` that read them need the folder, so on a fresh clone either:
>
> 1. copy the folder from a private backup into `tests/golden/` (the maintainer keeps one next to `koprulu-private/`), or
> 2. regenerate it. With the original game installed under `koprulu-private/` (`full/app`, `retail/extracted`; the
>    emulator is `tools/research/sc_emulator`, run from `tools/research` with `koprulu-private/venv/bin/python`), record
>    the runs with `python -m sc_emulator ...` (see [full-game-emulation.md](research/full-game-emulation.md)), then write
>    the goldens with `tools/state_check.py --write-golden` (states, scenarios, campaign, chains, plays) and the
>    exporters `tools/oracle/export_*.py` (each one's `--check` only compares; without it it writes). The emulator's
>    outputs are read from `$KOPRULU_EMU`, else `koprulu-private/emu`.
>
> Until the folder is restored, `tools/ci.sh` runs in its public mode (on by itself without `tests/golden/oracle`, or
> with `KOPRULU_PUBLIC=1`; hosted CI uses it): the golden steps and `kp_replay` are left out, and the C and converter
> tests that need a golden or the private research evidence skip (`KP_TEST_PUBLIC`, `tools/research/scdata/tests/private.py`).

Every golden of `tests/golden/oracle/` is JSON. The C suites (`tests/c`, `kp_test`), the oracle tools (`kp_oracle`) and the Python
checkers read them. The writers: `tools/oracle/oracle_common.py` (`golden_json` renders a golden's data,
`write_golden` writes it, `golden_stale` checks it, `cases_module` builds a header-plus-`cases` golden),
`tools/state_check.py --write-golden`, the exporters `tools/oracle/export_*.py` (their `--check` fails when a golden
is stale), the research probes (`probe_damage_table_117.py`, `probe_native_health_bar_117.py`,
`probe_native_iscript_117.py`) and, until stage C7 of ADR 0019, the fixtures' `fixtures.py` (`tests/fixtures/kpf/expect.json`, committed since).

The JSON form (`golden_json`): an object's members in key order, numeric keys (the sparse `steps[n]` of the runs) by
value as their decimal text, the others by name; the top level one member per line, and a member whose value is a
list or object of containers one element per line, everything else compact; strings ASCII, other characters (a
string's bytes) `\u00XX` escapes.

The oracle library (`native/kp_core/tools/`): `kp_runner.h` (the games of the oracle, `KpOracleGame`: a mission
built from the original data (`kp_oracle_data`: the root when it is an install or a repack, else its
`generated/repack-single` or `generated/repack`, ADR 0018) by its key (`missions/<campaign>/<name>`, a test
scenario's `scenarios/<name>` from its CHK under `generated/scenarios/`) and started through `kp_host` with the
original's seed and the mission index of the data's catalog;
the replays of a golden, a replay text or the emulator's `commands.jsonl`/`ui.jsonl`; the runner object step by
object step with its event observer and its `after_objects` hook), `kp_oracle.h` (the goldens, the state CRCs and
fingerprints, the step differences, the trigger and AI trace lines, the golden runs with their checks, the chain
context `KP_CARRY`/`KP_TURN`, the campaign chains with each game's carry through `kp_campaign.h`), `kp_oracle_ui.h`
(an interface with its files, the console's colours over a tileset, the console over a replayed game where the input
log says the original ran its interface, the glue's trace streams) and `kp_json.h` (the JSON reader and writer of the
tools and the suites only). `kp_oracle` (`kp_oracle_main.c`, `kp_oracle_cli_ui.c`) has the subcommands
`start_state`, `traces`, `play`, `fog_levels`, `export_replay`, `hud_check`, `input_check`, `menus_replay`,
`room_replay`, `result_replay`, `score_replay` (a score file's `data` line is JSON) and `draw_list`
(`tools/compose_capture.py`'s records). The private emulator outputs are found under `oracle_common.EMU_ROOT`
(`KOPRULU_EMU`, else `koprulu-private/emu`). The Python checkers (`state_check.py`, `fog_check.py`, `ui_check.py`,
`input_check.py`, `menus_check.py`, `room_check.py`, `compose_capture.py`) run them through `tools/oracle_cli.py`
(`build/cmake/kp_oracle` or `KP_ORACLE`).

`kp_test` (`tests/c`) runs the C suites: `kp_test [SUITE[:TEST[@I/N]]]`, `kp_test -j N` (a sharded test's shards and
the other suites in parallel processes, the logs in `KP_TEST_LOGS`, default `build/c-tests`; `KP_TEST_EXEC` starts
the jobs with another program: `node build/cmake-wasm/kp_test.js` runs them in WebAssembly, the HTML5 proxy of the
deterministic core). `kp_t_eq`/`kp_t_ok` end the test, `kp_t_pcall` catches a game's failure, `kp_t_mine` takes a
shard's items, `kp_t_read`/`kp_t_has`/`kp_t_golden`/`kp_t_game` release what they made at the test's end, a game's
C failure fails its test (the game's failure handler is the test's). The suites: the oracle suites (`test_rng`,
`test_initial_state`, `test_scenarios`, `test_damage_table`, `test_save`, `test_references`, `test_orders_native`,
`test_requirements`, `test_aiscript`, `test_campaign`, `test_images` with the IScript goldens), the loaders over the
fixtures and the data (`test_data`, `test_ui_data`), the core (`test_kp_core`, `test_fog`), the interface
(`test_kp_ui`, `test_hud`), the presentation (`test_present`) and the application's session (`test_session`). The
build: `CMakeLists.txt` (`cmake -S . -B build/cmake -G Ninja && cmake --build build/cmake`; `-DKP_SANITIZE=ON` ASan
and UBSan; `emcmake` for wasm under node with the host's files, `NODERAWFS`), every source also checked as C++11
(ADR 0008).

# Golden oracle: M0E reference cases

`tests/golden/oracle/` holds versioned original observations for the 1.17.0.1 profile
(`defs/profile/bw-1.17.0.1.json`, executable SHA-256 `94dea448…6ed0`). The M0E milestone
cases live in `tests/golden/oracle/m0e/`. Everything there is generated; do not edit it by hand.

## Files

| File | Content |
|---|---|
| `manifest.json` | One entry per M0E sub-stage E0–E4. Each case lists its sources, inputs, outputs to compare, status, scope and links. |
| `index.json` | The data files, so the tests can find them without listing a directory. |
| `e0_clock_emulator.json` | Full-game emulator, terran-m01 with no input: per-frame RNG state and elapsed-time cadence over 3000 frames. |
| `e1_<scene>.json` | One file per campaign Town scene: per-worker order/stage/cargo runs, positions after every callback, resource amounts, stock, deposits, WorkerAI service frames, resource areas and RNG. |
| `e2_<profile>.json` | Paid AI prerequisite chain (`paid`, `cash399`, `academy_unavailable`). Holds the initial, change-point and final states: script PC/wait, elapsed time, stocks, supply, raw and completed counts, spending heap, demand/budget, Town and unit records, plus requirement-gate returns. |
| `e2_spending_leaf.json` | Original leaf vectors for the spending heap, demand percentages, Town request insertion and the unit-limit predicate. |
| `e3_<family>.json` | The recorded output tuple of every construction/gas case, with one field label per value. |
| `e3_extractor_completion_emulator.json` | Full-game emulator, Zerg02: Extractor conversion, return from completion stamping, completed state, extractions and gas deliveries. |

Unit references use the native CUnit pool index
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

- **E0.** RNG leaf vectors (`../rng_game.json`), the static frame order and slow-service counters, and static pool/list functions. The emulator shows elapsed time advancing every 16 frames, with RNG checkpoints.
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
koprulu-private/venv/bin/python tools/oracle/emu_m0e_extractor.py
# Rebuild the manifest and data; --check only compares
python3 tools/oracle/export_m0e_cases.py [--check] [--emu-root DIR]   # default: $KOPRULU_EMU, else koprulu-private/emu
python3 -m unittest tools.tests.test_golden_m0e
```

The exporter needs `docs/research/evidence/`, which is gitignored. It checks that:

- every evidence file names the profile executable;
- each probe script and its recorded source dependencies hash-match `tools/research`;
- every case passed with `actual == expected`;
- every atlas address and ledger row exists.

Without the private emulator outputs, the committed emulator-derived files are kept as they are, and the manifest is rebuilt from their recorded summary line. `tools/ci.sh` runs `--check` when the evidence directory exists.

# Golden oracle: full-game emulator traces

`tests/golden/oracle/missions/` holds the reference traces of whole missions. They are exported from the private outputs of the full-game emulator ([full-game-emulation.md](research/full-game-emulation.md)) by `tools/oracle/export_emulator_traces.py`.

| File | Content |
|---|---|
| `manifest.json` | Per catalog mission, the 3,000-frame run without input after Start (`missions/<run>`). See below. |
| `m1_wasteland.json` | The M1 reference, a scripted victory in `terran/terran01` without cheats (`h03/terran-wasteland`): every per-frame digest (5,632 frames), the input log (53 events), the recorded events and the outcome (result 3 at frame 5,586). |
| `terran01_no_input.json` | Every per-frame digest and sound request of the catalog Wasteland run without input. |
| `index.json` | The data files. |

**Manifest contents per run:**
- catalog mission, CD and warp token;
- initial CRT seed and game speed;
- SHA-256 of the trace and sound logs;
- the digest every 500 frames plus the last frame;
- sound request counts per hooked function;
- the hashes and sizes of the captured PNG frames.

The M1 entry adds the outcome and the input-log hash. `data_sha256` records the SHA-256 of each data file.

**Game pixels and full traces.** Game pixels are not redistributed: PNGs are referenced by hash. Full traces of the other runs stay private; the manifest pins them by hash.

**Digest.** Taken at `0x444f76` after the trigger scheduler, once per frame. Its fields (frame, RNG, unit counts, unit hash, countdown, elapsed seconds, trigger waits, minerals, gas, trigger results, virtual time) are specified in the manifest's `digest` entry. The simulation computes the same digest from phase 3 to compare against these traces. Virtual time `t` and UI cadence are emulator properties, not game state.

**Regenerating:**

```sh
python3 tools/oracle/export_emulator_traces.py [--check] [--emu-root DIR]   # the same default
```

Without the private outputs, `--check` verifies the committed files against each other: the manifest is complete and every data file matches its hash in `data_sha256`. `tools/ci.sh` always runs it. `tools/tests/test_golden_missions.py` loads both traces and checks frame continuity and the recorded outcome.

# Golden oracle: state-model goldens

`tests/golden/oracle/initial_state/` (the start of each of the 64 catalog missions, with the objectives its briefing set), `tests/golden/oracle/scenarios/` (every object step of each scenario of `tests/scenarios/`) and `tests/golden/oracle/runs/` (every 20th step of campaign missions run idle for 2,000 steps) hold the CRC-32 of each category of the original's state model, written by `tools/state_check.py --write-golden` from the full-game emulator's state dumps ([phase 3](phase3.md), [phase 4](phase4.md), [phase 5](phase5.md)); the scenarios and runs with triggers also keep the trigger trace (each action run, with its result); all 64 campaign runs are kept since phase 7. `tests/c/test_initial_state.c` and `test_scenarios.c` check Koprulu's state CRCs (`kp_state_crcs`) and trace against them on the host, under ASan and in wasm, `kp_replay` in CI (its replays written by `kp_oracle export_replay`).

The categories are those of `tools/native_state.py` and `native/kp_core/src/core/kp_dump.c`: the unit, order, path, sprite, image, counter, spatial, player, selection, guard, tile and terrain records of phase 3; since phase 4 also `bullets` and `bullet` (the Bullet pool), `ai_region` (the computer players' AI region records that differ from their start), `ai_reach` (the military reachability cache), `ai_player` (the PlayerAi fields modelled so far) and `power` (the psi providers in list order, head `0x665b54`, next `+0xfc`; a Pylon's previous link `+0xf8` overlays its rally point, which both sides leave at 0, and its Psi field sprite `+0xd0` is the union word `s<sprite>`)); phase 5 adds the trigger categories and `resources` ([phase 5](phase5.md)); phase 6 adds `economy`: per player the unit availability `0x52655c`, the techs available and researched, the upgrade levels and maxima (the original's ids, then the expansion's from 24 and 46, in arrays of their own), the research and upgrade in-progress bits and the AI's available budget (PlayerAi `+0xc/+0x10`, which every delivery of cargo raises) ([phase 6](phase6.md)). Phase 7 adds `ai_lists` (the PlayerAi demand, spending heap, attack roster, defense lists, build limits, medic list and gencmd area), `ai_script` (the script pool: the active slots in list order, the free list, the shared return PC), `ai_town`, `ai_worker`, `ai_building`, `ai_military`, `ai_area` (the resource areas, their used count and refresh countdown, the defense anchors), `ai_resource` (a resource's area and claim), `guard_time` (a guard's timestamp `+0x1c`) and the diagnostic `rng_sources` (the draws per RNG source); the scenarios and runs with AI scripts also keep the AI trace (each opcode the interpreter `0x493a60` dispatches: step, player, slot, binary, PC, opcode; the emulator's `ai.jsonl`) as a count and a CRC ([phase 7](phase7.md)). From the third step on, the fields the original's drawing changes are left out on both sides: the image bounds (`0x445de0`) and the unit's minimap notices: the blink counter `+0x8a` (`0x46e490`), its color `+0x8b` and the silence of the under-attack notices `+0x87` (`0x446f00`, the camera and the idle loop `0x446ff0`).

Phase 9E's ready rooms, result dialogs and score screens: `tools/room_check.py --root DIR [--kind room|result|score]` (the emulator's venv) replays the establishing text and the ready room of every game of the chains recorded with the glue's log (`$KOPRULU_CAPTURES/../chains-ui`) through kp_ui (`kp_oracle room_replay`): the room opens over the game's mission at the original's tick, kp_ui's timers run at the original's passes, the input and the network turn steps at the original's ticks, and the UI timers (set, deleted, fired, by owner BIN and record), the briefing VM's actions, the movies begun, the flic buttons' and portraits' frames, the credits' pages and the glue captures (the screen in palette indices, the cursor's rectangle left out, and the screen's palette while the room's movies play) must equal the original's. The original's clock is given where its GetTickCount moves inside a call: each SetUiTimer's tick, each movie continuation's (the clock SmackWait compared, uilog.py's `wait`), each credits page's deadline, the first page's tick. The menus' CRT states before each draw (the portraits' variants) are compared too, and a portrait variant's missing movie still counts as an attempt (SVidPlayBegin logs its name).

The same tool replays each game's result dialog (`--kind result`, `kp_oracle result_replay`): `wmission.bin` or `lmission.bin` opens at the original's tick over the mission's tileset palette and its in-game blend table; its timers, the exit code its answer leaves (chain.json's `game_exit`) and its pixels in the modeless captures must equal the original's. The dialog's opaque pixels are compared strictly; its translucent border colour must be a value of the blend row, because the original blends the dialog into the screen bitmap at every repaint and the surface keeps the blend over the game view's last redraw there, which no capture records (the count over the view the dialog first blended over is printed). And each score screen (`--kind score`, `kp_oracle score_replay`) opens over the data the original read (uilog.py's `score` and `elapsed` records): its timers (the count-up, the slides) and its glue captures must equal the original's.

The glue's menus (phase 9E, E4): `python -m sc_emulator menus` drives the original from the main menu through the game mode, the login with a new ID, the campaign and episode menus and the mission history into a campaign's first room by input (`menus.py`, the history step with the progress it needs), recording the input log, the glue's records and the captures (from the title screen, `rez\titledlg.bin`); `tools/menus_check.py` replays the input through kp_ui's glue (`kp_oracle menus_replay`: the title opened at the original's tick, its loading text's blink at the original's timer passes until RunGlueLoop's DestroyTitleDialog deleted its timer, then the main menu at the original's tick; each room's records numbered from its start) and compares the screens (the title's palette too), the popups and the timers. The room oracle also checks the campaign's progress the profile keeps after each game.

The in-game menus (E5) are two test scenarios, `game_menus` (the game menu, objectives, help, options with the sound, speed and video dialogs, the tips, the end-mission confirmations) and `game_save` (the save dialog with a typed name, the wait box, the load dialog, the delete request); `save_effects` and `save_ai_records` save through the menus in a fight and after an AI record was freed, for SaveGameFile's mutations (the dropped reference to a dying unit, the free records' zeroed payload), recorded with the modeless dialogs' captures (`uiscreen.py`: a capture at each OpenModelessDialog generation). `tools/ui_check.py --modeless TICK:FRAME` compares each capture with kp_ui's composition of the modeless dialog over the backdrop and the console: opaque pixels strictly, a translucent pixel when the blend row maps the world's index under it to the capture's, and the console art outside the dialog's own pixels is not compared. Their state goldens keep a marker where the menu paused the game (`menu_pause`, the interface's call 6 in `hud_log.py`), and the game's state equals the original's at every step, the save's included: SaveGameFile `0x498d20` changes the game it saves (the units' spatial round trip and their overlap flag `0x40000`, the Pylons' relink, the selection overlays, the dead references, the AI pools' free records; `kp_save_effects.c`), and the save's time stamp is masked in the input oracle.

Phase 9D adds "critter": PlayUnitSelectSound `0x446c30` killed a critter selected thirteen times in a row (the unit; the emulator hooks the KillUnit call at `0x446db9`; `critter_annoyed`). The input oracle (`tools/input_check.py`) counts a local selection as the interface's only right after an input event other than the idle event, after a command or after another of its selections: a step that takes several units out of the selection one by one after an idle event is the game's.

Phase 6 adds two kinds of interface events to the scenarios' logs (`ui.jsonl`, kept in their goldens and in the native replays' KPRP text): "crt", a draw of the CRT `rand` outside the creep tile callback after the game's seed (the unit voices' choice; the creep tile art draws from the same generator), and "placement", the local player's building placement mode starting (`[order, type]`) or closed by the interface (`[]`; the game's own closes within StepObjects and the trigger scheduler are Koprulu's to make), which shows a Protoss builder's Psi fields ([phase 6](phase6.md)). The draws of frame 0 are logged only once the game is past it, after the game's seed has dropped the menus' ones. The state decoder leaves the placement mode's ghost and box images (pools `0x59406c` and `0x595074`) out of the sprites, reads a sprite pointer in the union only in a Pylon's `+0xd0` and a Ghost's `+0xd0` as its nuke dot's lone thingy (`t<index>`).

`tests/golden/oracle/requirements_general.json` and `requirements_units.json` (written by `tools/oracle/export_requirement_cases.py` from the isolated-execution evidence of `docs/research/evidence/native-dat-requirements-probes.json` and `native-requirements-probes.json`, refusing a probe script whose hash changed) hold the original's 664 requirement evaluator cases with their fixtures; `tests/c/test_requirements.c` rebuilds each fixture in a native game (D16).

`kp_test -j N` (what `tools/ci.sh` runs, with `JOBS` processes) runs the suites in parallel processes: a sharded test goes one shard per process (each takes every n-th data file, animation or unit type through `kp_t_mine`, so together they check each one once), the other suites run whole, except `test_flow` (`KP_SUITE_SPLIT`: its 52 short tests whole took 64 s alone, up to 110 s under the run's load, the run's longest job), whose tests run one per process; `kp_test SUITE:TEST@I/N` reruns one shard. Each job's time goes to `times` in the logs' folder (only this run's jobs: a test's shards before their count changed drop out), and the next run starts its jobs the longest first (those no run has timed before them), so that it ends with short jobs instead of one long job alone. The loops over goldens (scenarios, replays, idle runs, plays, their saves, the plays with the speech) deal their items to the shards by weight (`kp_t_mine_goldens`: each golden's last compared step, the heaviest first to the shard with the least so far; a sampled run reads only its sample's goldens), the same in every shard's process, so a job's line still names the same items; a play's time follows its steps only roughly (a step of the expansion's later missions costs up to twenty times one of the first missions'), so the plays' tests have 12 shards. With `KP_TEST_SAMPLE=S` and `KP_TEST_SEED=N`, the loops that replay whole games (those and the player reach, `kp_t_mine_sampled`) check about one item in S, chosen by a hash of the test's name, the seed and the item, whatever the shards; a failed job's line names it as `SUITE:TEST@I/N/S/SEED`, which reruns exactly that. The dump's CRCs (`kp_dump_crcs`) are computed as the lines come, from the same bytes as the text dump; where two of Koprulu's own games are compared in one process (a recording and its playback, `test_session`), the state's lines go into a 64-bit hash without their text (`kp_dump_hash`), which is never compared with a golden. The oracle's data root keeps the files read after its build (each game's tileset) and its last four missions built, so a test that starts game after game on one map (`test_player_reach`) checks a file's digest and derives the map's terrain once.

`tools/ci.sh` has two modes. The quick one, the default, passes `KP_TEST_SAMPLE=4` to the sanitizer and wasm runs, with the seed taken from the step's key, so another build checks other items and a rerun of the same build the same ones; the native run always checks them all. `KOPRULU_FULL=1` checks them all everywhere: before a push and at the end of a stage. The quick mode finds a memory error that appears in one play only when its sample reaches that play or at the next full run; a wrong result against the goldens it finds at once, in the native run. The C suites under Apple's sanitizers run only where there is no LLVM clang (that build's `kp_hd_phone`, with the ASTC encoder, always runs).

A step that passed is not run again (`build/ci-passed/<label>`, `build/python-tests/passed.json`; `KOPRULU_NO_CACHE=1` runs everything), so a run stopped late goes on where it stopped; a full pass also answers for the quick mode, not the other way round. A C step's key (`tools/ci_cache.py`) is the bytes of its binaries, `tests/golden` and `tests/fixtures`, the names, sizes and times of `generated`, its environment and its compiler's, node's and glslang's versions; the Python tests' (`tools/python_tests.py`) is the bytes of the tracked files except `native/`, `tests/c/` and the documents outside `docs/research/`, the names, sizes and times of `generated`, the private evidence, the original installs, FFmpeg and StormLib, and the Python packages (by their metadata directories: a half-removed package has no metadata to read). It does not trace a module's imports: a change to any of it reruns all the modules. A pass whose key no longer holds when it ends is not remembered: it may have read files changed since the run began. Neither key reads `generated/reports` (the reports of `kp_convert locale-check` and `test_locale`, no step's input): until 2026-11-14 they did, and as the C suites rewrite those reports, no C step and no Python run was ever remembered while `KOPRULU_SCR` was set. The fixtures are committed (`tests/fixtures/kpf/`, since ADR 0019's stage C7; the Python converter's `fixtures.py` wrote them, its fixture movies with FFmpeg's `bitexact` flags from `testsrc` sources), and `kp_convert`'s WebM carry no dates or random identifiers, so a repack's movies are repeatable. A sanitized LLVM binary is not repeatable: a source recompiled unchanged reruns that step, the safe side. The product's checks always run: they depend on the machine's GPU and display.

The Python tests' modules run in the background from the start, 4 at a time in processes of their own; the data's checks that need only the native build (`kp_convert locale-check`, `hd-ui-table`, `verify`, `manifest --check`, the FFmpeg decodes, the CASC digests and their twin, `kp_replay`) run in the background beside the C suites, one after another (`build/early-checks.log`, printed when they join). Both join before the product's checks, or at a step's title once they have ended, so that a failure stops the run early. The product's checks that wait on the wall clock (`smoke_timeout`, `smoke_hang`, the live run) run alone first; the others run in two batches of `APP_JOBS` (default 4) at a time (`app_batch`, each unit's output in `build/app-jobs/N.log`, printed in the order queued): the fixed clock's scripts, the autotest and the installs' repacks, then the scripts on the real clock (`clock real`: the HD art's pages load on threads), the runs over the installs and their repacks against the bundle's, the languages, the movies and the HD art. Measured 2026-11-14, 25 product runs one at a time took 210 s, 4 at a time 59 s, every file of the fixed clock's scripts, the autotest and the cine checks the same and every check of the real clock's scripts passed. The whole `KOPRULU_FULL=1 KOPRULU_NO_CACHE=1` run with `KOPRULU_ORIGINAL`, `KOPRULU_SCR`, `KOPRULU_APP` and `KOPRULU_WEB` took 1358 s, against 2971 s before that day's changes: the steps before the C suites 15 s (100 s), the native C suites with the data's checks and the Python tests beside them 144 s (460 s), ASan and LSan 517 s (1335 s), wasm 404 s (566 s), the product's checks 187 s (526 s), the web 86 s. Measured 2026-11-07 on 14 logical CPUs, with other work on the machine at times (load average up to 73): the old order took 2618 s; `KOPRULU_FULL=1 KOPRULU_NO_CACHE=1` 1872 s; the quick mode after an engine change (converter remembered) 790 s, after a converter change too (`test_hd2` alone is 800 to 1200 s of wall time) 1192 s; nothing changed 21 s without the product's checks, which take about 270 s.

A dump made before a category existed lacks it; rerun the emulator for that scenario (`python -m sc_emulator scenario SPEC`) before writing its golden.

Plan phase 8 adds two kinds ([phase 8](phase8.md)):

- `tests/golden/oracle/chains/` (`state_check.py --kind chains`): the games of the campaign chains the emulator played through the original's progression, every mission won with "there is no cow level" (`python -m sc_emulator chain NAME WARP --oracle DIR --state-steps 700`), the loss of `terran02` with "game over man" and the seven hidden rows (Bonus through `--inject 63,0,1`). A chain's games log into DIR/<chain>/<k>-m<mission>; each game is compared started from the game before it (`KpCarry`; the golden's `carry_from`) and with the menus' turn counter (`turn`); its golden keeps every 20th step and the last, and the interface events of its last frame (the step after it, whose trigger pass opens the result dialog, gives the next game's carry). `test_scenarios.campaign_chains_equal_the_original` replays each chain with Koprulu's own games and checks that the campaign (`kp_campaign.c`) picks the next game's mission from each game's result and next scenario. `campaign_chains.json` (`tools/oracle/export_campaign_chains.py`, from the emulator chains' evidence) holds what each chain's missions led to: the next mission, the progress saved, the end.
- `tests/golden/oracle/plays/` (`state_check.py --kind plays`): the scripted wins of the campaign rows (`tools/research/sc_emulator/campaign_plays.py`, recorded with `python -m sc_emulator play NAME OUT --out-root .../emu/plays --state 1-501,521-60001/20`), and `terran-wasteland` (scenarios.wasteland, `--root .../emu/campaign`). A play golden keeps the continuation fingerprint of every 20th step (the CRC-32 of its category CRCs as lines `<category> <crc>`, `kp_state_fingerprint`) and the category CRCs of every 500th step and the last; it keeps all the play's input up to the result (the transmissions that end a mission wait for their interface events). `test_save.campaign_plays_and_saves_equal_the_original` replays each play to its victory, its traces equal, and on the way saves it at every 2000th step and checks the continuation up to the next save step (the first one to the play's end): one run of a play serves both checks. `saved_plays_catch_every_interrupted_process` keeps fixed saves of the plays that hold each kind of unfinished process (construction, training, cargo, hidden units, a computer's attack roster).
- `tests/golden/oracle/plays-ruRU/` (`state_check.py --kind plays --speech ruRU`): the same plays the original's game made again with ruRU's speech (ADR 0016, stage L5): `python -m sc_emulator play NAME OUT --out-root .../emu/plays-ruRU --state 1-501,521-60001/20 --fog 1000,4000 --speech ruRU:<c>/<m> --scr DIR` (until ADR 0019's stage C2 `--speech` named the converter's overlay, `generated/speech/ruRU/missions/<c>/<m>.kpm`, whose records are the same), where `sc_emulator/speech.py` merges the language's map's `TIME` into the 1.17 map's records and writes it into the loaded map's trigger lists at the game's first object step, and the reactive plays adapt to the timing it makes (zerg-m12 and protoss-m22 by `scenarios.zerg_no_cheats` and `protoss_p1_nocheat`). Koprulu takes the same `TIME` from the language's map (`KP_SPEECH` with the locale, `KP_SPEECH_DATA` with the install or a repack holding the language's group; `KpOracleOpts.speech`, `speech_data`; `state_check.py --speech-data`, default `KOPRULU_SCR`). `test_speech.campaign_plays_with_the_speech_equal_the_original` replays every one to its victory, one per base play, all 64 missions.
- `tests/golden/oracle/replays/` (`state_check.py --kind replays`; [phase RP](phase-replay.md)): the original's playback of its own `.rep` files through Load Replay (`python -m sc_emulator replay FILE OUT --oracle`, the files kept by the chains in `replays/`), with the logs at the run's root and the dumps in `states/`, under `$KOPRULU_EMU/replays/<name>`. The golden's `"replay": 1` (`KP_REPLAY` for `kp_oracle`) starts Koprulu's game in replay mode: full vision after the map's load and the replay's rules. Its steps are the commands the original's feed dispatched, and the transmissions' interface events are at the original's frames. Every step is kept. `test_scenarios.replays_equal_the_original` checks 62 games at every step: terran01, zerg01 and Brood War's protoss01, and the campaign chains' games named `<chain>.<k>-m<mission>` (the chains rerun with each `LastReplay.rep` kept, `koprulu-private/emu/rp/chains`).

Chain and play goldens leave out the sync commands (CheckSyncCommand `0x47a510` changes no game state) and keep a run of the screen's draws at one place, adjacent in the log on consecutive frames, as one entry whose `draws` string holds each frame's number of draws (expanded by the oracle library's replay): the log's order and a frame's repeated draws stay, since a draw's creep borders change the tiles around it.

Plan phase 9 adds the interface's input log of every run (`input.jsonl`, `tools/research/sc_emulator/uilog.py`; [phase 9](phase9.md)). `tools/oracle/export_ui_timers.py` writes from it `tests/golden/oracle/ui/timers_<run>.json`: every UI timer the original set, deleted and fired, with the ticks, in order, and the timer changes each fired timer's work made; `tests/c/test_kp_ui.c` replays them through kp_ui's queue (`kp_ui_timers.c`) and expects the same timers fired in the same order. The first run is the scenario `build_terran` (the title, the ready room and 1400 frames of the game, about 6,000 passes).

The console's panels (phase 9B) are checked against the emulator's screen captures (`SC_SCREENS=F,F,...`, `uiscreen.py`): `tools/ui_check.py --root DIR` turns each run's input log into the interface's work (`tools/hud_log.py`: the timer passes, UpdateStatusUi and PresentFrame calls, selections, talks, the cursor and the game calls of ui.jsonl at their places), replays the run's scenario golden with kp_ui's console over it (`kp_oracle hud_check` over the oracle library's console replay, `kp_oracle_ui.h`) and compares every open dialog, the text lines' layer and the cursor's frame with the capture in palette indices (a dialog with flag `0x2000` where its own index is not 0; a modeless dialog likewise, its index 0 compared unless it has the flag). A run that cannot be replayed (a cut input log, the tool's failure) fails as one panel and the other runs are still checked; `tools/input_check.py` counts such a run as differing. The captures stay private (the emulator's `hud` runs, `run.sh` there records them); `tools/ci.sh` runs the check and the runs' input check with `KOPRULU_HUD=<dir>`. The 69 campaign plays re-recorded with the input log replay through the interface with `KOPRULU_PLAYS_UI` (the plays' directory) and `KOPRULU_PLAYS` (their first state dumps' cheat word): `tools/input_check.py --kind plays`. Its golden twin, `tools/oracle/export_ui_hud.py`, keeps four short runs (one per race and `tooltips`) as `tests/golden/oracle/ui/hud_<run>.json`: the interface's work up to the last kept frame and the CRC-32 of each console dialog crop without transparency that neither the cursor nor the tooltip box covers, and of the tooltip box (newer captures keep layer 1's rectangle and the visible flag); `test_kp_ui.hud_panels_equal_the_original` replays them.

The interface's recordings were made over the 1.17 install's files, and StarCraft: Remastered's copies of some differ
(seven strings of `rez\gluAll.tbl`, such as string 136, `"Map:  "` there and `"Map:"` in Remastered; the consoles' PCX;
the glue's GRP and dialogs). The tests that compare the interface's drawing or its text's layout with them
(`test_kp_ui.text_matches_the_original_probes`, `panels_equal_the_original`, `hud_panels_*`) take the data through
`kp_t_data_117` and fail when the data root's repack was written from Remastered
(`kp_oracle_data_origin`, `kp_res_origin`: its listing's `source`), naming the folder and `make repack SC=DIR`. The
game's goldens hold over either install: the files a game reads are the same.

The interface's behaviour (phase 9C, level 1) is checked by `tools/input_check.py --root DIR [--jobs N]`: kp_ui takes each run's logged input itself (`kp_oracle input_check`: the console replay with its input taken: the dispatched events, the WM_COMMAND accelerators, the scroll records where UpdateMapScroll `0x49bf10` left its state changed, with the system cursor it saw, and the cursor's choice with its position) over the run's scenario golden; the local selections it makes right after an input, its queued commands (commands.jsonl less the sync command 0x37; a command stands one frame after its input's), the view after each event, the scroll's state, the tooltip each run of tooltip records left (PlaceTooltip `0x4193e0`, HideTooltip `0x4191d0`), the cursor's choices and the trigger wait's releases (the portrait's end, the camera pan's arrival) must equal the original's. The runs stay private (the emulator's `input` runs: every test scenario, `run.sh` there records them, `tools/ci.sh` with `KOPRULU_INPUT=<dir>`; `hud`: the console runs, `KOPRULU_HUD`). With the log's sound records (phase 9D, uilog.py "snd") the interface plays the sounds itself: every PlaySoundInternal `0x49e4e0` call (id, attenuation, pan, unit type, path: rejected, queued or started, result) and every talking request must equal the original's, in order (the game's sounds reach the interface after its step); the voices' CRT draws are the interface's then, not the log's. A command stands one frame after its input's, or at its input's frame while the game is paused (a paused game's turns leave the frame counter). The campaign plays re-recorded with the input log (the emulator's `plays-ui`, `KOPRULU_PLAYS_UI`, `run.sh` there: the same flags as the plays, so the games equal the recorded ones; `tools/input_check.py --kind plays --root DIR --states DIR`) all replay through the interface's own input; where a re-recording's commands differ from the first recording (the emulator's timing changed since: terran-m03, terran-m06, xprotoss-m36, xzerg-m50, protoss-m22, whose play is `scenarios.protoss_p1_nocheat`), the re-recording with its state dumps replaced the play in `$KOPRULU_PLAYS` and its golden (the first recording kept in `plays-old`). A run without its golden or its logs is skipped and counted (`input_check: N runs, D differ, S skipped`); the check fails when a run differs or none was checked.

`tools/fog_check.py` compares Koprulu's fog of war with the emulator's fog captures (`sc_emulator play ... --fog FRAMES [--fog-partial]`, `fogcap.py`): the original's level grids and level map, the view after its fog pass pixel for pixel, the minimap's composed fog. The captures stay private (`$KOPRULU_CAPTURES/fog`); `tools/ci.sh` runs the check with `KOPRULU_CAPTURES`.

# Golden oracle: original-code hashes of runtime modules

Phases 2 and 4 check runtime code against the original 1.17 functions running in Unicorn: a probe and a C test run the same scenario and fold the state or the bitmap into the same 32-bit hash, `h = (h·31 + v) mod 2^32`. The golden files hold the scenario's parameters and the hashes, no retail bytes.

| File | Probe | Test | Content |
|---|---|---|---|
| `iscript/cases.json` | `tools/research/probe_native_iscript_117.py` | `tests/c/test_images.c` (`scenario_matches_probe`, `every_animation_equals_original`, `synthetic_programs_equal_original`) | Per image animation of the retail script (2,917) and per synthetic program: completed frames and the hash of the image, sprite, RNG and event state after each of 80 frames of `UpdateThingies` |
| `health_bar/cases.json` | `tools/research/probe_native_health_bar_117.py` | `tests/c/test_present.c` (`health_bars_equal_original`) | Per unit type with a health bar (207): the maximum energy from `0x434940` and one hash over its cases (hit point, shield, energy and timer levels, owners, hallucination, crops: 13,846) of the bitmap `DrawHealthBar 0x464d20` leaves |
| `damage_table.json` | `tools/research/probe_damage_table_117.py` (on the DAT of an expansion game's state dump, `--state`) | `tests/c/test_damage_table.c` | Per upgrade level of the target's owner (0 and 3) and unit type (228): the CRC-32 of the hit points and shields `WeaponDamage 0x41dfc0` takes from a target of that type, for each of the 130 weapons at its base damage |

Both tests need the original data (a repack or an install, `kp_t_data`) and skip without it.

**Regenerating** (private venv, retail directory):

```sh
python tools/research/probe_native_iscript_117.py --data DIR --golden tests/golden/oracle/iscript/cases.json --evidence docs/research/evidence/native-iscript-probes.json
python tools/research/probe_native_health_bar_117.py --data DIR --golden tests/golden/oracle/health_bar/cases.json
python tools/research/probe_damage_table_117.py --exe DIR/StarCraft.exe --state EMU/scenarios/medic_heal/states/s00001.state --out tests/golden/oracle/damage_table.json
```

`--rows FILE` (damage table) writes the uncompressed rows; `--case IMAGE ANIM` (IScript) and `--type T` (health bars) print the per-frame values or per-case hashes of one case for debugging.

**Renderer frames.** The phase 2 acceptance compares Koprulu's draw list and kernels with frames of the original renderer captured by the full-game emulator (`tools/research/sc_emulator/capture.py`, `tools/compose_capture.py`; [phase 2](phase2.md#acceptance-item-7)). The captures contain game pixels and stay private; `tools/ci.sh` compares them when `KOPRULU_CAPTURES` names their directory.
