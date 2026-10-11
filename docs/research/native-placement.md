# Native 1.17 building placement, creep, power and occupancy

This report follows [worker placement selection](native-workers.md) into the shared building validator and its race-specific and occupancy predicates. The reviewed executable is StarCraft **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. All addresses below are preferred VAs. The [static instruction witnesses](evidence/native-placement-static.json) and [1,338 passing original checks](evidence/native-placement-probes.json) apply to this image.

There are **1,337 complete original-function checks and one post-allocation initializer fragment**. Calls execute original instructions and actual reviewed callees in private Unicorn x86-32 memory; instruction bounds and allowlists reject an unexpected path. Windows imports, original allocation, game entry points and complete campaigns do not execute. Tile flags, border indexes, units and most power masks are explicit fixtures. One separate asset-backed batch uses the original power GRP. This establishes predicate contracts and selected connected validation paths, while generation of terrain flags and spatial indexes, complete candidate-grid search and construction order progression remain separate.

## Shared validator and call order

`0x412f10` receives `ECX=builder`, `EDX=player`, and stack `[tile_x, tile_y, requested_ID, output_entry, vision_mode, include_unseen, ignore_visibility]`. The last two names describe the covered consumers; their literal arguments remain authoritative. It computes signed DAT placement dimensions from `0x522728 + ID*4` and `0x52272a + ID*4`, truncating toward zero when dividing pixels by 32.

The order is:

1. Reject negative tile coordinates or footprint right/bottom beyond the map, code `3`. There is no separate `x >= width` or `y >= height` test; the zero-footprint boundary is covered in [native-workers.md](native-workers.md).
2. With a nonnull builder whose status bits `6` are both clear, call reachability `0x48e980`. A false result rejects with code `7`. Its actual group/bypass branches and selected connected validator calls are covered below.
3. For gas-building IDs `110/149/157`, delegate to `0x413610`, then fill all 48 output bytes with its result. Gas lookup rules and selected connected calls are covered below.
4. For DAT RequiresPsi `0x80000`, call `0x4130f0`. An unpowered result is code `6` and fills all 48 bytes.
5. When the builder is Nydus ID `134`, call the Nydus tile grid, ahead of the RequiresCreep branch.
6. Otherwise DAT RequiresCreep `0x20000` selects the creep grid; other units use the ordinary tile grid `0x412870`, described in [native-workers.md](native-workers.md).
7. Call occupancy grid `0x412360`, using global `0x6a2e80` as a builder override when nonnull. Return the signed maximum of the race-specific grid result and the occupancy result.

The output base is `0x675188 + output_entry*48`, with six-byte row stride. The 120 original shared-profile calls cover ordinary, creep, Nydus, powered and unpowered profiles, human/computer controllers, both vision modes and selected flag boundaries. Twelve additional shared calls cover real builder reachability and gas validation. Empty spatial indexes still execute the real query and release helpers. Rejection and gas-result paths execute the original `memset`, without an allocation or import stub.

## Creep grid

`0x412ac0` receives `ECX=tile_x`, `EDX=tile_y`, stack `[player, packed_width_height, output_entry, vision_mode, ignore_visibility]`. Width and height are signed 16-bit halves. It reads row-major DWORD flags through `0x666454`, with map width at `0x5264b4`.

Only player controller byte `0x6726d0 + player*36 == 2` enables the player-specific masks. For each cell:

```text
cell = 0
if human and flags & (1 << (player + 8)):
    cell = 10
    maximum = 10

bypass = !ignore_visibility && !vision_mode && human && flags & (1 << player)
required = vision_mode ? 0x40000000 : 0x00400000
if !bypass && !(flags & required):
    cell = 5
    maximum = max(maximum, 5)
```

Write each cell and return the maximum maintained during traversal. The cell and return can differ: player mask `10` followed by missing creep writes **5** to that cell while the function still returns **10**. Therefore computing the result only from the finished output bytes changes this branch. The 288 original checks cover players `0/1/7/11`, controller types, vision/bypass switches and nine flag combinations; unused output bytes and surrounding guards stay unchanged.

Upstream creation and maintenance of the two creep flag representations remain outside these calls. Keep the literal masks until that producer contract is recovered.

## Nydus exit grid

`0x412720` takes the same tile coordinates and the first four stack arguments above, without `ignore_visibility`. Its per-cell priority is:

```text
if human and flags & (1 << (player + 8)): code = 10
else if human and vision_mode and flags & (1 << player): code = 9
else if flags & (vision_mode ? 0x40000000 : 0x00400000): code = 0
else: code = 5
```

Return the maximum cell code. Unlike the creep grid, player mask `10` immediately skips later tests for that cell. Unlike ordinary/creep construction, the human player bit can produce **9** in vision mode. The 144 original cases distinguish these priorities across four player IDs and both controllers/modes. Exit creation, pairing and traversal are separate lifecycle contracts.

