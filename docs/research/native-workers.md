# Native 1.17 workers, town migration and construction selection

This batch establishes worker transfer and pool insertion, the map's direct/split region lookup, region restrictions on repair selection, racial worker/gas selection, and several construction rejection rules. Original instructions confirm that a donor's minimum-worker gate is checked once before its worker loop: an admitted donor can subsequently lose every eligible worker. Repair selection uses absolute missing whole HP and preserves the first equal candidate; the first available unfinished building overrides damaged finished buildings.

The target is the local StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. Addresses are preferred VAs and names are analytical labels. [37 static blocks](evidence/native-workers-static.json), [374 original-instruction cases](evidence/native-workers-probes.json), [static extractor](../../tools/research/inspect_native_workers_117.py), [probe runner](../../tools/research/probe_native_workers_117.py).

The probes execute actual original callees in private Unicorn x86 memory, with no helper replacements, imports, Windows process or complete game launch. Several probe families stop at specified original instruction boundaries before unresolved operations. Those fragments establish selection or rejection conditions, not completed construction or repair. Synthetic out-of-range race values, count wrap and a drained donor are boundary observations; their occurrence in authentic campaign play is a separate question.

## Direct and split region lookup

`0x481b10` accepts signed pixel X in ECX and Y in EDX and returns the region in AX. The pathing object pointer is `0x69b584`. For ordinary nonnegative coordinates:

```text
cell = uint16(pathing + 0x0c + 2 * ((y / 32) * 256 + x / 32))
if cell < 8192:
    return cell
bit = 4 * ((y / 8) & 3) + ((x / 8) & 3)
record = pathing + 0x1400c + 6 * cell
return uint16(record + (4 if uint16(record) & (1 << bit) else 2))
```

The encoded cell indexes the split records with its **full value**, including the 8192 threshold. Each split record has a 16-bit bitmap and two 16-bit region IDs. Bitmap bit order is X first, then Y; set bits choose the second ID. Direct lookup is a 32×32-pixel tile; a split tile contains sixteen 8×8-pixel cells. Native signed division is truncation toward zero rather than floor. There is no coordinate bounds check in this accessor.

The 70 lookup cases cover direct tile edges, the final 256×256 tile, negative values close to zero, both split encodings 8192/8193, all sixteen bitmap bits, and both results. These establish lookup, not generation of pathing regions, connectivity, long paths or collision rules. `0x481ba0` reads signed sprite coordinates at +0x14/+0x16 and tail-calls this lookup.

## Building region availability

`0x4aeaa0` returns zero for a null unit, a null sprite, or current order zero (the real inactive predicate at `0x41fe40`). For an active unit it obtains its region, then reads `0x6b1a74[unit.owner] + region*0x34`. It rejects:

- Region state byte +5 equal to 3.
- Nonnull air target at +0x1c or ground target at +0x20.
- Region flag byte +8 containing `0x20`.

Other tested state/flag values pass. The field meanings are independently correlated with the region target writer in [combat AI](native-combat-ai.md); the exact offsets and comparisons are direct native evidence. This function tests AI region state. It does not itself test Terran race, mechanical properties or whether a building can be repaired.

## Repair and unfinished-building selection

`0x4852b0` receives worker ECX and town EDX and walks the town building-AI list at +0x14 in forward order. Each record's +0x10 points to a unit. A nonnull unit +0x80 (builder/related unit) skips that unit; the region predicate above must also pass.

For a completed building the score is maximum whole HP minus current HP rounded upward to whole HP. The maximum comes from the signed DAT dword at `0x520708 + id*4`, shifted right by eight. If that maximum is zero, the routine substitutes rounded current HP, or 1 when rounded current HP is zero. It selects only when the new score is **unsigned greater** than the previous score, initially zero. Equal deficits retain the first candidate. The actual arithmetic is signed shifts with 32-bit addition/subtraction and an unsigned comparison; ordinary valid positive HP permits the simpler whole-HP explanation.

A qualifying unfinished building is selected immediately and ends the scan, even if an earlier completed building was more damaged. Thus the routine chooses the first available unfinished building, rather than the unfinished building closest to completion.

If a target is selected, the remaining native body calls order helper `0x42a380` with order `0x22`, writes target +0x80=worker, and returns 1. The seven original selection probes **stop at `0x485369` before that order call**. They confirm selection, list ties, occupied-target skipping, HP rounding and zero-maximum fallback, not execution of the order or the later repair FSM.

