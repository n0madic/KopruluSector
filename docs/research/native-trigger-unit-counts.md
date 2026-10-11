# Native Command, Bring and unit-count caches, StarCraft 1.17

The original executable passed **5,200 isolated checks** in this batch.
Command and Bring share a cached count getter, but prepare their counts
differently. Command copies global unit counters; Bring visits a spatial query
and builds counts for one location. Their comparison mode also changes the
counter family: **at most uses all-unit counters; at least and exactly use
completed-unit counters**. Completed Bring counts include some stored child
relationships that are absent from the all-unit Bring counts.

This report concerns original counting, cache and condition contracts. It does
not treat decoded campaign parameters as a played mission snapshot, and does
not execute original unit creation or whole-game simulation. Scalar resources,
scores and historical kill/death counters are covered in
[native-trigger-counters.md](native-trigger-counters.md). Scalar rankings and
Opponents are covered in [native-trigger-ranking.md](native-trigger-ranking.md).

## Provenance and reproducibility

The target is the local StarCraft 1.17.0.1 executable, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image
base `0x400000`. Listed addresses are preferred VAs; RVAs are VA minus
`0x400000`. No 1.16.1 address is assumed to identify a 1.17 function.

The artifacts are:

- [Probe script](../../tools/research/probe_native_trigger_unit_counts_117.py).
- [Static instruction inventory](evidence/native-trigger-unit-counts-static.json).
- [Native execution results](evidence/native-trigger-unit-counts-probes.json).
- [Retail input manifest](evidence/retail-manifest.json).

The successful run used Rosetta `x86_64`, Python 3.9.6, Unicorn 2.1.4, pefile
2024.8.26 and Capstone 5.0.6. The probe-script SHA-256 is
`c1788e36afa9766d629a4e79c751e8e630fa5894f6b853a06cdf30b762292690`.
The evidence records 1,427 distinct executed instruction witnesses, and the
static inventory covers 40 reviewed blocks.

The script gates the EXE, manifest inputs and effective `units.dat` by hash and
gates the PyMS parser checkout by commit
`bfc5d3aad0b5614a5aff72c223f8efa00afddfa4`. The effective units DAT hash is
`da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba`.
DAT group flags, special flags, cargo capacity and extents seed the fixture's
native tables. Terrain, units and sorted spatial entries are explicitly supplied
fixture state. The global source counter arrays are also host inputs;
the original copy/query code executes on them.

All native calls execute complete original functions and their actual selected
callees, with no stubs or imported functions. The per-instruction allowlist
excludes game entry points and unrelated code. Native memory functions execute
their original REP paths with an explicit feature-bit fixture. No original
allocation, Windows API, file or network code executes.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_trigger_unit_counts_117.py \
  --exe /path/to/StarCraft.exe \
  --data /path/to/extracted-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --pyms /path/to/pinned-PyMS \
  --static-out docs/research/evidence/native-trigger-unit-counts-static.json \
  --execute-original-code \
  --out docs/research/evidence/native-trigger-unit-counts-probes.json
