# Native 1.17 terrain classes and region graph generation

This report recovers the original transformation from VF4 mini-cell flags to walk flags, terrain classes, region rectangles, ordered adjacency, connectivity groups and split-cell lookup. It then executes the original long-path search on graphs produced by those routines. Target: **StarCraft.exe 1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`.

The [static witnesses](evidence/native-region-generation-static.json) contain **32 reviewed code blocks and 3,939 instructions**, with embedded jump tables recorded separately and excluded from the executable allowlist. The [execution evidence](evidence/native-region-generation-probes.json) contains **452 passing cases**, comprising **507 complete original-function returns and 110 explicitly labeled fragment completions**. A case can compose several invocations; those counts are different measures. The [probe](../../tools/research/probe_native_region_generation_117.py) supplies private buffers and synthetic terrain. All executed callees are original instructions. No allocator substitute, imported function, Windows API, game loop, WinMain or guest host-file/network operation executes.

The graph builder returns through its complete original ABI. VF4 conversion, megatile flags, walk-grid copying/finalization and terrain-class initialization are allocation-dependent routines: their specific post-allocation or post-free fragments are declared below. Returning from a suffix does not establish the enclosing allocation wrapper. Nineteen terrain cases record complete generated graphs with structural assertions; their exact region numbering and paths are original observations, rather than claims that an independently implemented full builder has matched every byte.

This extends [native long paths](native-region-paths.md), [region lookup](native-workers.md), [placement predicates](native-placement.md) and [ground footprint/terrain-edge consumers](native-unit-creation.md). Authentic CV5/MTXM import, generated collision contours and campaign-map geometry remain separate dependencies.

## Native representation and outer call sequence

Global `0x69b584` points to a `0x97a20`-byte region object. The complete outer initializer `0x490d80` statically performs:

```text
Storm allocation and zeroing of region object
 -> 490e90: walk-grid initialization and terrain-class raster
 -> 490f50: reserved bottom region
 -> 48f060: complete region graph builder
 -> 48af10: collision contour construction
 -> 48f0e0: route-cut ranks