When no target is selected, an empty `town+0x28` claims this worker as the town building SCV. Only the claimed worker calls `0x432e00`, a nearest-unit search wrapper with radius `0x800` and callback `0x432870`. The callback tests same owner, excludes Worker/Building DAT properties, requires candidate +0x5c to equal the worker, then calls connectivity and order-validity helpers. Complete search enumeration, callback eligibility and the fallback's order execution remain dependencies. This branch must not be replaced by an arbitrary nearest damaged building.

## Worker-AI insertion and removal

The worker branch of `0x484cd0(town, unit)` is selected by DAT property `0x8`. It pops the free head from the shared worker pool at `town+8`, pool +0x5dc0. An empty free head leaves the worker/town state unchanged. Otherwise it prepends the record to town +0x0c and initializes:

| Field | Native write |
|---|---|
| WorkerAi +8/+9 | Type 2, target-resource code 1 |
| +0x0a/+0x0b | Reassign count 0, wait timer 0 |
| +0x0c | Current elapsed game-time counter `0x5349d8` |
| +0x10/+0x14 | Unit and town pointers |
| Unit +0xc8 | Zero |
| Unit +0x134 | WorkerAi pointer |
| Town +0x1a | Incremented as an 8-bit value |

Native byte increment wraps 255 to 0; the synthetic boundary probe observes that operation. AISE's `saturating_add` differs at the boundary. These probes do not establish that 255 workers in a single original town is a normal reachable state.

Removal `0x486af0(unit, 0)` runs resource-reference cleanup, unlinks a worker through `0x483b70`, returns its record to the pool, decrements the donor's worker byte, clears town main-building/building-SCV pointers if they equal the unit, checks town emptiness, and clears unit +0x134. The successful migration probes execute these original callees. Their donor towns contain a non-Overlord building, making the native empty-town predicate return false. Complete town deletion and the special migration of worker/Overlord-only towns remain separate contracts.

## Resource-reference cleanup

`0x486000` acts only for units with DAT property `0x2000`. It visits the town lists of **all eight players**. For each town it clears matching pointers at +0x2c/+0x30/+0x34/+0x38. If town resource-area byte +0x1c is nonzero it sets refresh byte +0x1d to 1, whether or not one of those four references matched. Two original cases check all players and both property branches.

## Worker migration

`0x486480` receives recipient town ECX and requested transfer count EDX. Its rules are:

1. A recipient with in-battle byte +0x1e nonzero returns immediately.
2. When recipient worker count +0x1a is nonzero, count>=limit (+0x1b) rejects; otherwise the requested transfer is capped to the remaining limit. A recipient with zero workers bypasses this limit check and cap.
3. A zero resulting request returns.
4. It finds the final town in that player's list (`0x69b51c + player*8`), then visits donor towns backward through +4.
5. A donor must differ from the recipient, have more than one worker when it is visited, have a nonnull main building +0x24 accepted by `0x424460`, and, when the recipient's **current** worker count +0x1a is nonzero, have more workers than its own limit. The recipient count is re-read for every donor (`0x48651a`), and each transfer increments it (`0x484cd0` at `0x484d54`). An initially empty recipient therefore stops bypassing the donor-limit gate after the first donor has supplied any worker. Corrected after the function atlas ([P29 `0x486480`](atlas/p29.md)); the earlier text said "when the recipient was nonempty".
6. It walks each admitted donor's workers forward. Current orders `0x53,0x57,0x58,0x1e,0x21,0x1f,0x46,0x1a,0x19` and noncompleted workers are skipped. Eligible workers are removed and inserted into the recipient until the request reaches zero or no workers remain.

The donor count/limit gates are checked once per donor, not for every transferred worker. The recorded empty-recipient case requests five workers from a donor initially holding three and transfers all three. A separate two-donor case shows that the final town supplies both workers before the earlier donor is considered. This is list traversal, not donor distance selection.

Every transferred worker is prepended to the recipient. Multiple transfers therefore appear in reverse order in its new worker list. Since allocation later walks that list, migration changes later resource assignment order. The probes check transferred identities, both count bytes and recipient traversal, rather than only the number moved.

## Worker resource decisions and waits

Decision helper `0x487580` returns for a null AI pointer. A nonzero WorkerAi wait byte +0x0b decrements and returns without changing last-update +0x0c. Zero wait writes the current elapsed counter and attempts, in order, Terran repair/resume selection, spending-queue construction, main-building eligibility, then resource selection. Four original cases establish wait values 1/2/10/255 and the unchanged timestamp.

Assignment byte +9 has a mineral path for code 1. For code 2 the worker first requests the first class-3 nonempty gas building; code 3 requests the second; every other non-1/non-2/non-3 code requests the third. Missing occurrences fall back by checking class-3 occurrences 1,2,3, then class-2 depleted occurrences 1,2,3. This counts **matching occurrences**, not raw town gas slots. Worker assignment ordinals and gas classification codes are separate encodings.

