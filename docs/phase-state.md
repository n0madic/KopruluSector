# One walk over the game's state (phase W)

Design: [ADR 0021](adr/0021-state-walk-and-saves.md); the save's container: [docs/formats/rep.md](formats/rep.md#saves). The user approved the design and the plan on 2026-11-15, with these decisions:
1. All the stages in one session.
2. The save's format changes now (version 2), with no migrations: a save of another version is refused with a message the player sees.
3. The save's CORE is compressed by zlib at level 1.
4. The FNV-1a fingerprint gives way to a table of the sections' hashes.
5. A test of the schema's fingerprint forces the version up when the walk changes.

| Stage | What | State |
|---|---|---|
| W0 | The coverage: the COVER mode, the exclusion table, `kp_state_ranges`, `test_save.walk_covers_the_state` | done 2026-11-15 |
| W1 | The state hash and the native save v2: the HASH mode, `kp_state_hash`, the sections' hash table, `kp_save_schema` | done 2026-11-15 |
| W2 | The in-process snapshot: the RAW mode, `kp_snapshot`, `kp_restore` | done 2026-11-15 |
| W3 | The compressed save, container schema 2, the refusal the player sees (gluAll 66 and 67) | done 2026-11-15 |
| — | `kp_bench --state`: the walks' times and the forms' sizes | done 2026-11-15 |

