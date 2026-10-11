# Native 1.17 trained-unit placement, Show, vision and cancellation

This report follows an **already initialized, hidden child** through original Train completion, placement, relocation, spatial insertion, visibility, completed-unit AI assignment and rally. It also closes cancellation through actual Death-order issuance. It does not establish successful allocation of that child or execution of its later Death animation and recycling.

The target is StarCraft **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at image base `0x400000`; subtract the base for RVAs. The [static evidence](evidence/native-unit-creation-static.json) contains **72 blocks, 4,498 decoded instructions and no undecoded tails**. The [original-code evidence](evidence/native-unit-creation-probes.json) contains **543 passing cases: 530 complete returns and 13 explicit prefixes**. Every executed helper is original code, with no replacements or stubs. The guest runs under Unicorn 2.1.4 in an x86_64 process under Rosetta, with a 50,000-instruction bound per invocation. It executes no game loop, WinMain, Windows API, import or guest host-file/network operation.

The fixtures use real native CUnit pool addresses, private object pools, valid main-image pointers and **empty linked image lists**. Consequently, animation/drawing fanout genuinely returns through its empty-list branches; it does not establish populated image/iscript behavior. Producer/child owner3 and viewer0 exclude local completion voice and UI branches. Observer completion additionally uses the real sound-disabled gate. Terrain, region topology, fog flags and pools are explicit caller inputs. This is original instruction execution for those profiles, not a running campaign or general parity claim.

[native-training-lifecycle.md](native-training-lifecycle.md) covers admission-to-Train progress, allocation failures and assignment helpers; [native-primary-orders.md](native-primary-orders.md) covers the real issuer used by rally and Death. [native-placement.md](native-placement.md) supplies connectivity and spatial query dependencies. [native-trigger-unit-actions.md](native-trigger-unit-actions.md) supplies the reviewed Death helper; [native-trigger-unit-counts.md](native-trigger-unit-counts.md) supplies the original spatial collector and native pool resolver.

## Evidence coverage

| Cases | Complete returns | Profile |
|---:|---:|---|
| 182 | 182 | Hidden Train completion: nine stock flyer types, four stock ground types, human/computer controllers, six rally relations; Observer cloak completion; generated-mask fog cases; initial placement cases. |
| 36 | 36 | Original vision grid and record generation, sight0..11 and map strides64/128/256. Each case also records the separate complete grid-generator invocation. |
| 72 | 72 | Original fog reveal, sight0/1/7, ground/air, map edges and a nonempty height barrier. |
| 180 | 180 | Complete ground-footprint checks against nonempty terrain edges, all four sides and inclusive span boundaries. |
| 12 | 12 | Three real spatial insertions followed by indexed movement, including duplicate coordinates. |
| 8 | 8 | Direct placement success/failure, flight, walkability and terrain-displacement flag. |
| 1 | 1 | Show on an already visible unit is a genuine no-op. |
| 20 | 20 | Cancellation of an incomplete hidden Marine, Wraith, Zealot or SCV at every circular queue head, through actual Death issuance. |
| 19 | 19 | Air-density arithmetic, saturation and RNG; complete stock flyer Train with density enabled. |
| 12 | 0 | Deliberate control-boundary snapshots before Observer's `0x41d200`; the corresponding complete path is separately covered above. |
| 1 | 0 | Vision-template allocation wrapper stops **before `0x4d001c`**, the Storm allocation import trampoline. |

The main human/computer/rally matrix has empty vision descriptors and therefore establishes placement, lists, orders and queue effects without new fog writes. Separate complete Train cases use **original-generated, nonempty descriptors** and verify the exact changed tiles, sprite visibility byte and queue completion. The 72 direct reveal cases compare the entire 128×128 tile array against an independently evaluated dependency/height specification.

Stock unit properties, sizes, supply, sight, idle orders and shields come from effective retail `arr\\units.dat`, SHA-256 `da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba`. Stock order interruptibility, highlight and subunit inheritance come from the effective retail `orders.dat` accepted by the primary-order inspector. Host decoding uses pinned PyMS commit `bfc5d3aad0b5614a5aff72c223f8efa00afddfa4`; the runner checks both DAT provenance and the checkout commit. The synthetic initial unit state remains separate from these stock DAT fields.