```

Only the imported allocation/free and contour portions are outside this batch's execution. The complete graph builder and rank producer execute separately with the object/input buffers prepared in private memory. The wrapper's boolean failure and imported error handling are not silently replaced by a successful allocation.

| Object offset | Storage | Contract |
|---|---|---|
| `+0` | u16 | Region count |
| `+4` | pointer | Cursor into the large-neighbor WORD pool |
| `+8` | pointer | End cursor of six-byte split records |
| `+0xc` | `256*256` u16 | Tile-to-region raster, fixed 256-tile row stride |
| `+0x2000c` | 25,000 six-byte records | Split `[mask,region_false,region_true]` storage |
| `+0x449fc` | 5,000 records, 64 bytes each | Regions |
| `+0x92bfc` | 10,000 u16 | Large-neighbor storage |
| `+0x97a1c` | pointer | Collision-contour context, generated separately |

Region fields established here are literal class u16 `+0`, group u16 `+2`, tile count u16 `+4`, ground-neighbor prefix count u8 `+6`, total-neighbor count u8 `+7`, temporary path pointer `+8`, neighbor-array pointer `+0xc`, fixed8 center dwords `+0x10/+0x14`, pixel bounds `[left,top,right,bottom]` as four signed16 values `+0x18`, cut rank u8 `+0x20`, and zero-ID-neighbor count u8 `+0x21`. Up to ten neighbor words can reside inline at `+0x2c`.

Pinned [Teippi pathing structures](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/pathing.h#L97) corroborate the layout. This batch's widths, offsets and writes come from the local 1.17 instructions; modified source names do not establish additional field semantics.

## VF4 conversion and megatile flags

VF4 input has 16 little-endian WORDs per megatile, in row-major order for its 4×4 mini-cells. The loader-facing globals used by these routines are record count WORD `0x666448` and VF4 pointer `0x66644c`. Map megatile IDs are WORDs through `0x54323c`; the walk copier and split producer mask each ID with `0x7fff`.

`0x48ad80` allocates 16 bytes per VF4 record. Its measured fragment starts at **`0x48ad9f` after allocation**, with a supplied private output pointer in EAX and the actual saved-register/return stack shape. Each input low bit1 produces byte0 when set or byte`0x80` when clear. Other VF4 bits do not enter these walk bytes. The original packing instructions and return execute; the allocator does not. All 16 positions, single-bit masks, complemented masks and unrelated high bits are covered.

Megatile-flag routine `0x445540` allocates four bytes per record. The fragment starts at **`0x445562` after allocation**. It writes output pointer global `0x666450` and computes:

| VF4 count/test over 16 WORDs | Output flag |
|---|---|
| Walkable low bit1 count **>12** | `0x10000` |
| Walkable count <=12 | `0x40000` |
| Walkable count1..15 | Also `0x20000000`, partial mini-cell walkability |
| Bit4 count >=12 | `0x04000000`, high |
| Otherwise bit2 count + bit4 count >=12 | `0x02000000`, middle |
| Any bit8 | `0x01000000`, very high |

Middle plus high is the **sum of two counts**, so a WORD carrying both bits contributes twice to that sum. High has priority over middle. A 108-record boundary matrix covers walk counts0/1/12/13/15/16, middle and high counts0/11/12, very-high counts0/1, overlapping bits and ignored high bits. An empty-count fragment takes the original no-loop return. Independent expectations compare every output dword.

These are numerical native contracts. CV5 overlay flags and the actual map-to-megatile producer are not assumed from them. The nineteen generated-graph fixtures use these **original-generated megatile flags**, with no CV5 overlay, and supplied map ID arrays; their flag/raster stages are therefore connected without claiming complete map import.

## Walk raster, map UI exclusion and direction bits

Walk-grid allocation at `0x48b1c0` requests `0x100000` bytes: 1024×1024 mini-cells. The measured copy fragment begins at **`0x48b20c`** after both allocation operations, and stops **before** the free operation at `0x48b2a4`. A separate fragment begins at **`0x48b2b8` after free** and returns through the original epilogue. No execution is resumed through a fake imported return.

The copier expands each map megatile's four rows of four bytes into a **1024-byte walk-row stride**, while map tile IDs use the actual map width. The input's high ID bit is ignored. The finalizer then overwrites covered UI cells with byte`0x80`:

- In the last eight walk rows, the left twenty and right twenty mini-cells are blocked.
- In the last four walk rows, the entire map walk width is blocked.

For each remaining nonnegative walk byte, it adds boundary/adjacent-blocked flags: top1, right2, bottom4, left8. A cell already negative is not given direction bits. Four mixed-ID maps of sizes8×8,12×9,16×12 and24×10 compare **every valid walk byte** against an independent four-neighbor definition. Supplied records contain mixed obstacles and ignored bits. The tests preserve the fixed physical stride; they do not accidentally treat unused columns as map cells.

The native body has no safe small-width normalization before its twenty-cell UI writes. This report's tested dimensions are valid and at least eight tiles wide; behavior on malformed dimensions is not generalized from the valid-map probes.

## Terrain-class raster and the reserved bottom region

`0x490e90` first calls the walk initializer, then classifies map flags through `0x666454`. Its measured fragment starts at **`0x490e9f` after that call**, with the original frame and ESI pointing at the input dwords. The output is a WORD raster with 256-tile physical stride.

```text
if (flags & 0x410000) == 0: class = 0x1ffd
else:
    v = flags & 0x06010000
    if v == 0:          class = 0x1ffd
    elif v == 0x02010000: class = 0x1ff9
    elif v == 0x04010000: class = 0x1ffa
    else:               class = 0x1ffb