Not in the phase:
- a replay's rewinding in the session and the interface (the snapshot is its base);
- a hash equal across the players' places (the VIEW masks);
- a change of `KpGame`'s layout;
- `kp_dump` (the oracle's goldens depend on its text; its coverage by the walk came with stage T3, [phase T](phase-turns.md#t3-as-built));
- the `.rep` format and the save's `REPL` section;
- a run on the Fold7 (the save's time on the phone is not measured).

## W0 as built

- **`kp_save.c`:**
  - The end of `kp_load` (the changed-terrain flag, the event ring cleared, the countdown's text, the interface's serials) is `restored()`, shared by the load and the restore.
  - The COVER mode records each call: section, kind of primitive, address, count, element size. The walk has two cases of its own there. AIRG takes `KP_AI_REGIONS` as its count (the rows past the map's regions are no state, but the arrays are whole). LDAT does not read the DAT through `kp_dat_read`.
  - The exclusion table beside the walk is built from the live game's addresses (`&g->terrain.cv5`, `sizeof`), with no `offsetof`, so the C++11 check passes it. 94 entries: STATIC 44, HOST 31, DERIVED 13, VIEW 5, PADDING 1 (ADR 0021 lists what each holds).
  - PADDING is measured on the platform: the bytes between the end of the contours and `g->pathing`. `KpPathing` is aligned to 8 but walked from its byte array, so the test cannot tell its padding by that field's alignment. It is 0 bytes on the 64-bit hosts and 4 in wasm, where pointers are 4 bytes; the first full CI run caught it there.
- **`kp_state_ranges(g, &ranges, &count)`** gives the visited and the excluded ranges of the three bases: `KpGame`, `KpImages` (`g->w`) and `KpRngGame` (`g->rng`). Ranges outside them (heap blocks, the walk's locals) are dropped.
- **`test_save.walk_covers_the_state`** sorts and merges the ranges. An overlap fails it. A hole is allowed only before a field and smaller than that field's alignment, and a base's tail only under 8 bytes. A failure names the base, the offset, the length and the neighbouring entries.
- **The first run found 20 holes.** Each was mapped to its field through clang's record layouts and classified; none was unsaved simulation state. A mutation (a walked field dropped from the walk) fails the test.

## W1 as built

- **`kp_state_hash(g, each, ud, &total)`:**
  - **Section hash.** Each section is hashed over its bytes as they are in the save (its tag left out), as 8-byte little-endian words, the tail zero-padded. The seed is `mix(HASH_SEED, tag)` with `HASH_SEED = 0x6b70686173680000 | KP_HASH_VERSION`. A word goes in by `mix(h, w) = ((h ^ w) * 0x9e3779b97f4a7c15) ^ (… >> 32)`, and the section is closed by mixing in its length, then murmur3's `fmix64`.
  - **Total.** The sections' hashes, folded in order from `HASH_SEED` and closed with their count.
  - **Implementations.** Two implement the same definition. One works over the bytes, for the save's table and the load's check. The other is the HASH mode: a shift accumulator that adds each value of 8, 4 or 1 bytes to the current word, without encoding.
- **The native save v2:**
  - **Layout.** `KPSV`, u32 version 2, the mission's and the data's names, the sections with their tags, `END `, then the table: u32 count, per section its tag and u64 hash, u64 total.
  - **The load's checks, in order:** the version (`save: version N, this build reads 2`), the names, a dry walk that hashes the input's sections against the table (`save: section XXXX damaged`, or `save: damaged (hash table)`), then the real walk and `restored()`. A refusal leaves the game as it was.
- **`kp_save_schema()`** runs a COVER walk over a zeroed game (`calloc`'d `KpGame`, `KpImages`, `KpRngGame`) and hashes the sequence of (section, kind, count). There are no addresses in it, so it is the same on every platform. Version 2's fingerprint is `0x12e396b75619fee4`.
- **`tests/c/test_save.c`:**
  - `hash_is_the_saves_bytes` (sampled with `kp_t_mine_sampled`, at the start and the middle of each scenario): `kp_state_hash` equals the table in `kp_save`'s bytes and the test's own reference function over the bytes cut by the callback's sizes. It runs on every configuration of `kp_test`, which closes the platforms' equality.
  - `save_schema_matches_its_version`: the table {version → fingerprint}. A changed walk fails with "raise KP_SAVE_VERSION and add a row". Two visits of the same kind and count swapped within a section keep the fingerprint (it holds no addresses, to be the same on every platform): such a change raises the version by hand.
  - `continue_from`: the hash after a save and a load into another game equals the saved game's.
  - `loads_reject_other_saves`: a version-1 core, a damaged byte inside a section (named: `section SCAL damaged`), a damaged table.

## W2 as built

- **`kp_snapshot_size`, `kp_snapshot`, `kp_restore`** into the caller's buffer, so a ring of snapshots reuses its memory.
  - **Header:** magic `KPSN`, `sizeof(KpGame)`, `sizeof(KpImages)`, the region graph's, the AI regions' and the trigger copies' counts, whether the game has the CRT generator, the map's width and height, then the whole size.
  - **Restore:** checks the header and the size before changing anything. The sections' tags are written and checked in RAW too: a buffer damaged past its header is refused with the game partly restored. `restored()` follows, as after a load.
- **Tests:** `test_save.snapshots_rewind_as_the_original` (6 shards) and `restores_refuse_other_snapshots`.
  - A rewind: a snapshot in the middle of each scenario, the run to the end, the restore, the middle's state hash again, then the run again with the original's CRCs at every recorded step.
  - Another game: a snapshot restored into a fresh game of the same mission continues the same.
  - Refusals: a buffer of another size and another mission's snapshot are refused, and the game does not change.

## W3 as built

- **`kp_session.c`:** `KP_SAVE_SCHEMA` 2. CORE (its section schema 2) holds the native save's size (u32 LE) and its zlib stream (`compress2`, level 1); CMDS, CAMP, PRES and REPL do not change.
  - `decode()` checks the container's schema and identity before `kp_kpf_check`. Another schema: `save: a save of another build (save schema N, this build reads 2)`. Another profile or data hash: `save: made with other game data`.
  - The core's size is bounded by `KP_SAVE_CORE_MAX` (64 MiB). The load inflates the whole core; `kp_save_mission` inflates only its head (the magic, the version, the mission's name).
- **`kp_save_check(bytes, size, identity, &err)`** gives why a save cannot load: `KP_SAVE_LOADABLE`, `KP_SAVE_DAMAGED` or `KP_SAVE_OTHER_BUILD`. These are the statuses of the original's ValidateSaveFile 0x499750: 1 unreadable, 2 another version.
- **The player is told (the user's report: a save that did not load failed silently).** The original's AddSavedGameToList 0x499250 never lists a save ValidateSaveFile refuses. Koprulu lists the profile's saves, so it reads the chosen one at Ok, through the interface's `check_save` hook (the flow's `check_save` reads the file):
  - the glue's load screen shows an OK popup (`gluPOk.bin`) and stays;
  - the in-game load dialog shows an OK box (`rez\ok.bin`) and opens again after it, sending no load;
  - the text is gluAll 67 ("…because of a version mismatch") for another build or other data, and gluAll 66 ("…invalid or corrupted") for the rest, a missing file included;
  - the log keeps the reason (`load NAME: refused: …`);
  - a load the host cannot make after the check (`kp_ui_load_failed`) shows gluAll 66 over the game.
- **Tests:**
  - `test_session.session_saves_are_compressed_and_refuse_other_builds`: a new save is compressed and loadable. Schema 1, other data, a version-1 core, a cut file, a wrong size prefix and a cut stream are each refused with their kind, and the game does not change.
  - `test_flow.check_save_tells_why_a_save_does_not_load` (0, 66 cut, 67 old schema, 66 missing).
  - `test_kp_ui.load_dialog_refuses_a_save_that_does_not_load`.
  - `test_hud.glue_load_screen_deletes_and_loads` (the refused save's popup).
  - `test_flow.load_that_fails_keeps_the_game_or_opens_the_campaign_menu` (the box after a failed load).
  - The shared helper `kp_t_save_rebuilt` (`tests/c/kp_test_app.c`) rebuilds a container with another schema or CORE.

## Measurements

`kp_bench --state ROOT REPLAY [RUNS]` (20 runs, release, the development Mac; the minimum and the median in ms). Before: `kp_save` 15.3 (11.5 of it the FNV-1a fingerprint), `kp_load` 17.5, `kp_dump`'s CRCs 2.1.

| Scenario (step) | save | load | hash | snapshot | restore | zlib-1 | inflate |
|---|---|---|---|---|---|---|---|
| `bench_combat` (901) | 3.79 / 4.06 | 5.37 / 5.58 | 1.37 / 1.57 | 0.17 / 0.20 | 0.15 / 0.19 | 6.34 / 6.61 | 4.22 / 4.41 |
| `bench_200` (761) | 3.81 / 4.01 | 5.36 / 5.58 | 1.44 / 1.55 | 0.16 / 0.19 | 0.16 / 0.19 | 6.01 / 6.36 | 4.10 / 4.30 |

The native save is 8,032,252 bytes (`bench_combat`) and its zlib stream 171,564, 47 times less; `bench_200`'s stream is 158,231. The snapshot is 8,031,905 bytes.

## Verification

- **macOS:** the full `tools/ci.sh` (`KOPRULU_FULL=1`, both installs, the product, the web) passed on 2026-11-15. It covers the C suites on the host, under LLVM ASan/UBSan and in wasm under node (628 each), and the product's `save.kps` with `save_verify.kps`. Its first run failed `walk_covers_the_state` in wasm only: the padding before `KpPathing`, now the PADDING entry.
- **Linux** (GCC 15.2 in the Docker image, arm64): every target builds with no warning. `test_save`, `test_session`, `test_flow`, `test_kp_ui` and `test_hud` passed.
- **Windows** (mingw-w64 GCC 16.2, LLP64) under Wine: every target builds, including the C++11 check. `test_save` and `test_session` passed, so the schema's fingerprint and the hashes are the same there.
- **Older breaks fixed on the way.** They predate this phase, and no `ci.sh` step builds these toolchains:
  - GCC 15's `-Wmaybe-uninitialized` in `kp_remove_from_local_selection`;
  - its `-Wclobbered` in `kp_bench --dump`;
  - mingw's `-Wrestrict` on the replay folder's `strcat` (`enter_replay_folder`, now lengths and `memmove`);
  - `%zu` in `kp_t_ok` and `kp_t_log` formats, which mingw's `printf` format check refuses (`test_kp_rep.c`, `test_original.c`).
- **Not run:** the Fold7 (the save's time on the phone is not measured), and a save and load by hand in the browser (the web's saves go through the same C code, checked in wasm).