## Power predicate and the original GRP-derived mask

`0x430000` receives `ECX=requested_ID`, `EDX=pixel_x`, stack `[pixel_y, player]`. Without DAT RequiresPsi it returns true immediately. Otherwise traverse the linked power-source list at `0x665b54`, using next pointer `unit+0xfc`, and consider only sources with a matching owner byte. The source center comes from signed sprite words `+0x14/+0x16`.

The global half-extents are `0x665b58` and `0x665b5c`. Require **strict** `abs(dx) < half_width` and `abs(dy) < half_height`; equality fails. Then test the byte at:

```text
row = trunc((half_height + dy) / 32)
column = trunc((half_width + dx) / 32)
mask[row * (2 * trunc(half_width / 32)) + column]
```

Return true at the first nonzero byte, otherwise continue the source list. This predicate does not independently check Pylon type, completion, power-source list membership or source status; those are responsibilities of list maintenance. Synthetic mask checks independently toggle all 48 cells of an explicit 8 by 6 fixture and exercise strict bounds, owner mismatch, an empty list and the property bypass.

Placement wrapper `0x4130f0` derives the proposed building's pixel center from tile coordinates plus signed half DAT placement dimensions. It returns `0` when the power predicate succeeds and `6` otherwise. The shared validator executes it with real predicates on both paths.

The original power-mask asset chain is:

| Link | Value |
|---|---|
| Sprite ID | `321` |
| `sprites.dat` image | `584` |
| `images.dat` GRP filename reference | `555` |
| Effective `images.tbl` | BrooDat, SHA-256 `5fef82279f56fd51581ae81ecb6207dd0d3e42109821a97f0fceaf42a9224f86` |
| Effective GRP | StarDat `unit\thingy\Juice01.grp` |
| GRP bytes/hash | `1562`, SHA-256 `cd98ce60f5afb8167896cc099f0e9650edf2dc03bcc8c14fda90a667bff3ae08` |
| First frame | Width `252`, height `154`, data offset `14` |

[Asset provenance](evidence/power-asset.json) records all four resources. The native initializer `0x42fe60` obtains frame zero through `0x40f670`, computes half-mask tile dimensions `trunc((width+15)/32)` and `trunc((height+15)/32)`, and allocates four times their product. Original allocation is an explicit unexecuted boundary.

The separately executed fragment begins at `0x42febc`, with a supplied output allocation and the original loaded frame descriptor. It samples opacity through actual `0x40fd50` at `(16 + 32*column, 16 + 32*row)`, mirrors horizontally and vertically, and converts the half-extents to pixels. It produces the complete **16 by 10** byte mask and half-extents **256 by 160**, matching the recorded 160-byte output. Forty-five separate complete sampler cases include all 40 generator samples and frame bounds. This is original post-allocation behavior, not a claim that the imported allocation or entire initialization function ran.

The right-upper quarter, from top toward center, is:

```text
11100000
11111100
11111110
11111111
11111111
```

The [pinned OpenBW mask](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L74) agrees after accounting for its center-to-edge row convention. Treat that agreement as comparison, while the local asset and original fragment establish this profile.

The native GRP opacity sampler handles transparent, repeated and literal runs. After subtracting a run length, it continues only while the remaining x is **greater than zero**; a run boundary belongs to the preceding run in this predicate. It returns whether that run is opaque, without inspecting the actual palette value. Pixel decoding for rendering and this original opacity query require separate contracts.

## Spatial bounds, query stack and traversal

`0x419500` performs a binary search over an eight-byte coordinate-entry array, with global entry count `0x5c99b0`. A nonzero stack flag returns the first coordinate `>= target`; zero returns the first coordinate `> target`. The signed comparisons use doubled coordinates and target bias `-1/+1`. Forty complete calls cover an empty array, duplicates, negative values and equality boundaries within ordinary coordinate range.

Rectangle query `0x422330` uses X entries at `0x5c99b8`, Y entries at `0x5d03f8`, and units at `0x5da250 + unit_index*0x150`; ordinary first unit index is `1`. Each unit contains its left/right/top/bottom entry indexes at `+0x13c/+0x140/+0x144/+0x148`.

It derives the Y index interval `[first_y >= top, first_y >= bottom)`, then walks X entries with coordinates **`left <= x < right`**. A unit qualifies when its bottom Y-entry index is at least the first Y index and its top index is less than the ending Y index. Per-unit generation tags at `0x5d6e60 + index*4` suppress repeated X boundaries, preserving the first encountered order. This is a **border query**: a unit spanning the rectangle with no X boundary inside can be absent. Do not silently describe it as an arbitrary rectangle-overlap query. Subsequent occupancy performs stricter per-cell overlap tests.