## Caller contracts

| Entry | Register/stack contract | Effect |
|---|---|---|
| `0x47dc30` | ECX = unit; EDX = desired two-WORD point; stack = output point, optional four-WORD search rectangle, terrain-displacement flag, additional point; pops16 | Search for a legal placement. |
| `0x47e340` | ECX = unit; EDX = output point; stack = left x, top y, width, height, bounds pointer, additional point; pops24 | Scan one expanding rectangular ring. |
| `0x42ed50` | ECX low16 = unit ID; EDX low16 = x; stack low16 = y; pops4 | Check map bounds, walkability and terrain-edge clearance. |
| `0x4891c0` | ECX = terrain collision context; stack low16 = unit ID/radius code; pops4 | Prepare signed directional extents. |
| `0x48b780` | ECX = prepared collision context; stack = x, y; pops8 | Check four sorted terrain-edge arrays. |
| `0x43a270` | ECX = unit; stack = x, y, each consumed as signed low16; pops8 | Clamp and relocate existing unit/sprite state. |
| `0x422f60` | ECX = unit | Show hidden unit, reveal vision, insert spatially, update movement and lists. |
| `0x4195a0` | ECX = native-pool unit | Insert its four DAT-derived spatial bounds. |
| `0x419820` | ECX = indexed unit; stack = signed dx, dy; pops8 | Move four bounds and update neighboring units' index fields. |
| `0x466990` | ECX = sight descriptor; EDX = preallocated six-byte grid cells | Generate covered cells; EAX = total count. |
| `0x466bd0` | ECX = sight descriptor; EDX = generated grid; stack = tile stride; pops4 | Populate preallocated 20-byte ordered ray records. |
| `0x46bbb0` | ECX = signed x; EDX = signed y; stack = airborne flag, player mask, sight index, additional argument; pops16 | Reveal fog using the prepared descriptor and original callback. |
| `0x41d200` | ECX = unit; stack = sound ID; pops4 | Begin cloak; update detection and timer. |
| `0x481760` | ECX = unit | Add air-density entry when status `0x20000` permits it. |

The placement caller `0x426600` only searches when the child's sprite is hidden. It passes the sprite's current position, output storage, rectangle0, terrain-displacement0 and additional-point0. On success it calls `0x43a270`; an optional subunit tail is outside the complete child profiles here. On failure it reports and clears error word `0x5da398` through the real nonlocal notification gate, then returns0.

The complete chain exercised from `0x439310` is:

```text
Train -> ProgressBuild(timer already zero)
      -> prefinish -> 426600 -> 47dc30 -> 47e340 if required
      -> 43a270 relocation -> postfinish -> optional permanent cloak
      -> 422f60 Show -> vision/visibility/spatial/movement/lists
      -> default primary order -> completed-unit AI -> rally
      -> clear current queue slot and metadata -> advance circular head
```

## Placement ordering and factory collisions

`0x47dc30` first checks the requested DAT bounding rectangle against the spatial collector and placement map bounds. The default search rectangle is desired position minus128 through plus127. Its bounds clamp to x0..map_width−1 and y0..map_height−33. The initial candidate can succeed directly for a flyer, or after `0x412c30 -> 0x42ed50` for a ground unit. **The connectivity gate belongs to ring candidates; it is not called on every direct initial candidate.**

If the initially unobstructed ground candidate fails its footprint test and terrain displacement is disabled, placement returns0 with error60. Complete tiny-rectangle cases distinguish this from allowing a terrain search. The output point remains untouched on failure. Flight bypasses terrain footprint checks and succeeds for the same unwalkable input.

The collision callback `0x47ea20` rejects dead candidates through `0x41fe20`, and ignores either unit with status `0x200000`. A wanted powerup, DAT property `0x800`, is blocked by a grounded building or another powerup. Otherwise it normally compares airborne status4, but these producer exceptions force a collision:

| Obstacle | Wanted type IDs |
|---|---|
| Starport114 | Wraith8, Dropship11, Science Vessel9, Battlecruiser12, Valkyrie58 |
| Robotics Facility155 | Shuttle69, Observer84 |
| Stargate167 | Scout70, Carrier72, Arbiter71, Corsair60 |

Thus a stock Wraith cannot finish at the center of its Starport merely because it flies. This is verified through the actual stock Train call, not just a switch inventory.

The initial horizontal radius is `max(8, obstacle.right + wanted.left + 2)` and the vertical radius is `max(8, obstacle.down + wanted.up + 2)`, with the original 16-bit conversions. Without an obstacle the initial radii are8. Each failed outer iteration grows both radii by16, and both diameters by32.

`0x47e340` aligns its ring boundaries to an 8-pixel grid, clips them to the search rectangle, and scans **bottom left-to-right, right bottom-to-top, top right-to-left, left top-to-bottom**. Encountering an obstacle can jump past its edge and realign to8 rather than advance one grid cell. For a sufficiently wide ring, the first bottom candidate is shifted by the wanted unit's rounded-up full width. Candidate order and skip distances determine the selected point; this is not a nearest-distance search.

With producer and child initially centered at `(200,200)`, stock Wraith/Starport completes at **`(168,256)`**, Marine/Barracks at **`(152,248)`**, and Observer/Robotics Facility at **`(168,240)`** in the specified otherwise empty fixture. A synthetic symmetric 8-pixel flyer profile instead selects `(200,224)`. These differences demonstrate why stock extents and ordered search must be retained.

Ring candidates invoke `0x48e980`; unit byte `+0x105 & 1` enables the actual connectivity-group comparison `0x481d00 -> 0x481b10`. Ground candidates then require the footprint check. The main completion profiles use a single connectivity group. Additional-point0 skips the separate `0x4928f0` dependency; nonzero additional-point behavior is not inferred from these calls.

## Ground footprint and terrain edges

`0x412c30` is a trampoline to `0x42ed50`. That function checks signed low16 x/y against DAT extents: left/top inclusive, right/bottom strictly inside **full** map width/height. The outer placement search separately reserves the bottom32 pixels.

Pixel walkability `0x42ea10` maps signed coordinates to 32-pixel tile indices. The tile flag priorities are:

1. `0x400000`: accept immediately.
2. Otherwise `0x20000000`: read tile WORD through `0x54323c`, mask to `0x7fff`, select the coordinate's 8×8 subcell and test VF4 low bit1 through `0x66644c`.
3. Otherwise `0x10000`: accept; absent that bit, reject.

The collision context is `*(region_map + 0x97a1c)`. `0x4891c0` prepares signed words `[up, -right, -down, left]` at context `+0x20`, and `[up+1, -right-1, -down-1, left+1]` at `+0x30`. A radius code with bit `0x8000` uses its low15 bits symmetrically. The normal-unit extents preparation executes completely in the footprint cases; the artificial radius-code branch is static evidence here.

`0x48b780` uses four sorted eight-byte edge arrays at context `+0/+4/+8/+0xc`, with signed WORD counts `+0x10/+0x12/+0x14/+0x16`. Each edge stores axis position, span start/end and a byte selecting a prepared extent. Its binary searches separate edges strictly before or strictly after the center. The expanded perpendicular span uses inclusive comparisons.

The 180 nonempty profiles use symmetric half extents8 and center `(200,200)`. Before-center arrays reject axis deltas−8..−1; after-center arrays reject+1..+8. Delta0 is excluded by the directional binary-search partition. Perpendicular span starts208 and ends192 still intersect after expansion; starts209 and ends191 do not. These are actual full calls through bounds, walkability, extent preparation and edge search. **Generation of the terrain-edge arrays is a separate dependency.** The full Train ground profiles use flat walkable flags and empty edge arrays.

## Relocation, Show and spatial/list state

