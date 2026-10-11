# Native Move Unit action, StarCraft 1.17

The original Move Unit action passed **475 complete no-stub native cases**, with
**3,961 executed instruction witnesses**. It executes through spatial removal, relocation,
placement, vision refresh, reinsertion and order resumption. A blocked ground
placement restores the old position but still consumes one item of the action's
quantity budget. This extends the rejected Move inventory in
[native-trigger-unit-actions.md](native-trigger-unit-actions.md) and uses the
recovered placement, spatial insertion and vision dependencies in
[native-unit-creation.md](native-unit-creation.md).

The target is **StarCraft.exe 1.17.0.1**, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
Addresses are preferred VAs with ImageBase `0x400000`; subtract that base for
RVAs. The [static witnesses](evidence/native-trigger-move-static.json),
[native cases](evidence/native-trigger-move-probes.json) and
[probe](../../tools/research/probe_native_trigger_move_117.py) preserve exact
instructions, hashes, dependencies, native call paths and fixture inputs.

This is isolated original x86 execution in private Unicorn memory. Each
selected function and callee returns through original instructions. No helper
stubs, imported functions, Windows APIs, WinMain, game loop or guest file/network
operations execute. The fixtures do not constitute played campaign or full
engine compatibility evidence.

## Campaign parameter coverage

The retail corpus contains **56 stored Move action records**, **44 distinct
parameter tuples** and **16 physical maps with this action**. All 56 occur in
the 64 supported catalog maps; the four version-57 extras contain none. No
stored Move record has the ignored-action bit set. Catalog coverage is not a
campaign play order.

Each stored record has a connected probe with its exact action fields, authored
source and destination MRGN records, map dimensions, trigger owner flags and
map hash. A single matching unit is a private fixture. Explicit IDs use that
stock unit type; Any/Men selectors use Civilian type 15 and Buildings uses a
floating Terran Starport type 114. The explicit Terran building IDs106,111,113,
114 and122 also use floating status 5, which selects the ordinary-unit path.
These cases cover the stored parameters on an admitted state; they do not prove
that the mission's units are floating when the trigger executes. Terrain is flat
and walkable, unit AI and child links are null, and the linked image list is
empty. The selected grounded-building extension is published in
[native-trigger-grounded-move.md](native-trigger-grounded-move.md).

Map dimensions range from 96 to 192 tiles wide and 96 to 128 high in this action
subset. The original native vision template generators `0x466990` and `0x466bd0`
prepare the sight range and exact map stride consumed by the original reveal
functions. They are real callees, not synthetic replacement masks.

## Dispatch, selection and quantity

Action 39 handler `0x4b5840` requires a nonzero **low source-location byte** and a
nonzero **full destination-location DWORD**. It subtracts one from the source
byte, reads the MRGN corners as WORDs, takes the source elevation flags from
MRGN `+0x12`, and stores destination index `(low_byte(destination)-1)` as a BYTE.
The destination's elevation flags do not filter selected units.

Quantity BYTE `+0x1b` initializes global `0x6c4c0c`; zero becomes **999999**.
The original `0x443110` visitor collects spatial candidates before callbacks.
Callback `0x4b7b20` checks native elevation exclusion `0x4b95f0`, Death,
player selector `0x4ba260`, excluded unit classes and unit type/group. LiftOff
and Land orders 71/72 on building types, Nuclear Missile 14, Dark Swarm 202,
mineral/geyser types (`0x4243e0`), the units.dat resource-container flag 0x2000
and trap/door types 203–213 (`0x425ca0`) are rejected before movement.
Corrected after the function atlas ([P40 `0x4b7b20`](atlas/p40.md)): 0x2000 is
the resource-container flag, not a powerup property, and IDs 203–213 are the
floor/wall traps and doors, not Flag types (Flag is 215).

The destination is the arithmetic average of its DWORD corners. The callback
then selects `0x4b7a80` when status bit 2 is clear or `0x4b7810` when it is set.
**Both branches unconditionally decrement the shared quantity budget** at
`0x4b7c27` or `0x4b7c41`, regardless of the helper's result. The callback returns
true when this decrement produces zero. Quantity is shared across players and
is charged for an admitted movement attempt, including rollback. The action
returns 1 and calls cache invalidator `0x4b9820` after the visitor.