Resource-order helper `0x485f20` tests carried **mineral mask2** before **gas mask1**, choosing ReturnMinerals90 (`0x5a`) before ReturnGas84 (`0x54`). The earlier wording swapped the resource names; the unchanged native masks/order bytes are corroborated by [genuine mineral cargo/deposit](native-harvesting.md) and [whole-object gas cargo/return](native-gas-harvesting.md). Without cargo it accepts the preferred target only if nonnull, completed and not raw Geyser ID 188, otherwise tries the supplied fallback. A valid final target calls order helper `0x42a380` with order `0x4f` and then marks target byte +0xd9. When both preferred/fallback targets are invalid it returns without issuing an order.

Twenty original decision cases execute the worker helper and all selected classification/fallback callees up to the native order-helper entry. They vary assignment codes 2–5, mixed nonempty/depleted gas, absent slots, and all-depleted/all-absent towns, and inspect the selected resource pointer and order. They **stop before issuing the order**; harvesting movement, queues, cargo acquisition and return remain separate order contracts. The mineral branch also reuses the worker's existing mineral target, selects an alternative when the town mineral is marked, and calls `0x431fe0`; that search body remains unresolved here.

### Main-building eligibility

`0x424460` requires grounded status bit 2 and ResourceDepot DAT property `0x1000`. Its completion gate accepts a completed unit; incomplete Lair/Hive IDs 132/133; or incomplete Hatchery 131 with `0x43c040` true. That helper tests the current build-queue entry for IDs 133,132,137,144,146 while the unit is incomplete. Normal Hatchery morphs to Lair/Hive pass. Synthetic tests of the other queue IDs describe the literal shared predicate, without claiming those are normal Hatchery builds.

## Racial types and construction admission

Player race is read at `0x6726d1 + player*36`. Helpers `0x486070` and `0x486100` map 0 to Drone/Extractor (41/149), 1 to SCV/Refinery (7/110), and every other byte to Probe/Assimilator (64/157).

`0x4860a0(worker, requested_id)` reads DAT group flags at `0x523c40`. If the request has bit 2, the worker needs bit 2. Otherwise bit 1 requires worker bit 1; otherwise worker bit 4 is required. A mismatch writes native error ID 25 through `0x4b38b0` and returns zero. Thirty original cases distinguish mask priority, combined masks and failure writes.

Construction service `0x485400(town, worker)` requires spending-heap top kind 3. The top context must equal WorkerAi town, **except requested unit 109 (Supply Depot)** bypasses that town mismatch. Reassign count must be <3, the race predicate must pass, native requirements `0x4b32a0` must return exactly 1, and resource helper `0x4826d0` must pass. Six original cases cover early rejection; one stops at `0x485473` to confirm the Supply Depot exception before requirements execute.

The static body then distinguishes racial gas buildings, Bunker 125, Missile Turret 124, Hatchery 131, Creep Colony 143 and Shield Battery 162. Gas construction chooses the first class-1 raw geyser in town slot order. Other branches obtain a seed point and call the placement wrapper below. Successful native paths pop/service spending state, choose SCV/Drone/Probe order `0x1e/0x19/0x1f`, issue the order and update the build queue. [Production](native-production.md) owns request regeneration, requirements and costs; those operations must connect to actual placement and unit-order state machines.

## Placement wrapper and search skeleton

`0x4962c0` receives worker, requested ID, seed point, output point, throttle flag and distance parameter. If throttle is enabled and `PlayerAi+0x211` is nonzero it returns zero immediately. Missile Turret 124 overrides the distance parameter to 40; otherwise it passes the caller value. Failed searches with throttle enabled set +0x211 to 1 through `0x482ea0`. The current original probe establishes the already-blocked rejection only.

Search `0x494fc0` first handles addon placement using native validation. Its non-addon branch dispatch table maps IDs 106–156 to nine branches. Base buildings 106/131/154 disable one filtering phase; several Terran addons substitute another footprint ID; Pylon 156 attempts a dedicated seed-point search; Bunker/Creep Colony can select a different region. The exact dispatch bytes/targets are preserved in the static evidence.

It builds a 64×64 candidate-byte grid around seed tile `(x/32-32, y/32-32)`, scans rows from top to bottom and columns left to right, and evaluates each nonzero candidate through `0x496930`. Equal scores retain the earlier cell. It maintains best candidates on either side of the distance parameter and prefers the at-or-below candidate when one exists. A separate region anchor may trigger a second scoring pass before failure. Final output is the chosen tile origin plus half the signed DAT placement-box dimensions.

