# Native creep geometry, pools and Trigger Move

**4,372 isolated native cases passed:** 4,335 complete original function paths,
31 explicitly stopped prefixes, and six original arithmetic suffixes. This batch
closes creep bounds, provider retention, finite receding-node allocation and
cancellation, both border-table initializers, and the neighbor-to-border lookup.
It connects grounded Zerg Move to positive old-area removal and identifies the
exact dependency at which new creep generation requires Windows thread state.
It extends [grounded Move](native-trigger-grounded-move.md); it is not a full-game
or played-campaign result.

The target is StarCraft.exe **1.17.0.1**, SHA256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
Preferred ImageBase is `0x400000`; addresses below are preferred VAs. Exact bytes,
RVAs, calls, states, dependency hashes and runtime versions are in the
[composed static witnesses](evidence/native-trigger-creep-static.json),
[dedicated creep witnesses](evidence/native-creep-static.json) and
[native cases](evidence/native-trigger-creep-probes.json).

The [probe](../../tools/research/probe_native_trigger_creep_117.py) SHA256 is
`7d6227fc3a671961ea5ff8a5bb567fdf3fc4a119f5546439c132e2bb286c1dce`.
The [inspector](../../tools/research/inspect_native_creep_117.py) SHA256 is
`64974a0ebdd2d2f876ad15383048fb7f0986ba4175ed0a1d8c48efe6a91408c5`.
The composed allowlist contains **315 blocks / 14,949 instructions**;
**4,469 distinct instruction addresses** executed. No helper stubs or imported
functions execute. Original instructions run unchanged in private Unicorn memory
under x86_64 Python/Rosetta, with a one-million-instruction bound per call. There
is no Windows process, WinMain, game loop, guest file/network activity or retail
asset upload. Host reads prepare the fixtures before guest execution.

## Complete paths and evidence boundaries

| Family | Cases | Boundary |
|---|---:|---|
| Receding pool initialization | 1 | Complete original |
| Signed integer ellipse, boundaries and wrapping inputs | 1,171 | Complete original |
| All 228 stock type IDs × four completion arguments | 912 | Complete original |
| All 256 neighbor masks at five interior/edge points | 1,280 | Complete original |
| Stock bounds and clipped map edges | 114 | Complete original |
| Stock sustaining-type/geometry predicate | 228 | Complete original |
| Actual spatial queries, owners 2/11 and status gates | 240 | Complete original |
| Hash collisions, cancellation and 1,024-node exhaustion | 6 | Complete original |
| Old-provider removal, another live provider, occupied/receding tiles | 72 | Complete original |
| Provider-area cancellation with positive nodes | 36 | Complete original |
| Old-provider removal with a genuinely exhausted pool | 1 | Complete original |
| Both original border-table initializers | 1 | Complete original |
| Border lookup, all interior masks and selected edges | 272 | Complete original |
| Extractor Move resource-container rejection | 1 | Complete original action |
| Ranked spread candidates and source-26 selection | 24 | Prefix before Windows import |
| Grounded Zerg Move with positive old creep | 7 | Prefix before Windows import |
| CRT RNG arithmetic after context acquisition | 6 | Original suffix, not full rand |
| **Total** | **4,372** | **4,335 complete / 31 prefixes / 6 suffixes** |

Units, dimensions, flat terrain, MTXM/backup/graphics arrays, sprite and unit lists
are private state. The stock units/flingy/orders DAT inputs are hash-checked and
reported. The units.dat hash is
`da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba`.
These cases add **no authored campaign-parameter coverage**: the existing Move
reports separately cover the 56 stored Move records and their DIM/MRGN/action
parameters. The seven Zerg connections use selected existing-unit states, not
recorded mission execution.

## Providers, bounds and owner-independent retention

`0x4c0790` takes `ECX=type`, `EDX=completion_argument`. Hatchery 131 and Creep
Colony 143 return the supplied completion value literally. Lair 132, Hive 133,
Spore Colony 144 and Sunken Colony 146 return 1 regardless of completion. Other
stock IDs return 0. Inputs 0, 1, 2 and `0xffffffff` distinguish literal return
from Boolean normalization; the unusual values are register-level cases, not
legal completion states.