`0x43a270` clamps the desired center to DAT extents through `0x41a4b0`: x is within `[left, width-right-1]`, y within `[up, height-down-33]`. It updates fixed positions, the sprite, destination/waypoint state and path-update flags through actual helpers. Nonnull unit AI can cause a separate goal-reaction callback; the trained child has no AI yet at this stage.

Sprite mover `0x417330` compares the old and new 32-pixel rows. On a row change it removes the sprite from row heads/tails `0x5c51d0/0x5c55d0`, then inserts after the destination row's current head through `0x411930`. The fixtures provide a coherent parent/child sprite row list. Linked image redraw fanout is empty as explicitly configured.

For an unindexed hidden child, `+0x13c` is−1, so its call to `0x419820` genuinely returns without moving bounds. **The subsequent Show performs the first insertion.** Already indexed movement is separately tested with three actual native-pool units, crossing other units and duplicate coordinates; it updates both sorted arrays and every affected unit's bound indices.

Show `0x422f60` returns immediately when the sprite lacks hidden bit32. For a hidden child it clears that bit, handles a valid subunit if present, calls unit vision `0x423260`, sprite visibility `0x438c20`, spatial activation `0x41a050`, movement initialization `0x42c7e0`, hidden-list removal `0x427070`, and active-list insertion `0x425dd0`. The complete child profiles have no subunit and no old allocated path. Nonzero old path freeing through `0x48e910` remains separate.

Spatial insertion `0x4195a0` derives the index from `0x5da250 + index*0x150`. The fixtures use indices1/2/3 at `0x5da3a0/0x5da4f0/0x5da640`; an arbitrary private CUnit address would be invalid for the real resolver. DAT subunit property `0x10` skips insertion. Otherwise the two x bounds enter `0x5c99b8` and two y bounds enter `0x5d03f8`; each record is `[unit_index, coordinate]`. Count `0x5c99b0` is twice the indexed unit count. Bound slots are stored at unit `+0x13c/+0x140/+0x144/+0x148`.

**Insertion places a new bound before existing equal coordinates.** Opening slots uses original CRT memmove instructions and updates displaced units' bound indices. Movement uses the original swap helpers and reorders the four bounds. The movement evidence records the resulting equal-coordinate order rather than imposing a new stable-sort rule. Spatial overlap callback `0x42e670` can mark status `0x40000`; final assertions preserve independently checked status bits rather than treating that collision-update bit as completion itself.

The hidden global list uses heads/tails `0x665af8/0x665afc`; the active list uses `0x665ae0/0x665ae4`. Active insertion uses **after-current-head** ordering, consistent with the established native generic list helper. Completed child insertion after its producer, removal from hidden head, spatial count growth2→4 and circular queue head advancement are verified together.

Movement initialization frees an old path if present, clears byte `+0x97`, and sets unit byte `+0x105` bit0 according to sprite elevation `<12`, preserving its other bits. Stock ground elevation4 and flyer elevation18 therefore establish different subsequent connectivity-gate behavior.

## Original vision templates and the air-mask defect

The executable contains 12 descriptors at `0x508a50 + sight*28`. Width/height are `3 + 2*sight`; the unconditional inner rectangle is3×3. The native grid generator `0x466990` zeroes six-byte cells through real `memset`, fills the central rectangle and its integer circle construction, and counts marked cells. The record generator `0x466bd0` consumes those original marks and writes ordered 20-byte records into supplied storage.

| Sight index | Total records | Inner count | Outer count |
|---:|---:|---:|---:|
| 0 | 9 | 9 | 0 |
| 1 | 21 | 9 | 12 |
| 2 | 37 | 9 | 28 |
| 3 | 61 | 9 | 52 |
| 4 | 97 | 9 | 88 |
| 5 | 129 | 9 | 120 |
| 6 | 177 | 9 | 168 |
| 7 | 221 | 9 | 212 |
| 8 | 277 | 9 | 268 |
| 9 | 349 | 9 | 340 |
| 10 | 413 | 9 | 404 |
| 11 | 489 | 9 | 480 |

