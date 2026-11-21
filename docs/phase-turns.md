# The turn source, fuzzing and the dump's coverage (phase T)

The user approved the design on 2026-11-15, with stage T3 first; T1 and T2 follow. The dump's coverage extends [ADR 0021](adr/0021-state-walk-and-saves.md#addition-the-dumps-coverage-stage-t3); the turn source and the fuzzing are [ADR 0022](adr/0022-turn-source-and-fuzzing.md).

| Stage | What | State |
|---|---|---|
| T1 | The turn source: the session's commands, a replay's feed and the recorder behind one source of turns | done 2026-11-15 |
| T2 | Fuzzing of the untrusted inputs (replays, maps, saves, commands) by libFuzzer, the findings kept as regression inputs | done 2026-11-15 |
| T3 | The dump's coverage: names of the walk's visits, `test_save.dump_shows_the_saved_state`, `kp_dump_fields()`, the state the oracle did not read added as new categories, the plays' fingerprint fixed at the first version | done 2026-11-15 |

## T1 as built

- **`native/kp_app/kp_turns.{h,c}`** (`KpTurns`, held by `KpSession.turns`): the kinds `KP_TURNS_LOCAL` (the queued commands, each a whole buffer) and `KP_TURNS_REPLAY` (the record's commands of the frame by `kp_record_feed`, split by `kp_record_split`, each as the player whose record has its Storm ID, then the live commands).
- **The calls:** `kp_turns_replay` (the record loaded), `kp_turns_begin` (the local player, once the game started), `kp_turns_submit` (a playback's filter, table 0x47636c; an empty command and a player past the seats refused in every source), `kp_turns_frame` (`KP_TURNS_READY`, `KP_TURNS_ENDED` once, `KP_TURNS_NO_MEMORY`), `kp_turns_done`, `kp_turns_record` (the recorder watching the executed list: the seats with a Storm ID, the local player's 0 in a game played alone).
- **`kp_session.c`:** the step asks the source, opens the victory dialog on `KP_TURNS_ENDED` and keeps a playback paused after it, records through `kp_turns_record` and runs the frame. `KpSession.rec` is a recorded game's recorder only (a playback's record is the source's). The save writes CMDS from the source's queue and restores it through `kp_turns_submit`; a save whose queue holds a command it would refuse (an empty one, a player past the seats) is refused before the game changes. `kp_session_playing`, `kp_session_replay_ended` and `kp_session_replay_frames` replace the fields the flow and the scripts read.
- **The network's third kind is a TODO** in `kp_turns.h`: a frame not ready yet, the session's players as seats, a viewer as a replay over a growing record.

## T1 verification

- `test_session` (35 tests, with the two new ones and the replays of every golden scenario, play and idle run recorded and played back), `test_scenarios` (the 65 replays against the original at every step), `test_kp_rep`, `test_flow`, `test_script` passed.
- The new tests fail on a broken source: a playback's filter that lets a select through, a recording under the wrong Storm ID.
- The full `tools/ci.sh` (`KOPRULU_FULL`, both installs, the macOS product) passed: 631 C tests on the host, under ASan/UBSan and in wasm; the product scripts, `tests/scripts/replay.kps` among them, 0 failures.

## T2 as built

- **The entries** (`tests/c/kp_fuzz.{h,c}`, the static library `kp_fuzz_lib`): `rep`, `chk`, `save` (a save file, or edits by section of the game's own native save), `commands` (chunks of a player's command bytes and steps on one of three scenarios). `kp_fuzz_init` starts what a target needs (the data, the games and their snapshots), `kp_fuzz_run` takes one input, `kp_fuzz_stats` counts the inputs, the refusals by reason and the saves loaded.
- **The core's two calls for the harness** (`kp_game.h`, `kp_save.c`): `kp_save_seal`, a save's hash table remade for its bytes, and `kp_save_sections`, the sections kp_load's dry walk finds. Both share kp_load's dry walk (`walk_dry`).
- **libFuzzer's drivers** (`tests/fuzz/kp_fuzz_main.c`; CMake `KP_FUZZ`: `fuzz_rep`, `fuzz_chk`, `fuzz_save`, `fuzz_commands`), each printing its counts and refusals at the end. **`tools/fuzz.sh`** builds them with an LLVM clang and runs them, the corpora and findings under `build/fuzz/`.
- **The seeds** (`kp_fuzz_seeds`, `tests/fuzz/kp_fuzz_seeds.c`): `--synthetic` writes the five committed ones (`tests/fuzz/seeds/`); without it, with the data, 27 (`build/fuzz/seeds/`, never committed).
- **`test_fuzz`** (6 tests): the committed seeds and kept findings of each target (`kept_*_inputs`; the save target has no synthetic seed, a save is a game's), `data_seeds_run` (the data's seeds: every save loads in both forms, only the orders past orders.dat are refused), `seal_makes_the_hash_table`.
- **CI:** `tools/ci.sh`'s full mode runs `tools/fuzz.sh` (`KOPRULU_FUZZ_TIME`, 60 s a target) with the LLVM sanitizer step's clang.
- **Later targets:** `turns` (phase M, the seats' hub) and the network's `udp` and `snet` (phase N, stage N2: [ADR 0022's addition](adr/0022-turn-source-and-fuzzing.md#addition-the-networks-targets-phase-n-stage-n2)), `lobby` and `transfer` (stage N3) and `game` (after N6: [ADR 0022's addition](adr/0022-turn-source-and-fuzzing.md#addition-the-network-game-after-its-start-phase-n-2026-11-21)); `tools/fuzz.sh` runs all ten.

## T2 verification

- `test_fuzz` passed on the host, under ASan, LeakSanitizer and UBSan (LLVM clang 23.1) and in wasm under node.
- **The fuzzing**, five minutes a target under ASan and UBSan (LLVM clang 23.1, the M-series Mac, the corpus kept from shorter runs before), found no crash, sanitizer report, leak or hang:

  | Target | Inputs | Inputs a second | Coverage (edges) | What the games did |
  |---|---|---|---|---|
  | `rep` | 1,896,534 | 6,300 | 388 | — |
  | `chk` (with the data) | 70,867 | 235 | 1,152 | — |
  | `save` | 12,771 | 42 | 525 | 1,672 saves loaded |
  | `commands` | 132,422 | 439 | 5,782 | 15,984 games refused |

  The refusals of a two-minute run after the reasons were told apart (`commands`, 5,944 of 48,424 inputs): shared vision 0x0d 3,035, alliance 0x0e 2,298, 0x38-0x3b 487, leave game 0x57 38, 0x1d 53, chat 0x5c 1 (the commands no game played alone sends, [ADR 0022](adr/0022-turn-source-and-fuzzing.md#consequences-1)), and 32 targeted orders past orders.dat (the requirements' range check on `KP_NIL`).
- The first runs showed that the native save, 8 MB, never reached `kp_load` as libFuzzer's input (cut at 1 MB): the native form became edits of the game's own save by section.
- GCC builds everything and `test_fuzz` passes on Linux (GCC 15.2, the Docker image) and on Windows (mingw-w64 GCC 16.2 under Wine). The synthetic seeds are the same bytes on Windows. GCC also found a size read before it was set in `test_save.dump_shows_the_saved_state` (T3's), now set: the hosted Linux job would have failed on it.
- The full `tools/ci.sh` (`KOPRULU_FULL`, both installs, the macOS product) passed in 1329 s: 637 C tests on the host, under ASan, LSan and UBSan and in wasm; the fuzzing step's four targets 60 s each with no finding; the product scripts 0 failures.

## T3 as built

**Why a test and not one list.** `kp_dump.c` is a model of the original's memory: the names and the normalisation of the pointers of `tools/native_state.py`, by which every CRC of the goldens was computed. The dump cannot be derived from the save's walk, so a test binds the two, as `test_save.walk_covers_the_state` binds the walk to the structures.

- **Names of the walk's visits (`kp_save.c`).** Macros of the same names as the walk's primitives record `#expr` and `__LINE__` in the `Codec` before each call. Only the RAW mode logs them: `kp_snapshot_fields(g, &fields, &count)` gives each visit's section, name, line, offset in the snapshot, bytes and element size. The save's bytes, the hash and the schema's fingerprint do not change.
- **`test_save.dump_shows_the_saved_state` (the transplant test):**
  - the donors are 46 games: 23 of the replays' goldens, 5 campaign runs and 18 scenarios, each run to 5 points, with a snapshot at each;
  - a group is every visit with one name (the elements of a loop share it), so a field's whole array moves at once;
  - for each pair of points, the group's bytes go from the later snapshot into the earlier one, the game is restored and `kp_dump_hash(drawn=0)` is compared with the earlier state's. The values come from a real state, so indexes stay in range and lists have no cycles, which random mutation would not give;
  - a group is shown when the hash changes in some pair, or when the dump fails on the mixed state (`kp_t_pcall`: it reads the field);
  - hidden: its bytes differ but no hash changes; unexercised: its bytes never differ (logged, not failed: the method's limit); rebuilt: the restore changed it (fails: `restored()` must not touch saved fields);
  - a field the dump shows but no game changes on a listed record (its differences are only in free records) is listed as `KP_DUMP_SHOWN`: the test checks it by adding one to every element at every pair until the hash changes.
- **`kp_dump_fields()` (`kp_dump.c`, `kp_game.h`):** {name, class, why}. A name ending in `*` covers a prefix. Classes: `KP_DUMP_SHOWN`, `KP_DUMP_MODEL` (the original keeps it at a known address, the model does not read it yet), `KP_DUMP_HEAP` (in a heap block the emulator's dumps lack), `KP_DUMP_KOPRULU` (Koprulu's own). The test fails on a hidden field without a row, a row that matches nothing, a non-SHOWN row whose field is shown, a SHOWN row it cannot confirm, and one name at two lines of the walk.
- **The new categories** (`tools/native_state.py` `later`, `kp_dump.c` `later_lines`; [golden-oracle.md](golden-oracle.md)): `step_state`, `sprite_lists`, `links`, `ai_military`. Each is a category of its own, so the earlier goldens stand. `sprite_lists` holds every list of the sprites and the main images the others do not walk (the free lists, the rows' previous links): it is the local interface's like `sprite` and `image`, and a script's `sim.crc` leaves it aside (`tests/scripts/replay.kps` failed while the rows' previous links were in `links`: the recorded game's ground marker is not in its playback).
- **The plays' fingerprint stays at the first version.** The plays' goldens keep only the fingerprint at most steps, and their step dumps were deleted on 2026-10-26, so `kp_state_fingerprint` and `state_check.py fingerprint` cover the first version's 40 categories only (`FINGERPRINT_CATEGORIES`, the same list in both). The replays, whose dumps are kept, also keep `fingerprint_later` over the other categories at those steps.
- **Differences the new categories found, fixed:**
  - the game RNG's switch `0x673930` stayed on after the start: 0x467ba0 runs StartGame 0x443f40 between enabling and disabling it, and Koprulu's `kp_start` now ends with it off;
  - the IScript contexts: the original nulls the pointers, Koprulu cleared only its `has_*` flags and kept the old values. `clear_context` (`kp_step.c`) and the end of `kp_bullets_step` now zero the values too.
- **Left as `KP_DUMP_MODEL`, each with a TODO in the table:** the free lists of the health bars, the circles, the ghosts and the boxes, and the lone and fog thingies (their heads' addresses are not known yet); a CImage's GRP pointer `+0x2c` and draw parameter `+0x28` (the model must map them); weapons.dat's forward offsets that attkshiftproj writes and the requirements' lazy offset tables; the path searches' state kept between searches; the receding creep's chains. `KP_DUMP_HEAP`: the virtual GRP headers, the current trigger `0x6ca790` (a heap node; Koprulu only writes it) and the CRT generator's `holdrand` (the thread's data block).

## T3 verification

- `state_check.py --kind replays --step all`: the 65 replays equal the original at every step (22,289 states) in all the categories, the new ones included; `--kind initial`: the 64 missions' starts equal.
- The goldens rewritten by `--write-golden`: `initial_state` (the new categories, and the first version's AI and guard-time categories its goldens had lacked) and `replays` (every earlier CRC unchanged; 22,157 fingerprint steps gained `fingerprint_later`). A `fingerprint_later` changed by one bit fails `test_scenarios.replays_equal_the_original` at its step. The scenarios', runs', chains' and plays' goldens are not rewritten: only their first state's dump is kept.
- `test_save.dump_shows_the_saved_state` (605 saved fields): 12 s on the host, 26 s under ASan/UBSan, 16 s in wasm under node.
- The full `tools/ci.sh` (`KOPRULU_FULL`, both installs, the macOS product) passed: 629 C tests on the host, under ASan/UBSan and in wasm; `tests/scripts/replay.kps` 0 failures with `sprite_lists` left out of `sim.crc`.