Distance `0x496930` compares native regions at both points. For the same region it returns the original approximate pixel distance truncated to tiles (`/32`). For distinct regions it checks 16-bit connectivity-group fields at pathing +0x449fe+region*64; different groups return sentinel **9999**. Otherwise it calls path search `0x491300`, potentially measures a sequence of region centers, and returns the accumulated tile distance. Six original cases execute same-region and disconnected-group paths with the actual lookup/distance callees. Connected distinct-region search and waypoint selection are not part of those cases.

The candidate radius filter `0x4976d0` visits the existing 64×64 bytes in row/column order. It preserves zero cells without measuring them. For a nonzero cell it computes the same native region/path distance from the supplied seed to `(seed.x-1024+column*32, seed.y-1024+row*32)` and clears the cell only when that tile distance is **unsigned greater** than the limit. Equality is retained; accepted cells preserve their existing byte values. Six original complete-function calls compare the entire candidate grid for radii 0/1/2/31/32/UINT32_MAX, using actual region/distance functions and same-region candidates. The bounded instruction limit is 200,000 for this 4,096-cell loop, versus 10,000 for ordinary leaf calls.

### Placement bounds and zero-size footprints

The shared placement validator `0x412f10` reads signed placement-box dimensions at `0x522728+id*4`, converts them to tile dimensions with signed truncation toward zero, and rejects negative tile X/Y or `x+width > map_width` / `y+height > map_height`. Its rejection returns error 3 and fills all 48 bytes of the selected placement-state entry at `0x675188 + entry*48` with 3 through the **actual EXE memset**. Original cases verify exact edge rejection and guard bytes outside that buffer.

This native body has **no separate `x >= map_width` or `y >= map_height` test**. With a zero-size footprint dimension, the corresponding coordinate equal to the map dimension passes these bounds. Three accepted probes stop at `0x412fb3` before later placement conditions; they do not claim that the complete placement succeeds. The pinned [Teippi placement implementation](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit_build.cpp#L272) deliberately adds the separate coordinate checks. Copying that modified rule would differ from this original arithmetic boundary. Actual campaign footprint dimensions and the rest of the validator determine whether the distinction matters in supported gameplay.

### Tile-flag placement predicate

`0x412870` reads a row-major uint32 tile array through `0x666454`, with native map width `0x5264b4`. Its inputs are requested unit ECX, tile X EDX, and stack `[tileY, player, packedWidthHeight, placementEntry, checkVision]`. It writes per-cell error bytes into `0x675188 + entry*48 + row*6 + column` and returns the greatest per-cell code. The 160 original complete calls check the literal masks and ordering on 2×2 footprints, preserving the other 44 entry bytes and outside guards.

For each tile the established rule is:

```text
human = player_type == 2
mask = 0x20800000
if not (DAT.group_flags[id] & 1):
    mask |= 0x40000000 if checkVision else 0x00400000

if human and tile_flags & (1 << (player + 8)):
    code = 10
else if not checkVision and human and tile_flags & (1 << player):
    code = 0
else if tile_flags & mask:
    code = 0 if 194 <= id <= 199 and tile_flags & 0x80000000 else 4
else:
    code = 0
```

The tests distinguish human/computer controller bytes, both vision-flag values, Zerg-group bit 1 versus other groups, all three mask additions, the player bit, player+8 bit, and the special inclusive IDs 194–199. The numeric flag rule is original evidence. Its upstream flag generation and exact fog/terrain naming still belong to the map/vision contracts; an intuitive name must not change these comparisons.

The **ResourceDepot property branch is excluded from those executions**, with that DAT bit explicitly clear. When a ResourceDepot tile reaches code zero, the remaining native body may check a resource-neighborhood predicate at `0x4180a0` and/or enumerate units in a 224-pixel neighborhood with callback `0x413370`, producing code 2. Those are real unresolved callees, not replaced results. Creep, psi and unit occupancy are separate functions called by the shared validator; passing this tile predicate alone does not make placement valid.

This is a recovered search skeleton with recovered radius, bounds and selected tile-flag rules. Candidate-grid construction `0x495bb0`, extra filtering `0x498930`, region/seed selection, connected-region distance search, ResourceDepot neighborhood, occupancy, power, creep and upstream tile-flag generation still need their own complete contracts. The 374-case worker batch does **not** close full placement, harvesting, repair progress, construction interruption, town deletion, or the complete allocator. Those remain in the [completion ledger](completion-ledger.md).
