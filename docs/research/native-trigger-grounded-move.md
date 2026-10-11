# Native grounded-building Move, StarCraft 1.17

Grounded Move passed **2,171 complete no-stub native cases**, extending
[native-trigger-move.md](native-trigger-move.md). Selected complete Terran and
Protoss paths now include the grounded placement wrapper, tile occupancy
removal/reinsertion, terrain-grid alignment, old-position update guards and
building refresh flag. Positive Zerg creep generation and linked construction
or addon cancellation remain separate consumers.

The target is **StarCraft.exe 1.17.0.1**, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
Addresses are preferred VAs; ImageBase is `0x400000`. Exact instructions,
inputs, source hashes, runtime and outputs are in the
[static witnesses](evidence/native-trigger-grounded-move-static.json),
[native cases](evidence/native-trigger-grounded-move-probes.json) and
[probe](../../tools/research/probe_native_trigger_grounded_move_117.py).
**3,747 executed instruction witnesses** were observed. All selected calls
return through original instructions. No helper stubs,
Windows process, imports, WinMain, game loop or guest file/network operations
execute. Terrain, unit pools, sprite/reference lists and empty consumer guards
are explicit private fixtures; this is not played-campaign evidence.

## Corpus and complete case coverage

Move callback `0x4b7b20` selects grounded helper `0x4b7810` when CUnit status bit 2
is set. Of the 56 stored Move records, **21 records**, **14 distinct field tuples**
and **four physical maps** can select a building through explicit building IDs,
Any 229 or Buildings 231. All 21 are in the 64 supported catalog maps, with no ignored
action bit. This classification is a parameter-level possibility, not a claim
about those missions' dynamic unit statuses.

Each of these 21 records has two connected cases using its authored action fields,
source/destination MRGN records, map dimensions and trigger owner flags: a clear
physical target and a blocked target. Explicit IDs use the stock type; group
selectors use a completed grounded Command Center 106. Units are unlinked Terran
fixtures with null AI, addon, construction and path links. They do not substitute
for other types that an Any/Buildings trigger can select at runtime.

| Family | Passing complete cases |
| --- | ---: |
| Connected grounded Command Center Move | 1 |
| Terran/Protoss stock profiles, alignment, completion and rejection | 280 |
| Full placement wrapper: all 228 stock DAT rows × eight tile-flag profiles | 1,824 |
| Bounds and original memset | 20 |
| Depot/live Vespene Geyser neighborhood | 4 |
| All 21 authored grounded-eligible records, clear and blocked | 42 |
| **Total** | **2,171** |

Every DAT-row case executes the full placement wrapper and real callees. Calling
that wrapper for all 228 IDs does not assert that the Move callback admits every
ID as a grounded unit. The stock input is effective units.dat SHA-256
`da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba`.

## The grounded wrapper differs from construction

`0x4b7810` rejects incomplete grounded units before placement. It clamps the
requested coordinate against DAT extents and full map dimensions, subtracts
half the stock placement box, aligns down to a 32-pixel grid, then adds the half
box back. Underflow after subtracting the half box is compared unsigned with
the map dimension and replaced by `dimension-1` before alignment. Requesting a
position near the top/left edge can therefore yield an out-of-bounds snapped
footprint and reject, rather than clamping to the nearest valid building grid.

Its validator is **`0x412e20`**, distinct from construction's shared
`0x412f10` in [native-placement.md](native-placement.md). ABI:
`ECX=owner`, `EDX=tile_x`, stack `[tile_y, unit_ID, output_entry]`.
It computes signed DAT placement dimensions divided by 32, checks footprint
bounds, calls **physical grid `0x412560`**, then **occupancy `0x412360`**, and
returns the signed maximum of both results. Out-of-bounds returns 3 and fills
all 48 output bytes with 3 using original `0x4d0c60`.

This wrapper has no construction-specific gas, RequiresPsi, RequiresCreep,
Nydus or builder-reachability dispatch. Connected stock Gateway 160, Forge 166
and Shield Battery 171 cases move with the power-source list empty. No power
predicate executes in this selected Move path. Their later refresh flag is
separate from immediate construction validation. A remake that sends grounded
Move through normal construction validation would add a condition absent here.

## Physical grid and occupied-bit priority

For each stock footprint cell, `0x412560` uses mask **`0x28800000`** when
Staredit group bit 1 is set, otherwise **`0x28c00000`**. A matching tile flag
produces code 4. IDs 194 through 199 have an exception when the full tile flags
are signed-negative. The eight uniform tile fixtures include a negative
`0x80800000` profile to discriminate this branch across all 228 DAT rows.

