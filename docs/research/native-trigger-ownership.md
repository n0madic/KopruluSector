# Native Give Unit ownership transfer, StarCraft 1.17

The original Give Unit action passed **1,245 complete no-stub native cases**.
This batch closes ordinary, unlinked ownership transfer: owner-list mutation,
live/completed unit and supply accounting, score credit, order reset, sprite
allegiance, researched technology/upgrade copying and immediate trigger-cache
effects. It extends the rejecting/empty-state inventory in
[native-trigger-unit-actions.md](native-trigger-unit-actions.md).

The target is **StarCraft.exe 1.17.0.1**, SHA-256
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
Addresses are preferred VAs with ImageBase `0x400000`; subtract that base for
RVAs. The [static witnesses](evidence/native-trigger-ownership-static.json),
[native cases](evidence/native-trigger-ownership-probes.json) and
[probe](../../tools/research/probe_native_trigger_ownership_117.py) preserve the
exact instructions, hashes, dependencies, dynamic inputs and native call paths.
This is isolated original-code execution in private Unicorn x86 memory, not
retail process execution, played campaigns or complete engine compatibility.
Every selected function and callee returns through original instructions.
No helper stubs, imported functions, Windows APIs, WinMain, game loop or guest
file/network operations execute.

## Corpus and evidence boundary

The retail corpus contains **211 stored Give action records** in **20 physical
maps**, represented by **154 distinct parameter tuples**. All are in the 64
supported catalog maps; the four version-57 extras contain no Give records.
Twenty-two records are ignored actions. Their flags, map hashes, trigger/action
positions and owners remain in the static evidence. Calling their helpers as
parameter probes does not assert that ignored actions execute in missions.
See [campaign provenance](campaigns.md) for physical-map and progression
distinctions.

All 154 tuples execute the full original action in an ordinary private object
state. **143 transfer the candidate; 11 take native rejection paths**. The tuple
contains the source location/selector, type, destination and quantity decoded
from the real CHK. A private stock-type candidate is selected for that request:
individual IDs retain their type; Any/Men use type 15; Buildings uses type 106.
The fixtures contain no linked addon, carried powerup, loaded child, hangar
child or nonnull AI record. Their dynamic status is `0x1`: completed, with
status bit `0x2` clear. Thus a stock building type supplies its real counter/DAT
parameters while bypassing the wrapper's positive status-bit-2 training and
animation branch. Recipients use controller 2 for these tuple cases.
These are authored campaign parameters applied to supplied state, not captured
mission state or proof that the original condition is satisfiable.

Stock effective `units.dat`, `flingy.dat` and `orders.dat` populate the native
DAT arrays. Unit movement-control byte `+0x27` is explicitly zero: when a stock
movement-control-2 entry invokes `0x42dff0`, its actual unit-control-zero guard
returns before reading iscript. This does not close a real iscript-controlled
unit's speed recalculation. Original technology/upgrade state is zero in the
tuple batch, and the expansion byte is an explicit zero fixture. Separate
research-copy cases vary that expansion byte and researched state.

The 1,245 cases comprise:

| Family | Cases |
|---|---:|
| Ordinary Civilian transfer, including neutral owner 11 | 3 |
| All decoded campaign Give tuples | 154 |
| Recipient controllers 0–7 and completion/status variants | 32 |
| Shared quantity, rejected candidate and exact owner-list order | 24 |
| Real cache preparation, Give, stale reuse, invalidation and rebuild | 8 |
| Marine/Medic technology and upgrade copying, both expansion modes | 1,024 |

Dynamic units, sprites, images, lists, counters and free order nodes are private
fixtures. Counts start at 17 and group/supply/score aggregates at 1000 so exact
deltas can be compared. They do not describe the visible object count in a
mission. The real allocator/list code operates on the private primary-order
pool. Empty AI pools, empty selection groups and zero map-region count select
reviewed native guards; no successful replacement helper is used.

## Action and connected transfer

Give action 48 enters `0x4b5a50`. Its source location is narrowed to a byte;
zero returns success. The spatial visitor uses the actual `0x443110 -> 0x4224c0`
collector and callback `0x4b7750`. Quantity byte zero becomes **65535** in the
callback's remaining **u16** field. A successful recipient helper decrements
that word; rejected candidates do not consume it. The callback returns zero so
the collector continues visiting, while a zero remaining word prevents more
transfers.

