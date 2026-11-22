# ADR 0021: One walk over the game's state: coverage, hash, snapshot, saves v2

Status: accepted (phase W, 2026-11-15). The user approved the design and the plan; their decisions are listed below. Stages and their state: [docs/phase-state.md](../phase-state.md). The save's container: [docs/formats/rep.md](../formats/rep.md#saves).

## Context

The game's state was described once, by hand, in `walk()` of `kp_save.c`, and served only the save and the load. A state hash and an in-memory snapshot are needed too: by a replay's rewinding, by the determinism checks and, later, by network games. Each would have needed a third and a fourth hand-written list. A field forgotten in `walk()` was caught only by tests of behaviour, if a scenario happened to reach it.

The save was 8.03 MB. On the development Mac (release, `bench_combat`), `kp_save` took 15.3 ms, 11.5 ms of it in the FNV-1a fingerprint over the bytes, and `kp_load` 17.5 ms.

Saves were not compatible in practice anyway:
- no release and no tag has shipped;
- `KP_SAVE_VERSION` had been 1 since 2026-10-13 (cf3cd9c), while the walk changed in about eleven commits;
- a save is already bound to the data (`source_hash`, `profile_id`).

A migration makes no sense for a deterministic simulation: an old state does not continue as the original in a corrected simulation.

## Decision

1. **One walk, four modes.** The walk's `Codec` gains a mode, and its typed primitives (`i64s`, `i32s`, `u32s`, `u8s`, `ints`, `lists`, `str`, the single `u32`/`u64`, `checked_*`) branch on it:
   - BYTES: the save's little-endian encoding (written, dry-read, read);
   - HASH: the same values into a streaming 64-bit hash, without bytes (`kp_state_hash`);
   - RAW: the arrays as they are, into the caller's buffer and back (`kp_snapshot`, `kp_restore`);
   - COVER: a record per call (section, kind, address, count, element size), for the coverage and the schema.

   There is no visitor interface: the walk's body stays the one list of fields.