The unchanged live/completed source arrays are checked separately from cache
invalidation. Spatial membership changes immediately; Move does not change
ownership or live/completed unit counts.

## Ordinary-unit movement and rollback

`0x4b7a80` saves the old sprite position and requested destination as WORDs.
It calls `0x430390(unit,0)`, relocates with `0x43a270`, then asks original
placement helper `0x47dc30` for a position. The three optional placement
arguments are zero. A ground target whose footprint fails walkability returns
error 60; the Move helper relocates to the old position and calls `0x430430`.
No successful-movement order resumption follows this rollback.

On successful placement, the helper relocates to the returned position and
calls `0x430430`. If status Completed bit 1 is set, `0x42a5a0` resumes the existing
queued order or allocates and activates the stock units.dat ReturnToIdle order.
Incomplete ordinary units move without that order resumption. These selected
paths include ground units, flyers and floating Terran buildings. The stock
units.dat input SHA-256 is
`da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba`.

`0x43a270` clamps coordinates to unit extents and map bounds, updates integer
and fixed-point Flingy positions, destination and waypoint, moves the sprite,
updates existing spatial borders when present and marks overlaps. The tests
check CUnit integer/fixed-point coordinates against sprite position, sorted X/Y
border tables, each CUnit's border indexes, neighboring entries and sprite row
head/tail/previous/next links. Nonempty candidate fixtures force the original
ring search and index maintenance; no host collision function replaces them.

## Detachment and reinsertion

`0x430390` returns early for an already-hidden sprite. Its mode 0 path used by
Move clears unit and bullet references, calls occupancy removal `0x41a210`,
marks existing sprite image rectangles dirty and initializes movement.
It **does not set the sprite hidden bit or transfer the active unit to the
hidden unit list**. The batch starts with properly linked visible units.

`0x419460` removes the high X/Y borders, then the low X/Y borders, decreasing
spatial entry count twice. Actual `0x419d00` and `0x419d90` decrement the affected
neighbor border indexes and call original `memmove 0x4d2360`. Removed unit
indexes become `0xffffffff`.

`0x430430` calls original unit vision `0x423260`, sprite visibility `0x438c20`,
spatial activation/insertion `0x41a050`/`0x4195a0` and movement initialization
`0x42c7e0`. The selected mode 0 path retains active-list membership throughout.
The original sprite mover `0x417330` updates sprite row lists when Y crosses a
tile row. Vision reveals the destination using the native owner/shared-vision
mask; it does not revoke previously accumulated exploration at the old position.
The initialized old location in these probes was initially unrevealed, so its
flags stay unchanged.

## Reference, queue and density effects

Connected private active-unit lists include a Civilian mover, an SCV watcher
and a floating Starport with rally reference to the mover. Actual `0x4269b0`
clears matching fields `+0xec`, `+0x5c`, `+0x14`, `+0x7c`, `+0x80` and `+0x11c`.
Its worker branch clears resource pointer `+0xc8`, carried-unit pointer `+0xd0`
and carrying byte `+0xce`. For a rally-capable producer, it clears `+0xfc` and
copies the mover's **old** sprite coordinates to rally `+0xf8/+0xfa`.
A queued order targeting the mover is removed through the real `0x429d20`,
list removal and order-pool release. Actual `0x40a5a0` clears matching bullet
`+0x5c/+0x64` references and writes old mover coordinates into bullet `+0x58/+0x5a`.
Unrelated bullet references remain. These effects also occur before a movement
attempt that later rolls back.

An existing queued Move order on the mover is allocated by the original
`0x4295d0`. Successful movement resumes it through `0x42a5a0`/`0x42a610` and
releases its order node; failed placement retains the queue. Final user-action
byte remains 0x82 on the successful unlocked fixture: the original activator
clears the bit set before activation. The instruction that sets this bit is
not an unconditional final-state postcondition.