Results append to a temporary list buffer at `0x5d88f8`, terminate with null, and advance offset `0x5d6e5c`. Nesting depth `0x5d6e48` and saved offsets at `0x5d6e4c` allow `0x4222b0` to restore the previous offset. Fourteen query/release pairs and a nested pair execute this original stack behavior.

Query generation `0x5da38c` increments with u32 wrap, mapping zero to one. It does not clear the tag array in this function. Three original cases demonstrate that a preexisting tag `1` can suppress a unit when the generation wraps back to `1`. This is an edge of the recovered predicate, not an observed live campaign failure.

## Occupancy predicates and error priority

Builder/tile predicate `0x4122d0` returns **0** only for a nonnull, grounded-status-bit-2 builder, whose ID is not Nydus `134`, whose sprite is not hidden, and whose spatial borders strictly overlap the tile. Border equality at any edge fails. All other cases return **1**. Fifty cases cover status combinations, hidden state, four touching boundaries, overlap, null and Nydus.

`0x413390` scans a null-terminated supplied unit list. With a nonnull builder, a candidate having status bits `0x300` and lacking the builder owner's bit in candidate visibility `+0xe4` is classified as unseen. It skips:

- Status `0x20000` (the mobility bit seeded from units.dat flag `0x08000000`, not a Building bit) when the first boolean argument is zero. Grounded buildings are skipped anyway by the status-bit-`6` test below, so a zero argument ignores mobile units. Corrected after the function atlas ([V01 item 1](atlas/v01.md)); the grid argument formerly named `include_buildings` is renamed `include_mobile`.
- The builder itself, status bits `6`, IDs `105/202`, hidden sprites.
- An unseen candidate when the second boolean argument is zero.
- Nonoverlapping spatial borders, using strict tile bounds.

At the **first eligible overlap**, return `1` for unseen, `4` otherwise. Exhaustion returns `0`. Seventeen original cases include reversed lists showing first-eligible priority: unseen then ordinary returns `1`; ordinary then unseen returns `4`. These flags are predicates over supplied visibility, not a reconstruction of detection/vision generation.

Occupancy grid `0x412360` receives `ECX=builder`, `EDX=tile_x`, stack `[tile_y, player, requested_ID, packed_width_height, output_entry, include_mobile, include_unseen, ignore_visibility]`. It first obtains a real border-query list for the requested DAT collision rectangle. For each footprint cell:

1. Skip an existing nonzero output cell.
2. Unless `ignore_visibility`, skip a human cell with the player's low bit set.
3. If tile bit `0x08000000` is set and builder/tile predicate returns nonzero, produce `4`.
4. Otherwise call the actual per-cell unit predicate above.
5. Write the new code and update the maximum of **newly evaluated** cells.

It releases the query and returns that maximum. Existing nonzero output cells are preserved but do not themselves contribute to the return. The shared validator keeps the earlier grid result separately and combines both results. Eight complete grid cases cover the occupied-bit branch, builder overlap/Nydus exception, a genuine indexed unit, visibility bypass/override and preservation of preexisting errors.

## Builder reachability

`0x48e980(ECX=builder, EDX=pixel_x, stack=[pixel_y])` returns true immediately when builder byte `+0x105` bit `1` is clear. Otherwise it sign-extends the target's low coordinate words and tail-calls `0x481d00`.

The latter resolves the builder sprite position and target point through original region lookup `0x481b10`, then compares their terrain connectivity-group words at `path + 0x449fe + region*64`. Equality returns true. There is no route construction, neighbor scan or distance limit in this helper. Eight complete calls cover flag values `0/1/2/3` and equal/different groups, with three complete shared-validator calls confirming success and code-`7` rejection/fill behavior. Region generation and actual movement paths remain independent requirements.

## Gas-building lookup

`0x413610` takes `ECX=tile_x`, `EDX=tile_y`, stack `[player, vision_mode]`. It always uses **Geyser ID188's DAT placement dimensions**, packed at `0x522a18`, to derive the footprint and proposed pixel center. The requested race's gas-building ID does not parameterize this helper.

For a human controller:

1. If any footprint cell has player high bit `1 << (player+8)`, return `10`.
2. Otherwise, if **every** footprint cell has low bit `1 << player` (`0x413580` returns 0 at the first cell lacking it; an empty rectangle passes), return **0 immediately when vision mode is zero**, even if both live and fog lists are empty. A mixed footprint takes the live lookup below. Corrected after the function atlas ([P04 `0x413580`/`0x413610`](atlas/p04.md)); the gas probes used uniform footprints and did not distinguish partial coverage.
3. In nonzero vision mode on that low-bit path, search the fog-sprite list for sprite ID **275** whose center lies inside the proposed geyser-sized rectangle. Require the **first matching record's exact center** to equal the proposed center; return `0` on equality, `8` otherwise.