```

Without the execution flag, the script prints a plan and can emit the static
instruction inventory. Dependency setup and input extraction are documented in
[reproduction.md](reproduction.md). Retail originals stay outside the project.

## Condition wrappers and count getter

| Condition | Wrapper VA | Cache preparation |
| --- | --- | --- |
| Command, ID 2 | `0x4b8ed0` | Global `0x4b9c00` |
| Bring, ID 3 | `0x4b8f20` | Location `0x4ba350` |
| Command the Most, ID 6 | `0x4b9070` | Global `0x4b9c00` |
| Commands the Most At, ID 7 | `0x4b90e0` | Location `0x4ba350` |
| Command the Least, ID 16 | `0x4b93b0` | Global `0x4b9c00` |
| Command the Least At, ID 17 | `0x4b9420` | Location `0x4ba350` |

Command and Bring read player selector DWORD `+4`, amount DWORD `+8`, unit WORD
`+0xc` and comparison byte `+0xe` from the raw condition. The original getter
`0x4ba680` receives selector in ECX, unit/group ID in DX and a stack DWORD
selecting the completed family when nonzero. The wrappers pass zero only for
comparison code 1. All other codes query the completed family, including invalid
codes that later return false. Codes `0/1/10` mean unsigned at least/at most/
exactly; other codes return false.

Bring and the At rankings use only the **low byte** of the location DWORD.
Zero returns false before cache preparation. Other bytes select zero-based
location `low_byte-1`. Synthetic 256 and 257 therefore behave like 0 and 1.
Ordinary retail locations in this inventory stay within the normal byte domain.

The same direct and grouped player-selector rules as the scalar counters apply:
direct slots bypass active-player filtering, Current uses `0x6ca754`, groups use
the eligible iterator, All Players appends slots 8–11, Forces use one-based force
bytes, and an alternate four-slot sequence retains duplicates. Summation wraps
at 32 bits.

## Cache layout and global copy

The cache key is byte `0x6ca6d0`: 0 invalid, 1 global, 2 location. Byte
`0x6ca6d1` holds the location index for key 2. The shared count arrays are:

| Field | All units | Completed units |
| --- | --- | --- |
| Individual unit/player counters | `0x6c7c10` | `0x6c5150` |
| Men | `0x6c4c10` | `0x6c4c40` |
| Buildings | `0x6c4d60` | `0x6c4d90` |
| Factories | `0x6c4eb0` | `0x6c4ee0` |

Individual entries are DWORDs at `base + 4*(12*unit_id + player)`. Aggregate
entries use `base + 4*player`. Group IDs are 229 Any Units, 230 Men, 231 Buildings
and 232 Factories. Any Units sums Men and Buildings; it does not sum the whole
individual array. Other ordinary unit IDs read the individual counter.

Global preparation `0x4b9c00` immediately returns if key 1 is already set.
Otherwise it sets key 1, copies `0x540` bytes from `0x5290c4` into `0x6c4c10`
with `rep movsd`, then invokes the real native `0x4d16a0` memory function for
two `0x2ac0`-byte copies: `0x52c0c4 → 0x6c5150` and
`0x529604 → 0x6c7c10`. The probes check the entire aggregate block and every
individual counter after these calls. They do not execute the gameplay code
that maintains the source arrays.

Location preparation `0x4ba350` returns immediately only when both key 2 and the
location byte match. Otherwise it sets that key/index and uses actual native
`0x4d0c60` memset calls to clear the `0x540` aggregate block and both individual
arrays. It then constructs a rectangle and passes the location elevation flags
to the original visitor and collector.

The native invalidator `0x4b9820` writes key zero. Sequence checks demonstrate
that changing global sources or unit completion without invalidating a matching
cache preserves the old result; calling the original invalidator forces a new
copy or location traversal.

Static direct-call anchors for invalidation include Move Location at
`0x4b5822`, Move Unit at `0x4b58eb`, Kill Unit at `0x4b6860`, Kill At at
`0x4b68a6`, Remove Unit at `0x4b68e0`, Remove At at `0x4b6926`, and the trigger
cycle's reset before player iteration at `0x4be55b`. These anchors do not prove
every mutation path's invalidation behavior. Creation, ownership transfer and
gameplay producer changes need their own contracts.

## Bring traversal and elevation

Location preparation loads the low WORD of the four MRGN corner DWORDs and the
WORD elevation flags at location `+0x12`. It calls visitor `0x443110`, which
obtains a list from the original location spatial query `0x4224c0`, invokes
callback `0x4ba830` for each result, then releases the list with `0x4222b0`.
The callback always returns zero, so the visitor processes the entire list.

The query operates on native sorted X/Y border tables, rather than comparing
every unit center with the location. It uses maximum unit-width and unit-height
globals `0x5d6e40/0x5d6e44` to expand short query ranges and apply unit-extent
checks. The fixture loads actual DAT extents and supplies the border indexes
and original unit-pool addresses. Selected spatial helpers also appear in
[native-placement.md](native-placement.md); this batch reviews its own address
bounds and executes the location wrapper's real path.

The native query has boundary behavior that cannot be replaced by a simple
center test. With a location `[32,32,64,64]`, Marine extents and maximum-width
fixture 256, a Marine centered at x=72 is included because its left extent
touches x=64. With maximum width 17, that upper-edge touch is excluded. The
tests also distinguish lower-edge touches and the corresponding Y boundaries.
The fixture's stock-DAT-derived maxima are width 256 and height 160; this is
fixture setup, not proof of every game's maximum-size initialization path.

Zero-area `[64,64,64,64]` can return a unit at that point. Inverted
`[96,96,32,32]` can return a sufficiently large unit spanning both corners
(tested with type 202) while a Marine at the same center is excluded. These are
post-load rectangle fixtures; they do not establish loader normalization or
claim that a selected retail condition has an inverted rectangle. Rejecting
every zero-area or inverted rectangle before this original query would change
the observed function contract.

The callback first asks elevation exclusion helper `0x4b95f0`. That helper reads
the unit sprite position, calls the actual terrain-height function `0x445690`,
and selects a location exclusion bit:

| Terrain height | Ground, InAir bit 4 clear | Air, InAir bit 4 set |
| --- | --- | --- |
| 0 | 1 | 8 |
| 1 | 2 | 16 |
| 2 | 4 | 32 |

A set bit excludes the unit. All 64 flag combinations are tested for each of
three heights and both statuses. Terrain MTXM, CV5 and VF4 tables are private
flat fixtures, but the original height lookup and exclusion helper execute.
Native `0x4b9654 and esi,1` handles low ground; `0x4b962f and esi,8` handles
low air. The pinned OpenBW `unit_is_at_elevation_flags` assigns the opposite
ground/air bit sets, so it must not be copied as the original contract.

This is reachable through stock campaign records, rather than just an editor
edge case. Among the raw Bring/Most At/Least At records with a nonzero location
byte, masks are 0 in 826 records, 56 in 102, 61 in 14, 7 in 6, 60 in 2 and 62
in 1. Thus **125 raw condition records reference nonzero elevation masks**.
All 125 have their condition ignore bit clear.
Three other Bring records have location byte zero and return false. Mask 45
exists in the retail MRGN inventory but has no selected condition reference.

For example, expansion Protoss 03 uses mask 56 in Bring conditions for unit
IDs 61 and 151 at locations 2/3/7/8. Mask 56 allows ground units at all heights
and excludes air units at all heights in the original helper. Expansion Protoss
06 has a Shuttle 69 Bring record at location 31 with mask 7, selecting the
complementary ground exclusion. The per-record map/trigger/slot references are
recorded in the generated campaign cases. These input references do not prove
when a mission makes those conditions true.

After elevation, callback `0x4ba830` excludes hallucinations, status bit
`0x40000000`, and DAT subunits, special flag `0x10`. It then invokes all-unit
increment `0x4b9970`. If completion status bit 1 is set, it additionally invokes
completed increment `0x4b9830`.

## Individual and aggregate increments

The all-unit increment counts the root's individual type and owner. DAT group
flag `0x20` increments Factories; group `0x08` increments Men, otherwise group
`0x10` increments Buildings. Zerg Egg 36, Cocoon 59 and Lurker Egg 97 explicitly
increment all-unit Men even without the ordinary Men group flag.

The completed increment counts the root's individual type and owner and uses
the same DAT Factory/Men/Building bits, but does not apply that Egg fallback.
It also adds stored children:

- A transport-capable parent recursively counts its nonempty cargo slots via
  original `0x4381f0`, `0x4381d0`, `0x428b50`, `0x41fe20` and unit-pool getter
  `0x4255f0`. Overlord ID 42 requires transport upgrade 24. Cargo resolution
  validates the slot's unit index, generation byte and live-unit fields. These
  are fixture objects; no cargo loading or unit allocation executes.
- Carrier 72/82 and Reaver 81/83 add the inside-count byte at unit `+0xc8` to
  the Interceptor 73 or Scarab 85 individual counter using actual child-type
  helper `0x41cc20`. They do not add those stored counts to Men/Buildings.
- A worker, DAT special flag 8, with a powerup pointer at `+0xc0` adds the
  powerup's individual type to the worker owner's completed count. Flag unit
  215 instead recurses into the flag object and uses that object's owner.
- Nuclear Silo 108 with nonzero readiness/pointer DWORD `+0xd4` increments the
  completed Nuclear Missile 14 counter for the silo owner.

Cargo recursion does not retest the child's completion status, location or
elevation. A completed transport can therefore contribute a resolved incomplete
cargo child's completed counter. Stored cargo, hangar, powerup and nuke entries
are not separate all-unit increments in this location traversal. This is why
Bring at most and Bring exactly can query different counts even for the same
location and unit type.

The cargo lookup's packed WORD uses a low 11-bit one-based pool index and a
high 5-bit generation tag. Helper `0x428b50` resolves the pool object, invokes
the actual `0x41fe20` invalid predicate, then compares the tag with unit byte
`+0xa5`. The invalid predicate rejects a null sprite, or order byte `+0x4d=0`
with order-state byte `+0x4e=1`; it does not reject every order-0 state.
Separate native cases exercise all 32 tags, matching/mismatching generation,
sprite presence, four order/state pairs, and the zero-slot sentinel. This proves
the lookup's selected input contract, not allocation's generation lifecycle.

## Command and Bring rankings

The four Command/At ranking wrappers always query the completed family, compare
eligible slots `0..7`, and accept ties using unsigned extrema. Their condition
player selector, threshold and comparison fields do not select the ranking
opponents. Current Player comes from `0x6ca754`. Most initializes Current value
and maximum to 0; Least initializes both to `0xffffffff`. Empty eligible
sequences pass, and inactive Current Player retains that initial value. Bring
rankings return false for a zero location byte before this ranking loop.

Two At-ranking records occur in the physical campaign inventory; the other
ranking variants do not occur. Their parameters are checked with controlled
location/unit fixtures, alongside ordinary Command and Bring inputs.

## Campaign parameter coverage

All 68 physical CHKs are scanned, and all have at least one selected condition.
The inventory is physical-file coverage, not a 68-mission play order. The four
VER 57 prerelease files contribute 32 Command and three Bring records. The
remaining 64 supported catalog maps contain 402 Command, 949 Bring, one Most At
and one Least At record, totaling 1,353.

| Condition | Raw physical-file records | Distinct decoded parameter tuples |
| --- | --- | --- |
| Command 2 | 434 | 248 |
| Bring 3 | 952 | 879 |
| Commands the Most At 7 | 1 | 1 |
| Command the Least At 17 | 1 | 1 |
| **Total** | **1,388** | **1,129** |

Eight Command and 12 Bring conditions have ignore flag 2. Their decoded wrapper
arguments are checked and their references marked ignored; the normal VM skips
these records. Record counts do not prove branch reachability or mission timing.

Bring tuples include each referenced location's corners and elevation mask.
The fixture supplies a matching unit inside each positive rectangle, uses a
selected owner where available, and uses flat height zero. Unit completion and
air status are explicit fixture inputs; they do not claim the campaign reaches
that state. Global Command tuples use distinct seeded source counters. All
decoded ordinary selectors and unit IDs in this inventory are supported, with
zero unresolved tuples.

The checks consist of 3,515 synthetic condition calls, 40 native location-query
and release checks, three cache sequences, 513 cargo-lookup checks and 1,129
campaign parameter calls. Synthetic conditions comprise 1,968 Command,
1,411 Bring, 64 Command Most, 64 Command Least, four Most At and four Least At
calls. The counter tests include unsigned wraparound, distinct completed/all
families, all 228 DAT classes, hallucination/subunit filters, all 64 elevation
masks at three heights for ground/air, selectors, stored child relationships
and location-byte edge cases. Repeated calls within cache sequences are reported
as one sequence check rather than additional independent cases.

The two At-ranking records are in base Zerg 05, raw triggers 27 and 29,
condition slot 0. Both select unit ID 15 at location 18, whose bounds are
`[3008,1248,3648,1600]` and elevation mask 0. Their raw selector is 0, but the
ranking routines compare Current Player, as separately established above.

## Primary sources and remaining dependencies

[BWAPI trigger declarations](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/Triggers.h)
provide condition names, and
[GPTP unit status declarations](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/enumerations.h)
provide InAir, Completed and Hallucination terminology.
[PyMS UnitsDAT](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/DAT/UnitsDAT.py)
decodes the fixture's effective retail DAT. Original instruction evidence and
native checks establish the behavior, rather than these names alone.

The pinned [OpenBW trigger counting implementation](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L18791)
is a useful structural comparison. Its completion-dependent families and cargo
recursion point to relevant mechanisms, but it has differences such as the
reversed elevation bits, exactly comparison code 2 instead of native 10, and
selector filtering documented in the scalar report. Its individual noncompleted
`trigger_command_count` expression indexes `unit_counts[owner]` inside the
selected-player loop, whereas the native getter indexes the selected player.
Its Reaver branch in `trigger_bring_count` adds the stored inside count to the
Reaver counter, while original helper `0x41cc20` selects Scarab 85. The native
relationship fixture with Reaver 83 and inside count 4 has one completed Reaver
and four completed Scarabs. The same stock campaigns have positive Reaver
hangars, whose creation is separately listed as a dependency in
[native-unit-properties.md](native-unit-properties.md).

This batch does not establish global producer maintenance, sorted-index
construction during movement, real cargo loading, generation allocation,
hangar creation, worker pickup, nuclear missile production, all cache
invalidation paths, gameplay terrain changes or played mission results. Those
remain required dependencies for integration and full campaign parity. Save
state must preserve the source counters, trigger cache state or its correct
invalidation, location state and the underlying stored relationships.