A ResourceDepot property `0x1000` adds the real live-resource border query
when the physical mask permits the cell. Its callback `0x413370` tests DAT
ResourceContainer property `0x2000`, without an owner filter. Connected
Command Center 106 and Nexus 154 cases use a real indexed **Vespene Geyser 188**
with owner 11 near or far from the target. The near resource produces code 2 and
rejects the movement attempt. Type 188 is a geyser; Mineral Field IDs are 176–178.
The spacing query is the per-cell seven-by-seven-tile border rectangle covered
in [native-placement.md](native-placement.md), not a circular distance test.

The wrapper preserves the physical-grid result, then calls occupancy with
`builder=null`, `include_mobile=!(properties&2)` (formerly named `include_buildings`; the flag admits units with the status-`0x20000` mobility bit, corrected after the function atlas, [V01 item 1](atlas/v01.md)), `include_unseen=1` and
`ignore_visibility=0`. Existing nonzero physical errors are preserved.
Occupancy can produce code 4 from tile bit `0x08000000` even after the physical
ID 194–199 signed-flag exception accepted a cell. The stock-row matrix proves
this combination, rather than treating the first grid's result as final.

## Accepted relocation and stage-specific bounds

After successful `0x412e20`, the helper calls original center-bound predicate
`0x424d30`, which checks the full map dimensions. For completed Terran grounded
units it checks carried construction, the constructing-building predicate and
an attached addon parent; selected null links take the original guards.

It then calls `0x430390(unit,0)`, `0x43a270` and `0x430430`. The ordinary Move
report covers reference clearing and sprite/spatial effects. Grounded units
add old occupancy removal **`0x41a190` → `0x4134a0`** and new occupancy addition
**`0x419fd0` → `0x412c40`**. The old footprint loses bit `0x08000000`; the final
footprint gains that bit. Original border removal, memmove, neighboring indexes,
sprite row lists and reinsertion execute and are checked together.

**Validation and relocation use different Y bounds.** Relocation's
`0x43a270` calls `0x41a4b0`, whose upper Y bound reserves 32 bottom pixels.
At `0x41a527` it subtracts `0x20` from the comparison limit; the clamped value
at `0x41a531` subtracts `0x21`. For a 128×128 map and stock Command Center,
request 4095,4095 is snapped and validated as **4032,4048** but actually moves
to **4032,4022**. Final Y is `4096 - down_extent 41 - 33`.
This final position is not revalidated or snapped again. Occupancy is written
using that final position. The tests check each stage rather than imposing one
shared bounds rule.

Grounded Move does not call ordinary movement's `0x42a5a0` order resumption.
The selected initial order 3 remains 3. The admitted attempt always consumes one
shared quantity item through callback `0x4b7c41`, including incomplete, blocked,
resource-neighborhood and bounds rejection. The complete action returns 1 and
invalidates trigger count caches. Live/completed ownership counts are unchanged.

## Post-move consumers and explicit guards

Terran post-relocation `0x437180` calls original adjacent-addon search
`0x431ae0` and its real callback `0x4317d0`. The selected fixtures contain no
eligible attachable addon. `0x4b46e0` derives the old building bounds through
`0x4c04f0`, tests original map-tile group predicate `0x4c07e0`, and resets
`0x6c46d8` when its selected traversal completes. Private old-map group values
are zero, so positive creep-candidate allocation does not execute.

The selected Terran/Protoss types do not enter the positive Zerg-creep branches
of type predicate `0x4c0790` or RequiresCreep property handling. They reach
`0x42ffc0`, which writes **`0x665b60=1`**. The report proves that dirty marker;
it does not claim that the later power/status pass ran.

Grounded visibility can reach original fog-unit creator `0x417b10`. Its free
record list is explicitly empty, so the original guard returns without sprite
allocation. Empty linked image lists, null child/AI/path links and no active
construction/addon records are fixtures, not supplied helper returns.

Remaining connected dependencies are positive linked construction/addon
cancellation and attachment, populated fog-record creation, positive old/new
creep production, nonnull AI reaction, subunits and later power/status refresh.
The separate [image initialization](native-image-initialization.md),
[unit creation](native-unit-creation.md) and [worker](native-workers.md) reports
cover parts of those consumers; selected complete Move paths do not imply their
composition for every state.

## Reproduction and provenance

Use the pinned x86_64 runtime and private inputs in
[reproduction.md](reproduction.md). The probe takes `--exe`, `--data`,
`--manifest`, `--pyms`, `--static-out`, `--out` and the explicit
`--execute-original-code` flag. It pins the previously published Move,
ownership and placement source hashes, plus creation dependencies; all
executable, DAT, map and script hashes are recorded in output. The published probe SHA-256 is
`a82eb2e8b728f0268ce897022fac18b4a8ef0576052664f957501afbfbeac390`.
Native x86 runs
in Unicorn private memory; retail data stays in scratch outside the project.