Bounds helper `0x4c04f0` takes `ECX=type`, `EDX=center_x`, stack
`[center_y, output_rectangle, completed]`, and returns whether the large provider
area applies. A provider uses ±320 pixels horizontally and ±200 vertically;
other types use their stock placement footprint. Signed division truncates toward
zero. Each axis uses a minimum-clipping branch followed by an **else-if** maximum
branch. This is not simultaneous two-sided clipping on arbitrarily tiny maps.

The large-area ellipse rejects when the **signed, wrapping 32-bit** expression
`dx*dx*100 + dy*dy*256` exceeds `10,240,000` (`0x9c4000`). Under normal coordinate
bounds it is equivalent to `dx²*25 + dy²*64 > 2,560,000`, with axes 320 and 200.
The explicit overflow cases establish instruction arithmetic without claiming
those inputs occur in normal candidate enumeration.

Sustaining predicate `0x4c0830` requires the stock Building property and Zerg
Staredit group flag, checks the type-specific bounds, then the ellipse. Actual
query `0x4c0250 → 0x4b4170 → 0x422330` searches the tile-center rectangle ±640/400
pixels and requires CUnit Grounded status bit 2. It releases the original spatial
query with `0x4222b0`. **No owner filter applies**; a nearby grounded Zerg building
owned by player 11 retains creep just as the same profile owned by player 2 does.
A non-provider Zerg building can retain its footprint. These structures agree
with the independently pinned reconstruction, after checking exact local code.
[OpenBW sustaining query and pool operations](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L3864-L3983),
[provider dispatch](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L805-L813).

## Receding pool and old-area updates

`0x4b44a0` initializes **1,024 records of 20 bytes**. Buckets 0–8 group active
records by neighboring-creep count; bucket 9 is the free list. Heads are at
`0x6c4698 + 4*bucket`, WORD counts at `0x6c46c0 + 2*bucket`; the free-head alias
is `0x6c46bc`, and **`0x6c46d2` is the free-node count**. It is not a trigger
count. Records occupy `0x6be698..0x6c3684`.

A record contains bucket next/previous at offsets 0/4, hash next/previous at
8/12, BYTE tile x/y at 16/17, and BYTE bucket at 18. Hash heads begin at
`0x6c3698`; key is `(x + 17*y) & 1023`. Allocation `0x4b3f30` pops a free record,
counts real MTXM neighbors with `0x4c0060`, inserts both chains and sets physical
flag `0x10000000`. MTXM group-one test is `(word & 0x7ff0) == 0x10`; the center
is excluded, and neighbors outside map bounds are excluded. A word such as
`0x801d` still counts as creep.

Cancellation `0x4b4930` matches BYTE coordinates in the collision chain, removes
the bucket/hash links, returns the record to bucket 9 and clears the receding
flag. The probes exercise colliding tiles `(10,10)`, `(27,9)`, `(44,8)` and remove
middle, head and tail. Every measured pool state validates all 1,024 records,
backlinks, bucket counts and exact hash membership. The capacity test actually
allocates all 1,024 nodes; a further allocation returns 0 without pool writes.

Old-provider removal `0x4b46e0` enumerates native bounds/ellipse, skips non-creep
and already-receding tiles, and normally skips occupied flag `0x08000000`.
**Extractor 149 omits the occupied-bit exclusion** in this helper. It uses the
actual sustaining spatial query before allocating a receding node. Successful
completion resets `0x6c46d8` to 0. Genuine pool exhaustion returns 0 early and
preserves that counter. Restoration helper `0x4b4dd0` cancels only receding nodes
inside the unit's current native bounds/ellipse; outside nodes remain linked.

The [frame scheduler](native-frame.md) calls `0x4b4010` before AI. This pool
establishes the identity of its free-count input. Executing the later disappearance
scheduler/visual tile removal is a separate consumer of these records.

## Original border lookup and ranked spread