```

The isolated class matrix tests combined/absent bits, partial flags, unrelated high bits and all-one input. For ordinary generated VF4 flags these represent unwalkable, middle walkable, high walkable and other walkable terrain respectively. Literal `0x1ffd` is the actual marker consumed by path/AI predicates. The very-high bit does not independently produce another region class here.

Complete `0x490f50(ECX=region_object)` creates reserved region0 before general generation. It marks the final **tile row** as region0, with flags`0x1ffd`, group`0x4000`, tile count equal to width and bounds `[0,(height-1)*32,width*32,height*32]`. Its fixed8 center is:

```text
x = (width*16 + 16)*256
y = (height*32 - 16)*256
```

The additional **16 pixels in x** is an actual original write; do not replace it with the midpoint of the bounds. Four complete calls cover dimensions8×8 through256×256. The reserved group prevents ordinary small-region merging and group relabeling.

## Complete rectangle graph builder

`0x48f060(ECX=region_object, EDX=Rect16)` runs the following original callees and returns1 on the covered valid inputs:

1. `0x48f8e0`: create initial rectangles inside the supplied tile rectangle.
2. `0x48f870`: refresh counts/bounds, ordered neighbors, centers and connectivity groups.
3. `0x48fc80`: merge small regions.
4. `0x48feb0`: compact/reindex live regions and raster IDs.
5. Refresh again.
6. `0x48f1b0`: replace partial cells with split records.
7. Original CRT memset clears the unused tail of the 5,000-record array.

The fixture selects the real scalar/REP-dword memset branch using CPU-feature globals, without changing code or substituting a helper. The function-entry/size allowlist and 5,000,000-instruction bound apply to nested calls as well. Every complete invocation checks the original return address and stack cleanup.

Initial creation scans row-major with wrapping inside its rectangle for unassigned class WORDs >=5000. `0x490b80` finds a homogeneous area, initially1×1, with maximum x at min(start+8,map_width) and y at min(start+7,map_height). It grows x/y according to matching new strips and the corner, stopping when either limit is reached or growth fails, then caps either aspect dimension at three times the other.

Consequently a uniform area beginning at0,0 yields **7×7**, despite the x limit8. A start in the last map column or row can yield **1×1**, because that initial extent already reaches a limit. Six complete direct rectangle calls fix these boundary examples. The limits are not a promise of every rectangle being8×7 or an instruction to enlarge a thin edge rectangle after stopping.

If the current candidate area is smaller than the previous selected size, the initializer can look ahead through up to **25 additional candidate areas**. Strictly larger area replaces the current best; equality does not. Initial previous size is56. The selected rectangle is filled with its new region ID; its center initially stores integer tile coordinates.

Expansion `0x48fff0` takes the rectangle enlarged by at most one cell on each side, then makes two **in-place row-major passes**. A remaining matching class cell joins only when one cardinal adjacent cell already belongs to this region. Expansion order can propagate newly changed IDs within a pass. On the first selected area<=6, the initializer expands all regions created in this call once more. The checkerboard/island cases exercise this branch; they record exact output IDs rather than replacing it with flood-fill components.

The initial-region limit is5000. Its overflow branch writes global`0x69b580=1`, frees the walk buffer if nonnull and reaches native error handling. Those imported/UI branches are static evidence; valid probes do not execute them or replace them with an artificial failure result.

## Counts, adjacency and connectivity groups

`0x490330` resets counts and bounds, scans the raster and counts ordinary IDs<5000. Bounds start `[32767,32767,-1,-1]`; every matching tile expands them to exclusive-right/bottom pixel bounds. A zero-count record's class becomes`0x1fff`. Fifteen complete randomized raster cases compare all counts/bounds and verify that class markers>=5000 are ignored. Counts are WORDs; artificial 65,536-cell aggregation is not established as a supported map profile here.

`0x490a60` obtains neighboring raster words in exact order **top,left,right,bottom,top-left,top-right,bottom-left,bottom-right**, using`0x1fff` for outside-map positions. Nine complete interior/edge/corner cases preserve that order.

`0x490500` enumerates each owned tile inside a region's bounds in row-major order, then those eight neighbors. It excludes outside/self IDs and suppresses duplicates while preserving the first accepted encounter. Cardinal neighbors are accepted without the diagonal walk test. A diagonal between ordinary walkable regions needs two clear endpoint 2×2 mini-cell blocks and a clear intervening 2×2 block on at least one of the two routes around the corner. A literal`0x1ffd` endpoint bypasses that ordinary diagonal test. The generated wall/cross/checkerboard graphs exercise the actual code; exhaustive diagonal truth-table equivalence remains a distinct expansion of the evidence.

Up to ten collected IDs are copied into inline storage, with unused words filled`0x1fff`. More than ten use the shared WORD pool cursor. The total count is stored as a byte. `0x4910c0` then performs a **stable partition**: nonzero ordinary neighbors first, nonzero`0x1ffd` neighbors next, and ID0 last. It writes the first partition's count at`+6`. It writes`+0x21` only when a zero ID was present; absent zeros, its previous byte survives. The normal enumerator initializes that byte to0 before partitioning. Forty direct cases compare ordering, duplicates, empty input, both counts and preservation of a deliberately stale byte.

`0x4901f0` resets group IDs below`0x4000` to0, preserving reserved values. It visits region IDs in increasing order. A zero-group, nonempty root starts the next group number, beginning1, then actual recursive `0x490e20` walks only still-zero-group neighbors. A `0x1ffd` current record uses the **non-ground suffix**; another record uses the **ground prefix**. The recursive visitor does not additionally require a positive neighbor tile count or a matching neighbor class. Fifty complete directed-graph fixtures cover those precise guards, reserved groups, empty roots, empty reachable nodes and prefix/suffix selection. Those unusual directed inputs isolate helper behavior; they are not assertions that authentic terrain adjacency is directed.

Refresh converts newly stored tile centers smaller than256 to fixed8 pixel centers `(tile_center*32+16)*256`. Centers are retained from rectangle creation/expansion history; they are not recomputed as the average of every tile or the eventual bounds.

## Merging and reindexing

`0x48fc80` starts with threshold6 and scans regions in **descending ID order**. A source must be nonempty, have group<`0x4000`, and have tile count strictly below the threshold. Among stored neighbors it chooses the strictly smallest positive tile count with the **same literal class** and nonreserved group. Equal counts retain the first neighbor. It rewrites source IDs inside source bounds, adds WORD tile counts to the target, expands target bounds and clears source count/class to0/`0x1fff`. Centers and neighbor arrays are not refreshed during that scan.

After the pass it counts nonempty regions. If that count is>=2500, it repeats with threshold increased by2; otherwise it stops. The threshold and bound follow the original loop. Twenty-four complete count/bounds/merge sequences distinguish source counts1/5/6, reversed equal-count neighbors and source/target reserved groups. They do not exercise the >=2500 repeated-pass branch.

`0x48feb0` builds a 5,000-WORD prefix-count mapping on the stack, remaps neighbor IDs, copies each live 64-byte record forward, changes the region count and remaps raster WORDs. Twenty-four complete randomized live-mask cases compare retained payloads, groups, ranks, centers, neighbors and the entire8×8 raster. The helper does not remove stale adjacency references to dead regions; the enclosing builder immediately refreshes adjacency afterward. Standalone compaction is not a substitute for that second refresh.

## Partial cells, lookup and route-cut ranks

For each map tile with flag`0x20000000`, complete `0x48f1b0` builds a16-bit blocked mask from VF4 low bits: bit`mini_y*4+mini_x` is1 when unwalkable. It considers current region plus cardinal/diagonal neighboring regions. A walkable current region selects a blocked side using edge/corner blocked counts; an unwalkable current region selects a walkable side using cardinal edge walkable counts. Scanning is descending direction index, with strictly better counts replacing the best, so ties retain the later direction in the forward neighbor order. A previously split neighboring cell is resolved to its appropriate true/false region with additional adjacent walk tests.

If no distinct suitable second region is found, the mask becomes0 for a walkable current region or`0xffff` for an unwalkable one. Two blocked sides also force`0xffff`. The stored record is `[mask,walkable_side,blocked_side]` when such sides exist. The raster receives `0x2000 + split_index`, and the split end pointer advances six bytes. This happens **after** counts, groups and adjacency were refreshed; split replacement does not rerun those producers.

Ten generated partial-wall profiles cover single/corner/row/column/checkerboard and complemented masks, including walkable-count threshold changes. Their recorded masks equal the VF4 complement and their selected side IDs vary with the native direction/tie rules. **160 complete original pixel-lookup calls** then visit every 8×8 mini-cell center in those generated split cells. Actual `0x481b10` returns the false region for mask0 or true region for mask1. The branch is established through generated records and the real lookup, rather than only an authored record fixture.

Complete rank producer `0x48f0e0` examines the non-ground neighbor suffix of each region. It counts distinct group WORDs of neighbors with tile count>=4, writes0 for fewer than two groups, and caps the rank at5. Ground-prefix neighbors do not contribute. Seventy-two complete cases distinguish group counts0..8, tile counts0/3/4/5 and an excluded first ground neighbor. [Single-cut selection](native-region-paths.md) separately consumes this rank and selects the last equal-ranked eligible path node.

## Connected generated graphs and evidence limits

Nineteen full builder cases cover flat maps, vertical/horizontal walls, a cross, checkerboard, elevation stripes, islands and the ten partial-wall profiles. JSON preserves input VF4 records/IDs/flags, all region records, groups, bounds, ordered neighbors, split records and raster. Assertions verify reserved bottom IDs, ordinary class preservation, valid/deduplicated neighbors, prefix classification and the relation between pre-split counts and remaining direct raster cells.

On flat8×8, the original produces two records: bottom region0 and walkable region1 with56 tiles. Its walkable center is `(112,112)` pixels; the bottom center is `(144,240)`. A vertical16×12 wall separates the walkable sides into groups1 and2, while the wall is group3 and the bottom remains`0x4000`. These are measured fixture results, not imported campaign geometry.

**22 complete long-path calls** use those generated same-group graphs and endpoints taken from actual raster cells. They reach the requested region, preserve legal ordered adjacency and clear every temporary region backreference. The exact routes are recorded. Their success does not erase the earlier synthetic-graph heap exhaustion or heuristic counterexamples in [native-region-paths.md](native-region-paths.md).

The remaining terrain closure work is original CV5/MTXM/megatile import and overlay flags, collision-contour construction and its directional arrays, dynamic terrain/doodad changes, exhaustive diagonal/split-neighbor alternatives, large graph limits and authentic campaign-map verification. Local paths, repathing, collision response and movement FSMs remain additional algorithms. No complete game or campaign playthrough is claimed.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_region_generation_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --static-out docs/research/evidence/native-region-generation-static.json \
  --execute-original-code \
  --out docs/research/evidence/native-region-generation-probes.json
```

Default mode hashes and disassembles without importing Unicorn or executing original code. Unknown executable hashes stop before analysis/execution. The report records its probe source hash, Python3.9.6/x86_64, Unicorn2.1.4, pefile2024.8.26 and Capstone5.0.6. Original binaries and retail assets remain outside the project. Fragment frames, registers, stop addresses and complete-function ABIs are explicit in the runner and execution records.

Pinned [OpenBW region reconstruction](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L20044) supplied hypotheses for rectangles, groups and split selection. The native center arithmetic, data widths, transient state and measured paths retain local 1.17 authority; source agreement alone does not close unexecuted branches.