The 24 multiple-unit cases prove a **shared quantity across owners**. All
Players with quantity 1 transfers one eligible candidate in total, unlike
Kill/Remove At's per-player limit. They also prove that a first hallucinated
candidate and a unit already owned by the recipient do not consume the budget.
Actual selected call order and every player's doubly linked owner list are
checked, including head, predecessor and successor changes.

Recipient helper `0x4b81a0` resolves destination 13 to Current Player and checks
destination less than 12. It rejects hallucinations, resource containers,
types 73/85, DAT addon bit 2 and unchanged ownership. Accepted ordinary units
execute `0x428df0 -> 0x426ed0`. The complete wrapper has additional real branches
for linked gas buildings, cargo, hangar children, carried buildings and units
at `+0xd0`; their positive transfer is a remaining requirement.

Core `0x426ed0` first clears applicable AI associations through `0x48dbc0`,
evaluates the temporary-completion helper `0x43c040`, then removes old-player
accounting. It unlinks `+0x68/+0x6c`, adjusts old player head
`0x665b08 + 4*owner`, changes owner byte `+0x4c`, and prepends the unit to the
new player's list. A nonempty new head gains the new unit as its predecessor.
It then applies new-player accounting, target allegiance cleanup, UI dirty
flags, recipient AI admission, subunit handling and upgrade effects.

The probes check ordinary and temporarily incomplete/status variants. They do
not claim closure of the positive Zerg morph-completion override, selected
sprite/selection-circle reconstruction or associated image/iscript paths.

## Counter, supply and score effects

| Native helper | VA | Selected effect |
|---|---|---|
| Live count remove/add | `0x414590` / `0x414070` | Pass -1/+1 to `0x414760` |
| Live accounting | `0x414760` | Per-type, group and required-supply changes |
| Completed count remove/add | `0x414580` / `0x414060` | Pass -1/+1 to `0x4145a0` |
| Completed accounting | `0x4145a0` | Completed counts, provided supply and optional aggregate/score credit |

The per-type live source array is `0x529604`; completed is `0x52c0c4`, each
indexed by `type*12 + owner`. For a completed ordinary transfer, a fixture
source value 17 becomes 16 and recipient 17 becomes 18. Hallucinations and DAT
subunits bypass those counters. Incomplete ordinary units move live accounting
but do not move completed accounting.

Men, Buildings and Factories group counters follow the native DAT tests.
Live Men accounting has explicit type-36/59/97 fallback; completed Men uses
the group flag. Required/provided supply follows race-group priority
1, then 2, then 4, and the stock supply bytes. The evidence contains all tested
aggregate and race-supply values for every campaign tuple.

Give is **not a symmetric transfer of every statistic**. The core passes zero
to the old completed-accounting call and one to the new call. For a completed
recipient other than owner 11, the new side receives aggregate credit at
`0x529124` for most Men or `0x529274` for most Buildings, and the unit's DAT
build score is credited to Units score `0x5291b4` or Buildings score `0x529304`.
Native exception tables at `0x414704` and `0x414748` can skip the aggregate
increment while retaining the score credit. The old side does not lose those
aggregate/score values. The per-type `0x531644` array stays unchanged in this
transfer. `produced_*` probe-field names are analytical labels for those
aggregates, not an assertion that every eventual score-screen consumer has
been recovered. [Scalar score getters](native-trigger-counters.md) independently
establish the Units/Buildings score components.

## Sprite allegiance, order reset and AI guards

`0x428df0` calls sprite-owner helper `0x426e80` only when the recipient byte is
less than 8. Its actual `0x417160` sets sprite byte `+0xa`, then marks every
linked image's u16 flags with bit 1. The basic cases use a nonempty image list.
**Giving to neutral owner 11 changes CUnit ownership while preserving the old
sprite allegiance byte.** This matters because the native enemy helper uses
sprite allegiance for neutral-owned units; see the two-stage allegiance cases
in [native-trigger-unit-actions.md](native-trigger-unit-actions.md).

For ordinary completed units eligible for an idle reset, recipient controller
1 selects DAT computer-idle byte `0x520d50 + type`, controller 3 selects order
161, controller 7 selects order 162, and other controllers select DAT human-idle
byte `0x520e38 + type`. `0x42a2c0` and the real order queue/activation helpers
execute. Incomplete units, single-entity DAT bit `0x800`, types 14/73/85 and
status bit `0x40` bypass that reset. The Civilian cases verify all eight
controller values and those selected status guards. Their initial user-action
byte `0x82` remains `0x82`; Give does not gain Kill's initial Death user-action
write.