2. **The coverage is a test.** `kp_state_ranges` gives the ranges the walk visits and an exclusion table beside the walk: 94 entries, each with a class:
   - STATIC: what the mission's start builds and nothing changes (DAT, the map's size, regions, contours, the region graph, the tileset's tables, the power grid's masks, the AI scripts, the trigger strings and briefing, GRP, IScript), 44;
   - HOST: the host's pointers and settings, 31;
   - DERIVED: what `restored()` or the next step derives (the changed-terrain set, the spatial index's frames, the collision and AI scratch, the countdown's text, the draw keys), 13;
   - VIEW: the event ring and the interface's serials (selection, refresh, minimap, load), 5;
   - PADDING: a platform's padding before a struct aligned past its first walked field, measured from addresses (4 bytes before `KpPathing` in wasm, none on the 64-bit hosts), 1.

   `test_save.walk_covers_the_state` checks that the two cover the whole of `KpGame`, `KpImages` and `KpRngGame`, without overlaps and with holes only of padding (smaller than the next field's alignment).
3. **The state hash is the save's bytes.** A section's hash mixes the section's bytes in the save (its 4-byte tag left out) as 8-byte little-endian words, the tail zero-padded. It is seeded with the tag and closed with the length (murmur3's finaliser). The total folds the sections' hashes in order. The header (the mission's and the data's names) is not hashed. The arithmetic is `uint64_t` only, so the hash does not depend on the host's layout or byte order. `KP_HASH_VERSION` (1) is in the seed. Since ADR 0023 (save version 3, hash version 2) the units' view state is a section of its own, `VIEW`, which the total leaves out: its hash is in the table and checked by the load, but two seats whose local players differ get the same total.
4. **Saves v2, one version, no migrations.**
   - The native save (`KPSV`) is version 2: the FNV-1a fingerprint gave way to a table of the sections' hashes (count, then tag and hash per section, then the total). A load checks every section's hash in its dry walk, before the game changes, and names a damaged section. Since 2026-11-22 (the fuzzer's first finding past a load, [ADR 0022](0022-turn-source-and-fuzzing.md#addition-a-loaded-save-runs-on-2026-11-22)) it also refuses, after the hashes, the original's globals a game cannot hold: a presence byte other than 0 or 1, a global every game holds marked absent, a value outside 32 bits.
   - The container (`KP_SAVE_SCHEMA` 2): CORE holds the native save's size (u32) and its zlib stream at level 1, the only form.
   - A save of another version or schema is refused with a message the player sees: gluAll 67, "a version mismatch". A damaged one gets gluAll 66, "invalid or corrupted". These are the statuses of the original's ValidateSaveFile 0x499750.
5. **The schema's fingerprint forces the version.** `kp_save_schema()` hashes the sequence of (section, kind, count) of a COVER walk over a zeroed game. `test_save.save_schema_matches_its_version` keeps a table {version → fingerprint}: a change of the walk fails it until `KP_SAVE_VERSION` is raised and a row is added. It does not see two visits of the same kind and count swapped within a section: the fingerprint has no addresses, so that it is the same on every platform, and such a change raises the version by hand.
6. **Snapshots stay in the process.** A snapshot holds a header (sizes of the records, the counts of the variable parts, the map's size) and the arrays as they are. `kp_restore` refuses another game's snapshot (its header or size) before changing anything; a snapshot damaged past its header is refused with the game partly restored. A snapshot is never written to disk or carried between builds.

## Consequences

- **Every save made before this change no longer loads.** That covers the desktop's, the Fold7's and the web's: the load screens say why (gluAll 67) instead of failing silently.
- **A field added to `KpGame`, `KpImages` or `KpRngGame` fails the coverage test** until it is walked or excluded with a class. A field added to the walk fails the schema test until the version is raised.
- **The hash, the snapshot and the save cannot drift apart:** they are one walk. `test_save.hash_is_the_saves_bytes` checks the streaming hash against the table in the save's bytes and against the test's own reference function, on every configuration of `kp_test` (the host, ASan/UBSan, wasm with a 32-bit `size_t`).
- **What the coverage does not see:** the contents of heap blocks (trigger records, the region graph's slots, contours). Each pointer to them is classified by hand in the table.
- **Times on the development Mac** (release, `bench_combat` at its end, `kp_bench --state`):

  | Operation | Before (ms) | After (ms) |
  |---|---|---|
  | `kp_save` | 15.3 | 3.8 |
  | `kp_load` | 17.5 | 5.4 |
  | state hash | — | 1.4 |
  | snapshot | — | 0.17 |
  | restore | — | 0.15 |

  zlib level 1 adds 6.3 ms to a save and the inflate 4.2 ms to a load. The file shrinks from 8.03 MB to 172 KB.
- **`kp_core` gets no zlib:** the compression lives in `kp_app_core` (`kp_session.c`), which already links it.

## Alternatives

- **Keeping version 1 and migrating old saves.** The old states would not continue as the original in the corrected simulation, and no save has shipped. Rejected (the user's choice of 2026-11-15).
- **Keeping FNV-1a over the bytes.** It cost three quarters of the save's time and could not name a damaged section. Rejected.
- **A visitor interface with a callback per field.** It would have meant a call per value in the hottest walk, and the walk's body rewritten. Rejected for a mode in the existing `Codec`.
- **`offsetof` tables for the coverage.** They are not portable to the C++11 check for every member. The exclusions are built from the live game's addresses instead.
- **A hash of `kp_dump`'s text.** The oracle's goldens depend on that text, and its CRCs took 2.1 ms against the walk's 1.4 ms. It stays as it is, for the goldens.

## Addition: the dump's coverage (stage T3)

Accepted 2026-11-15 with the design of phase T ([docs/phase-turns.md](../phase-turns.md#t3-as-built)). The coverage above binds the walk to the structures; nothing bound the oracle's dump (`kp_dump.c`) to the walk, so a saved field the dump never prints was compared with the original by no golden.

- **The dump is not derived from the walk.** It is a model of the original's memory, by which every golden's CRCs were computed (`tools/native_state.py`). A test binds the two instead: `test_save.dump_shows_the_saved_state` moves each saved field's bytes between two snapshots of real games and checks that the dump's hash changes.
- **The walk's visits get names** (`#expr` and the line, recorded by macros of the primitives' names; logged in the RAW mode only, `kp_snapshot_fields`). The save's bytes, the hash and the schema's fingerprint do not change.
- **Every saved field the dump does not show is listed** in `kp_dump_fields()` with a class: `KP_DUMP_SHOWN` (shown, but changed by no game on a listed record; the test bumps every element), `KP_DUMP_MODEL` (at a known address of the original, not read by the model yet: a TODO), `KP_DUMP_HEAP` (in a heap block the emulator's dumps lack) or `KP_DUMP_KOPRULU` (no counterpart). A new saved field fails the test until the dump shows it or the table lists it.
- **What the oracle did not read goes into new categories** (`step_state`, `sprite_lists`, `links`, `ai_military`), so the earlier CRCs stand. The sprites' and images' lists are a category of their own, `sprite_lists`, since a viewer's selection circles and ground marker change them: a script's `sim.crc` leaves it aside with `sprite` and `image`. The plays' fingerprint covers the first version's 40 categories only: the plays' step dumps are gone and a later category would break all their goldens. The replays, whose dumps are kept, also keep `fingerprint_later` over the rest.
- **Consequence:** the new categories found two differences from the original at step boundaries (the RNG's switch after the start, the IScript contexts' null values), both fixed.
- **Later (2026-11-22): the model's rows closed.** The image pools' free lists, the lone and fog thingies, the receding creep's chains and weapons.dat's forward offsets went into five more categories ([golden-oracle.md](../golden-oracle.md)): `image_pools` and `fog_thingies` with the local interface's in `sim.crc`, `lone_thingies`, `receding` and `dat_writes` with the game's. The path searches' working state and the requirements' lazy offset tables got a class of their own, `KP_DUMP_SCRATCH`: what no later step reads as it was left. Each search sets its nodes, heaps, hash, geometry and swept areas up before it reads them. The offset tables are a cache of the built-in tables, which the original's interface fills when it checks its buttons, so the oracle's runner would differ from the original there with no difference in play. Comparing them would compare the order of searches and the interface, not the game. The test checks the claim instead: each state of the 46 games takes all these fields from its neighbouring state, both ways, and runs 128 steps on to the same dump as without them.