Both table initializers now execute completely: `0x4bff60` produces a 512-byte
neighbor-to-shape table at `0x6ca7c8`; `0x4c0010` scans it and assigns compact
ranks for represented shapes in the 128-byte table at `0x6ca9c8`. The probe
compares every table byte with an independent bit/rank model. Existing tables
are then consumed by real `0x4c0470 → 0x4c03f0/0x4c0e00 → 0x4c0ca0/0x4c0d90`.
The neighbor bit order is SE, S, SW, E, W, NE, N, NW; the ninth center offset
exists in the walker, but shifting into a BYTE accumulator drops bit 8. MTXM and
backup stay unchanged while the graphics byte receives the two-stage lookup.
External map/render callbacks are null fixtures. This establishes the algorithm,
not drawn pixels or tileset resource loading.

Spread-one `0x4c0e60` scans bounds in row-major order. Existing creep, blocked
physical tiles and occupied tiles are excluded. Eligible non-creep tiles need
one to eight creep neighbors and, for a large provider, ellipse inclusion.
Coordinates are stored in eight buckets of up to 240 entries at `0x6caa50`.
It selects the highest nonempty neighbor-count bucket, calls the **main game RNG
with source 26**, and picks `(result >> 4) % bucket_count`. The 24 native prefixes
compare every stored candidate, ordering, selected coordinate, live RNG state
and source/total counters under three seeds and enabled/disabled RNG. They stop
before the subsequent visual-variant RNG context import.

## Grounded Move connection and the CRT boundary

The seven selected types are Hatchery 131, Lair 132, Hive 133, Hydralisk Den 135,
Creep Colony 143, Spore Colony 144 and Sunken Colony 146. Their action executes
real placement, hide/relocation/reattachment, old occupancy removal/new occupancy
addition and positive old-area receding allocation. The pool free count and
exact old-tile set match the independent bounds/ellipse model. Providers next
enter footprint stamping `0x43c220 → 0x43c130 → 0x4c0dc0`; a RequiresCreep
non-provider enters `0x4c0aa0`. Extractor 149 is rejected earlier by the Move
callback's stock ResourceContainer property `0x2000`; it keeps its old position,
quantity and pool state. Helper-specific Extractor removal behavior therefore
must not be confused with admission by this action.

New-tile variant callback `0x4c0c40` calls statically linked CRT rand
`0x4d14d4` twice. Rand calls thread-context wrapper `0x4d744c → 0x4d7464`, which
**unconditionally reaches GetLastError IAT `0x4eb17c` at `0x4d7466`** before even
retrieving a pre-existing thread context. The later wrapper also uses thread-local
storage and SetLastError. Each prefix stops before that first import; no import
is emulated or replaced. Move has not returned, its quantity is still 1, and new
creep stamping is not claimed complete.

The pure original suffix `0x4d14d9..0x4d14f5`, entered after context acquisition
with an explicit private context pointer in EAX, independently establishes:

```text
CRT state = (state * 214013 + 2531011) modulo 2^32
CRT result = (state >> 16) & 32767
```

This is a different RNG from main game state `0x67393c`. Static variant code uses
the first draw modulo 100: values below 4 select `second % 7 + 6`, otherwise
`second % 6`; it saves an initially zero backup WORD before drawing. The six
suffix cases prove the arithmetic only. They do not execute context acquisition,
the full variant callback, or establish thread RNG initialization/consumption
order. Treating CRT randomness as source-26 main randomness would lose this
separation.

## Reproduction and remaining consumers

Prepare local inputs using [reproduction.md](reproduction.md), then run:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_trigger_creep_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out /private/tmp/creep-probes.json \
  --static-out /private/tmp/creep-composed-static.json \
  --execute-original-code
```

The dedicated inspector accepts `--exe` and `--out`. Omitting
`--execute-original-code` from the probe produces only a plan/static extraction.
Source and target hashes reject drift; retail byte payloads remain in private
scratch. Remaining: complete CRT/context/visual callback execution, resource
loading and rendered borders, full disappearance scheduling, all lifecycle
consumers and played-campaign traces. Positive construction/addon links are
separate grounded-Move dependencies; this batch does not claim their closure.