If neither human gate selects a path, or the controller is not human, call live lookup `0x4132b0` for Unit ID **188**. It uses actual point-query `0x4221f0`, then scans the returned units and requires the sprite center to truncate into the same 32-pixel tile as the proposed center. Return `0` when found, `8` otherwise. Exact pixel-center equality is not required in this live path.

The point query expands its initial rectangle by the maximum unit dimensions at `0x5d6e40/+4`, obtains the original border list, and filters that list by half-open containment of the queried point. The lookup releases the temporary list on success and failure. These real helpers execute in the 112 gas cases; there are no supplied return values standing in for a geyser.

Fog lookup `0x418000` uses linked records at `0x5c9978`, next `record+4`, unit ID `+8`, sprite pointer `+0xc`. It ignores records whose unit ID is in `{14,85,105,202,205,206,207,208}`, proven by all 228 valid unit-ID calls to `0x4bc730`. Its rectangle test is a wrapping unsigned comparison after adding signed half-width/height. Two additional gas cases place an offset record before an exact one and reverse them: the first in-rectangle record controls the result. The later exact record cannot repair the first mismatch.

Nine shared-validator calls execute the three requested gas IDs `110/149/157`, with a missing live geyser, a real indexed geyser and the human low-bit bypass. All 48 output bytes receive the result. Extractor's RequiresCreep property does not select the creep grid here because gas dispatch precedes the property branches.

These are lookup rules over supplied terrain and fog/live lists. Fog record creation, ownership of the list, exploration updates and gas-building morph/construction orders are separate contracts.

## Resource-depot spacing

Ordinary tile grid `0x412870` has a ResourceDepot DAT-property branch `0x1000`, after the ordinary masks permit a cell. For each footprint cell:

- In nonzero vision mode, check fog-resource neighborhood `0x4180a0` with margin **3 tiles**. A match produces code `2`.
- Otherwise, or if no fog match, query live borders in the pixel rectangle **`[(tile_x-3)*32, (tile_y-3)*32, (tile_x+4)*32, (tile_y+4)*32)`**. Callback `0x413370` accepts a unit solely by DAT ResourceContainer property `0x2000`; the first accepted border-query unit produces `2`.
- No match leaves cell code `0`. Earlier ordinary-mask codes still take precedence.

This is not a circular 224-pixel radius. The visible query is a seven-by-seven-tile rectangle and uses the border-query edge rules above, without an additional distance or strict-overlap predicate. Thirteen original depot cases distinguish live resource/nonresource/empty lists, left/right query boundaries, controller/vision mode and fog resource classes. A one-pixel fixture touching the left border is included; one touching the right border is excluded by the query.

Fog-resource classification accepts sprite IDs `{178,204,263,275}` using Geyser188 dimensions, and sprites `279/280/281` using Mineral176/177/178 dimensions respectively. All other IDs in the tested `177..282` interval reject. The function converts each resource's sprite center to its top-left tile using the signed half placement size and division toward zero, then tests:

```text
unsigned(depot_cell_x - resource_left_tile + 3) < resource_width_tiles + 6
unsigned(depot_cell_y - resource_top_tile + 3) < resource_height_tiles + 6
```

The 106 classification calls and six margin-boundary calls execute this original geometry. The visible branch's callback has no independent owner, life, visibility or order filter; upstream spatial-list membership still determines which units reach it. This batch does not infer broader computer knowledge from that isolated callback.

## Reproduction and remaining dependencies

Use [inspect_power_asset.py](../../tools/research/inspect_power_asset.py) to keep the original GRP in scratch, then [probe_native_placement_117.py](../../tools/research/probe_native_placement_117.py). These calls use the already authorized isolated scope.

```sh
arch -x86_64 /usr/bin/python3 tools/research/inspect_power_asset.py \
  --stormlib /private/tmp/koprulu-pyms-campaign/PyMS/FileFormats/MPQ/StormLib.dylib \
  --app /private/tmp/koprulu-retail.jJ8D4z/extracted/app \
  --grp-out /private/tmp/koprulu-power.grp \
  --out docs/research/evidence/power-asset.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_placement_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --power-grp /private/tmp/koprulu-power.grp \
  --static-out docs/research/evidence/native-placement-static.json \
  --execute-original-code --out docs/research/evidence/native-placement-probes.json
```

Remaining exact dependencies are tile flag generation/updates, spatial index insertion/removal and maximum-dimension maintenance, and fog/Pylon list lifecycle. [AI candidate grids](native-ai-placement.md) subsequently recover filter/scoring consumers, while [region paths](native-region-paths.md) recover connected distance and original search bounds/ties/fallbacks for supplied graph state. Construction, creep growth/disappearance, power loss effects, Nydus lifecycle and builder disruption still require their consuming order state machines and frame ordering. The [completion ledger](completion-ledger.md) retains those obligations.
