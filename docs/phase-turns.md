# The turn source, fuzzing and the dump's coverage (phase T)

The user approved the design on 2026-11-15, with stage T3 first; T1 and T2 follow. The dump's coverage extends [ADR 0021](adr/0021-state-walk-and-saves.md#addition-the-dumps-coverage-stage-t3); the turn source and the fuzzing get an ADR of their own when they are built.

| Stage | What | State |
|---|---|---|
| T1 | The turn source: the session's commands, a replay's feed and the recorder behind one source of turns | planned |
| T2 | Fuzzing of the untrusted inputs (saves, replays, commands) by libFuzzer, the findings kept as regression inputs | planned |
| T3 | The dump's coverage: names of the walk's visits, `test_save.dump_shows_the_saved_state`, `kp_dump_fields()`, the state the oracle did not read added as new categories, the plays' fingerprint fixed at the first version | done 2026-11-15 |

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