Flyer status 0x20005 exercises density removal `0x481830` and addition
`0x481760`. Old density decrements when nonzero; destination density increments
unless already 255. CUnit density-cell bytes `+0x14e/+0x14f` become
`floor(sprite_x/48)` and `floor(sprite_y/48)`, with native row stride 0xab.
With byte `+0x14c`'s high nibble already nonzero, the native RNG guard skips a
draw; with it zero, one original source 37 draw executes. Main RNG state
`0x67393c`, total counter `0x673938` and source 37 counter
`0x673940 + 37*4` are checked. The shared main LCG is
`state = state*0x015a4e35 + 1` modulo 2^32. No auxiliary-state restoration is
claimed for Move. Zero and 255 density inputs are width/saturation fixtures,
not statements about authored campaign occupancy.

## Redraw and vision guards

Additional Move cases attach two private images with clipped screen rectangles
and animation 0. Actual `0x415f30` calls `0x43eb50` and original memset
`0x4d0c60`; the dirty-cell matrix at `0x6d06d0`, native screen redraw flags and
both image dirty bits are checked. Zero or signed-negative dimensions suppress
that image's rectangle; the full-redraw flag suppresses rectangle marking.
Relocation still marks both linked images dirty. The pixel rectangles are
fixture inputs, and the idle-animation 0 guard avoids IScript transition.
This closes selected redraw invalidation, not sprite frame rendering or full
image/IScript lifecycle.

Shared-vision cases cover each of eight owners, ground and air reveal consumers,
and one- or two-player masks. A native generated sight shape reveals the target
tile's visibility and exploration bits. It leaves the distant initial source
tile unchanged. The grounded collision fixture forces real ring search; for
Civilian's stock 23-pixel box around target 400,400, the original first accepted
candidate is **400,424**. At `0x47e424`, a 48-pixel ring width exceeds twice 23 and
advances the first bottom-edge X from 376 to 400. This ordering is observable;
a host search using a different perimeter order could choose another position.

## Evidence boundary and remaining dependencies

The connected cases separate stock DAT and authored action/location/dimension
parameters from private unit, order pool, sprite, fog, terrain, density and
reference graphs. Complete selected call chains are a function contract; they
are not complete state coverage.

Grounded buildings use `0x4b7810`, including `0x412e20` building placement,
construction/addon cancellation, old-position terrain updates and
creep/building-status consumers. Those paths are outside these ordinary/floating cases;
[native-trigger-grounded-move.md](native-trigger-grounded-move.md) closes
selected complete Terran/Protoss grounded paths and preserves the remaining
creep/construction/addon consumer boundaries. Nonnull AI destination reaction `0x437540`, linked
subunits, transport/hangar contents, nonempty movement paths and complete image
animation/render-frame behavior also remain dependencies. Separate reports cover
[primary order issuance](native-primary-orders.md),
[unit creation and Show](native-unit-creation.md),
[unit/location counts](native-trigger-unit-counts.md) and
[ownership accounting](native-trigger-ownership.md).

## Native case inventory

| Family | Passing complete cases |
| --- | ---: |
| Initial connected Civilian Move | 1 |
| Stock ground/flyer/floating-building profiles | 108 |
| Unwalkable target rollback and budget | 48 |
| All 64 source-elevation masks, ground and air | 128 |
| Shared quantity and tied spatial entries | 10 |
| Every authored action/DIM/MRGN record | 56 |
| Active worker/rally/order/bullet references | 4 |
| Successful queue resumption versus rollback | 4 |
| Flyer density and native RNG guard | 40 |
| Owner/shared-vision masks | 32 |
| Occupied target and original ring ordering | 2 |
| Source/destination field widths and guards | 30 |
| Linked image redraw rectangle/memset guards | 12 |
| **Total** | **475** |

## Reproduction

Use the pinned x86_64 runtime and private inputs described in
[reproduction.md](reproduction.md). The probe accepts `--exe`, `--data`,
`--manifest`, `--pyms`, `--static-out` and `--out`; original execution additionally
requires `--execute-original-code`. The published script SHA-256 is
`50f3087b82de7b8b0e2fe6d0c391bc03a0b46a9144060fd6907e1e6bf86dcb2a`.
It verifies the executable, effective DAT,
map hashes and pinned source dependencies. Output contains function witnesses,
hashes, inputs and state results; retail CHK/DAT/executable copies remain outside
the project.