Record0 is the center. Later records scan expanding rectangles in directions `(1,0),(0,1),(-1,0),(0,-1)`, starting just above the center. Record `+8` is signed `dx + dy*map_tile_width`; `+0x10/+0x11` are signed-byte dx/dy; `+0x12` is dependency count0/1/2; `+0/+4` are pointers to earlier records; `+0xc` stores the propagation snapshot. The evidence preserves every coordinate and dependency index for all12 shapes, at all three tested strides. It checks native grid/record agreement, symmetry, offsets and earlier-only dependencies. These are supplied buffers for the two complete pure helpers, not allocator import stubs.

The allocation wrapper `0x466e40` determines a maximum grid size and calls Storm allocation twice per template, then frees the temporary grid. Its exact prefix reaches `0x4d001c` requesting **3750 bytes** for 12 descriptors, followed by file pointer `0x4ee64c`, line286 and flags0. The import is not executed. The pure generators are independently complete with valid preallocated buffers.

Unit vision `0x423260` combines owner/shared-vision masks and unit parasite byte `+0x121`. Owners>=8 without parasites and Nuclear Missile14 return without ordinary vision. Special-source handling is through `0x4273c0`. The complete stock completion cases use owner3, configured shared mask and parasite0.

`0x46bbb0` rejects player masks greater than255, selects the sight descriptor, and clears both low-byte current visibility and the next-byte explored bits:

```text
clear_mask = u32(~(player_mask | (player_mask << 8)))
tile_flags &= clear_mask
```

Ground vision calls original height lookup. Required propagation flags are `player_mask | 0x07000000` for source height0, `player_mask | 0x05000000` for height1, or `player_mask | 0x01000000` for height2. The first nine records reveal unconditionally when in bounds. A later record reveals if at least one earlier dependency's snapshot lacks all required blocking bits. Records that are outside the map or remain blocked receive snapshot `0xff`; successful records receive their resulting tile flags. The processing order therefore matters.

**Air callbacks `0x46c570/0x46c5b0` traverse only `outer_count = total−9`, starting at record0. They omit the final nine generated records.** Ground callbacks traverse `inner_count + outer_count`. In particular, sight7 has221 generated records: flat ground can reveal221, while air uses212 before clipping. Sight0 air reveals no tiles. This is confirmed with nonempty original-generated descriptors and exact fog writes, including edge clipping; it must not be silently changed to a full circle in a faithful remake.

OpenBW's pinned [vision consumer](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L12354) explicitly comments on the omitted final nine entries. Its [template generator](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L20912) supplied a source hypothesis; the local original instructions and writes provide the version-specific evidence here. The native field layout and pointer representation are not transferred from that reconstruction.

Sprite visibility uses the unit's placement dimensions, 32-pixel tile clipping and the real rectangle visibility union `0x418d80`. Complete Train cases starting from unrevealed/unexplored tiles verify shared masks0/1/8/12/255/256, resulting sprite byte `+0xc`, hidden flag clearing and queue advancement. Separate direct reveal cases use source positions0/31/32/100/4000/4095 on both axes and a height barrier column; the whole resulting tile array is checked. Source height1/2 and positive parasite/cargo combinations remain additional caller profiles.

## Observer cloak, density and completed-unit assignment

Stock Observer property `0x400000` causes completion to call `0x41d200(unit, sound273)`. Its nonburrowed path marks sprite drawing, sets unit status `0x100`, clears status `0x200`, writes detection sentinel `0x80000000`, computes actual detection, and sets timer byte `+0x91 = 30`. The complete profiles reach those real helpers, including actual spatial detector candidate filtering and the empty special-unit list.

With owner3, shared mask8, parasite0, no detector candidate and no force-detection status, `0x4316b0` returns mask8. `0x4325a0` initializes the sentinel through an 8-bit complement, clears stale targeting for lost players through `0x426b80`, and selects undetected drawing for viewer0. The other players' owner lists are explicitly empty. The owner3 parent/child list is coherent. Image-specific drawing fanout remains the configured empty-list branch. Positive detectors, positive lost-player targeting lists, populated cloak images and burrowed cloak are not claimed from this profile.