The actual `0x4a7ed0` and its three callees set command/selection/portrait dirty
flags and clear portrait pointers; they perform no imported UI action. For
controller 1, AI admission sees the explicitly empty town/guard pools and
returns through original guards. Positive allocation/reassignment of guard,
worker, town or military AI records is still a connected-transfer dependency.
The [training lifecycle](native-training-lifecycle.md) and
[guard transfer](native-guard-transfer.md) reports supply related helper
contracts, without treating isolated coverage as complete Give behavior.

## Research and upgrade copying

Before changing the unit owner, `0x428df0` invokes the original per-unit recipe
iterators `0x4bbcb0` and `0x4bbd40`. Their native constant tables enumerate
technology and upgrade IDs for that unit. Native callbacks behave as follows:

- `0x429490`: if the source researched-tech byte is nonzero and the recipient
  byte is zero, call real setter `0x49a300` with **1**. Preserve an already
  nonzero recipient byte; do not clear it when the source is zero.
- `0x4294d0`: compare source and recipient upgrade bytes and use real setter
  `0x49a370` only when the source is larger.
- Both setters gate expansion-only IDs on byte `0x536720`: technologies
  24 and above, upgrades 46 and above. Their getters still read the selected
  arrays. Native setters do not clamp these supplied bytes to a DAT maximum.

The 1,024 connected Give cases use the original Marine recipe IDs technology
0/upgrade 16 and Medic IDs technology 34/upgrade 51. They vary source and
recipient bytes through 0, 1, 3 and 255, and expansion mode through 0/1.
Values 3/255 for researched booleans are width fixtures, not claims about
authored researched mission states. With expansion disabled, the Medic's
expansion setters leave recipient state unchanged; with it enabled, the rules
above apply. Marine's base IDs remain writable in both modes.

The post-transfer upgrade iterator and actual `0x43b0b0` apply-dispatch function
also execute. The selected IDs 16/51 take their native no-immediate-effect
paths. This does not close every researched speed/range effect on all units
or the full connected iscript-controlled speed update.

## Cache consistency

Give changes live/completed source arrays but does not invalidate a prepared
global Command cache. Eight complete native sequences execute cache preparation
`0x4b9c00`, Give, cache reuse, getter `0x4ba680`, invalidator `0x4b9820`, rebuild
and getter again. For both all/completed families, the old player's cached
count remains **17** after Give while the source count is **16**. The real
invalidation/rebuild exposes 16. Global mode byte `0x6ca6d0` stays 1 through
Give and reuse.

The exact scheduler instructions at `0x4be55b` call the invalidator before
entering player/trigger traversal at `0x4be565`, rather than after every Give.
Direct-call inspection finds invalidation in selected Move/Kill/Remove wrappers
and that scheduler call. The tested stale-cache contract must be preserved;
an authored multi-trigger scheduler trace is a separate verification from this
connected helper sequence. See [count caches](native-trigger-unit-counts.md)
and [trigger scheduler](native-triggers.md).

## Reproduction and remaining work

The successful run used Rosetta x86_64, Python 3.9.6, Unicorn 2.1.4, pefile
2024.8.26 and Capstone 5.0.6. It records **1,999 executed instruction witnesses**.
Probe SHA-256 is
`c3f7a5ee2372c70450b8e2d5cda15271affc2706f8a6a7c5cfbfe99b383219c3`.
The evidence records SHA-256 for five imported research modules, EXE, manifest,
Units/Orders/Flingy DAT and pinned PyMS commit. Freshness is checked against
the current files; no retail binary, CHK or DAT is copied into this report.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_trigger_ownership_117.py \
  --exe /path/to/StarCraft.exe \
  --data /path/to/extracted-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --pyms /path/to/pinned-PyMS \
  --static-out docs/research/evidence/native-trigger-ownership-static.json \
  --execute-original-code \
  --out docs/research/evidence/native-trigger-ownership-probes.json
```

Omit the execution flag for static inventory only. Input extraction and
runtime setup are in [reproduction.md](reproduction.md). The field schemas
are pinned [PyMS UnitsDAT](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/DAT/UnitsDAT.py)
and [FlingyDAT](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/DAT/FlingyDAT.py);
native instruction/case evidence establishes the behavior above.

Remaining connected cases are positive addon/gas/cargo/hangar/carried-unit
transfers, nonnull AI cleanup/admission, selected-image/selection-circle paths,
completed morph overrides, positive building/flying-unit animation transitions,
all upgraded iscript-controlled movement effects, native authored scheduler
traces and full mission playthroughs. This batch materially narrows R30 and
does not certify all Give branches or overall remake parity.
