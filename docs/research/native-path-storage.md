# Native path storage and waypoint reuse (1.17)

The original stores a unit route in a finite pool of128-byte records. **6327 complete cases and7921 original entry returns pass**, covering all point/region counts0..50, selected packed payloads, allocation failure, restoration, reuse and waypoint policy. Search outputs are explicit fixtures; this batch does not execute local path construction or walking.

Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. [Probe](../../tools/research/probe_native_path_storage_117.py), [results](evidence/native-path-storage-probes.json), [static witnesses](evidence/native-path-storage-static.json). All addresses below are preferred VAs for this target.

## Allocation and truncation

`0x42c840` receives pointers to the context's WORD region count `+0x86` and WORD point count `+0x152`. Let these counts be L and S. The header occupies32 bytes; region IDs require2 bytes each and points require4 bytes each:

```text
if 2*L + 4*S > 96:
    if S >= 24: L = 1; S = 23
    else:       L = 2*(24-S)
request_size = (2*(L + 2*S) + 35) & 0xfffc
```

Both count pointers are changed **before** `0x48e8d0` attempts to pop a record. An empty pool therefore returns0 with the reduced counts still written. Complete packer `0x48e730` also returns0 without changing the prospective record when allocation fails. The4 failed-packer cases distinguish these two effects.

The original pop checks requested size with a signed comparison against128. Raw size fixtures −1/0 are accepted when a record exists;129 and `0x7fffffff` are rejected without changing the free head. Negative sizes are deliberately supplied leaf inputs, not a claimed output of the normal nonnegative-count allocator.

Pool base is global `0x69b574`, free head is `0x69b570`, and the stride is128 bytes. A free record stores its next link as a one-based index:0 means null; otherwise `base + (index-1)*128`. Pop reads that link and updates the global head. It does not clear the returned record. Pool creation by an OS allocator is outside this batch.

## Packed representation and restoration

Complete `0x48e730` copies these fields from the explicit search context:

| Record offset | Written value |
|---|---|
| `+0x0c` DWORD | Frame counter `0x52651c` |
| `+0x18` BYTE |0 |
| `+0x19` BYTE |0, retry timer |
| `+0x1b` BYTE | Low byte of full region-chain count from context `+0x154` |
| `+0x1c` BYTE | Reduced region count L |
| `+0x1d` BYTE | Signed minimum of context current-region index and L |
| `+0x1e` BYTE | Reduced point count S |
| `+0x1f` BYTE |0, current-point index |
| `+0x20` onward | First S points, followed immediately by first L region IDs |

The actual copy uses the unchanged original REP implementation selected by CPU flag `0x6d319c=2`. Truncation keeps array **prefixes**. This packer does not replace the final retained point with the original destination. Fields `+0..+0x0b`, including destination/next point, are populated by other callers; they are not initialized by this function.

`0x48e7e0` restores a path into the larger context. It takes source coordinates from the unit's current Sprite and destination coordinates from record `+8`; it restores region IDs to context `+0x20`, points to `+0x88`, counts to WORDs `+0x86/+0x152`, point index to `+0x150`, full region-count low byte to `+0x154` and the current-region index to `+0x84`. The stored current-region BYTE is sign extended: the selected index −1 remains `0xffff` in the context. The supplied range includes−1,0,1,23,49; arbitrary malformed counts and overflowed signed indices are not claimed.

Context `+0x154` is a **count**, not the partial-path flag. The complete [long-path producer](native-region-paths.md) writes the full parent-chain count's low byte there; its partial flag is instead `+0x1c`. The selected codec fixtures use full-count bytes0/1 to test transmission, without claiming all fixture count combinations are naturally produced. Local-path continuation compares this byte with the retained region count to decide whether the original long chain fits in the stored prefix.

Each of360 payload cases runs actual pack→unpack→`0x48e880` unit release→`0x48e8d0` reuse. Release clears CUnit path pointer `+0x100` and pushes the record at the free head. The immediately following pop returns the same record. The separate [Move-link probes](native-trigger-move-links.md) establish disposal through `0x42c8a0 → 0x48e910`, including its survival after placement rollback.

## Retry and waypoint policy

Setter `0x48e970` writes DL to a present path's BYTE `+0x19`. Poll `0x48e950` returns1 only when a path exists and the **old** timer is0. A positive timer is decremented and the call returns0. Timer1 therefore requires one further poll after reaching0. All256 byte inputs are tested with two successive polls; absent-path setters return without writing and absent-path polls return0.

Complete waypoint reuse helper `0x48ee40` follows this order:

Its next point is the packed coordinate at **path+4**, reached through CUnit`+0x100`, rather than either CUnit waypoint pair. [Complete consumer/state17](native-path-progress.md) now connects this predicate to actual waypoint production and retry scheduling.

1. Missing path returns0.
2. Path next point equal to current Sprite position returns0.
3. Next point equal to current movement target returns1.
4. Nonzero movement-type byte `CUnit+0x27` returns1.
5. Original approximate distance greater than16 returns1.
6. Twice that distance at least the unsigned wrapped heading difference returns1.
7. Before the final retained point, return0.
8. At the final point, perform actual pixel-region lookup and return1 only if the source region differs from the stored current-region ID.

The480 grid cases use movement types0/1/2, cardinal distances0/1/8/16/17, heading differences0/2/16/17/32/33/64/128, last/nonlast point and matching/nonmatching region. Six additional equal-target cases prove current-position equality wins over movement-target equality. A uniform region raster is an explicit fixture; actual region lookup executes without replacement. These inputs do not establish the natural production of every path state or a complete turning/repath loop.

## Reproduction and limits

| Family | Complete cases |
|---|---:|
| L/S0..50 × empty/nonempty pool |5202 |
| Raw signed pool size × three free-list lengths |18 |
| Failed packer with count truncation |4 |
| Pack/unpack/release/reuse |360 |
| All retry timer bytes |256 |
| Waypoint heading/region policy |480 |
| Waypoint equals move target |6 |
| Absent-path guards |1 |
| **Total** | **6327** |

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_path_storage_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --static-out docs/research/evidence/native-path-storage-static.json \
  --execute-original-code --out docs/research/evidence/native-path-storage-probes.json
```

Default mode is static only. Unknown executable hashes are rejected before PE parsing/emulation. Runtime is Rosetta x86_64 Python3.9.6 / Unicorn2.1.4. All executed PCs must match decoded instruction boundaries in13 reviewed blocks,376 instructions; each entry is bounded to100000 instructions and checked for original return/stack cleanup. No helper stubs, Windows imports, WinMain or guest host-file/network calls execute. The probe pins its three source dependencies and has SHA-256 `1ae91a817f4ad59d5af464b3832a04913eb90fdf618fd7c1592c6fc3f4678952`.

The pinned [OpenBW path allocation and packing](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L10824-L10856) provides a reconstruction lead. The executable calls above establish this local profile independently. Complete local search, blocked-target adjustment, produced waypoints, dynamic invalidation, finite-pool initialization and the physical movement state machine remain in the [completion ledger](completion-ledger.md).