Air-density helper `0x481760` requires status `0x20000`, excludes DAT building property1 and Interceptor73, then uses a171-column grid at `0x67cf60`. With no high nibble at unit byte `+0x14c`, it consumes PRNG source37, stores the result's low byte in `+0x14d`, and sets high flags to `(result>>8)&0xf0`, replacing zero with`0xf0`. It clears the low three flag bits, stores signed-truncation x/48 and y/48 as bytes `+0x14e/+0x14f`, and increments the selected density byte up to255. Existing high flags avoid the RNG draw.

Nine direct calls distinguish existing/high-zero flags and densities0/254/255. Ten additional stock flyer Train calls start with status `0x20004`, execute Show's actual density insertion, and verify the new density cell and exact main PRNG state. They retain actual Observer cloak and generated vision where applicable. See [native-prng.md](native-prng.md) for the 32-bit generator contract; the probe's expectation uses exact integer arithmetic.

Completed-unit AI and rally occur **after placement and Show**. The real worker pool assigns SCV/Probe WorkerAI; the real reserved guard assigns Marine/Zealot and ordinary flyer profiles. Observer's special assignment path leaves its AI pointer0. Same-owner rally targets issue Follow49; a point or different-owner target issues Move6; self/zero-x rally inputs retain the stock default idle order. These outcomes use the original order pool/activation chain. Successful Train completion clears current metadata, writes queue sentinel228, advances the head modulo5 and clears producer state/current child.

## Cancellation issuance and remaining boundaries

Cancellation `0x41b810(producer, relative_slot0)` calls `0x41a340` for an incomplete current child, refunds through `0x408460`, then reaches **real `0x42a4d0`**. That helper drops a carried powerup if needed, clears the primary queue, sets user-action bit1, appends Death0, activates it and removes AI. The covered child has no carried powerup, no AI and no subunit, and has a real available primary-order node.

Twenty complete calls cover four stock child types and all circular queue heads, with configured prices50 minerals/25 gas. Stocks become1050/1025, child primary order becomes Death0, and cancellation compacts the parent queue to sentinel228. **Final child user-action byte is0:** the issuer sets bit1, then successful `0x42a610` activation clears it. The cancellation helper itself leaves the parent current-child pointer and secondary state2 intact, and the child remains hidden. Death's later destruction/recycling and its clearing of references are separate lifecycle work; these complete returns prove issuance rather than physical removal.

The remaining creation/completion boundaries are successful `0x424900` image/iscript initialization and owner/list setup; optional subunit success/rollback; populated image fanout; old-path freeing; additional-point placement; nontrivial terrain/region generation; positive detection/cargo/parasite variants; and complete Death execution after cancellation. The selected hidden-child completion paths no longer stop at `0x47dc30` or Observer cloak. The sole import prefix in this batch remains vision-template allocation, whose arithmetic generators are separately complete.

## Reproduction and freshness

Inspector [inspect_native_unit_creation_117.py](../../tools/research/inspect_native_unit_creation_117.py) SHA-256: `991cd331c288f3a0351729407801eebccf11d6db0516624235382cbdc8fb758e`.

Probe [probe_native_unit_creation_117.py](../../tools/research/probe_native_unit_creation_117.py) SHA-256: `b0357fb80501d8ec68cfb3a96cebfb1d92be3ded097f41e97359c3083f05efe1`.

The execution JSON records all imported inspector/probe source hashes, input values, expected and actual outputs, exact prefix boundaries, register/stack arguments, instruction counts and visited original functions. Each complete return also checks stack cleanup against the declared argument count.

```sh
python3 tools/research/inspect_native_unit_creation_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-unit-creation-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_unit_creation_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --units-dat /private/tmp/koprulu-campaign-data/arr/units.dat \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out docs/research/evidence/native-unit-creation-probes.json \
  --execute-original-code
```

Default probe mode prints its execution plan. Unknown EXE/DAT hashes or an unreviewed decoder commit fail before original execution. The report and local evidence do not bundle the executable or retail DAT files.
