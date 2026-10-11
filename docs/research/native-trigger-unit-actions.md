# Native trigger unit actions, alliances and outcomes, StarCraft 1.17

This batch recovers original action wrappers, player/type selection, immediate
cache effects, Kill/Remove Death-order issuance, accepted Order issuance,
alliance mutation and pending-result commit from **StarCraft.exe 1.17.0.1**.
The reviewed SHA-256 is
`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.
All addresses below are preferred VAs at ImageBase `0x400000`; subtract that
base for RVAs. This is original-code research, not a completed campaign engine.

The [static inventory](evidence/native-trigger-unit-actions-static.json),
[native cases](evidence/native-trigger-unit-actions-probes.json) and
[reproducible script](../../tools/research/probe_native_trigger_unit_actions_117.py)
preserve exact instructions, input hashes, dependencies, complete-return status,
selected native call arguments and scope limits. Execution uses private Unicorn
x86 memory under Rosetta. There are no helper stubs, imported functions, game
process, WinMain, game loop or guest file/network operations. Artificial unit,
sprite, image, order-pool, player and count tables discriminate native rules;
decoded campaign parameters are not authentic running mission state.

## Coverage and campaign inputs

The raw 68-file corpus contains **3,019 selected action records**, represented
by **2,137 distinct parameter tuples**. The 64 supported catalog maps contain
**2,907** of those records. The four version-57 extras contribute 98 Create With
Properties, ten Defeat and four Victory records. All 68 physical maps contribute
selected inputs; physical input coverage must not be interpreted as campaign
play order. See [campaign provenance and catalog distinctions](campaigns.md).

| Action ID | Name | Raw 68 | Supported 64 | Native wrapper |
|---:|---|---:|---:|---|
| 1 | Victory | 77 | 73 | `0x4b5ef0` |
| 2 | Defeat | 140 | 130 | `0x4b5f20` |
| 11 | Create Unit With Properties | 853 | 755 | `0x4b64d0` |
| 22 | Kill Unit | 23 | 23 | `0x4b6840` |
| 23 | Kill Unit At Location | 95 | 95 | `0x4b6870` |
| 24 | Remove Unit | 70 | 70 | `0x4b68c0` |
| 25 | Remove Unit At Location | 212 | 212 | `0x4b68f0` |
| 39 | Move Unit | 56 | 56 | `0x4b5840` |
| 44 | Create Unit | 519 | 519 | `0x4b64d0` |
| 46 | Order | 632 | 632 | `0x4b65a0` |
| 48 | Give Units To Player | 211 | 211 | `0x4b5a50` |
| 56 | Draw | 0 | 0 | `0x4b5f70` |
| 57 | Set Alliance Status | 131 | 131 | `0x4b5e90` |

Eighty records have action ignore bit2 set. Their references retain that flag,
map hash, trigger/action indices and raw owner/execution flags. Parameter probes
invoke the action helpers directly, including ignored records, to check their
decoding and native contract; they do not claim that ignored actions execute in
a mission. No selected stored action occurs beyond an earlier nonignored None
action in this corpus. Condition satisfiability and complete trigger execution
remain separate requirements.

The final native batch contains **6,548 passing cases**: 2,304 outcome wrappers,
nine actual action-VM cases, 336 result commits, 48 controller-retirement cases,
32 outcome timers, 1,260 sequential alliance mutations, 39 target cancellations,
25 Create quantity/neutral-owner/auxiliary-state cases and 32 accepted-type
Create allocation-cap-failure cases,
2,137 campaign parameter calls, 224 positive Kill/Remove cases, 36 stock type/group
cases, six recursive cargo cases, 30 accepted stock Order cases, 18 nonempty Move
rejections and 12 nonempty Give rejections. All reach original returns on the
explicitly selected paths. None ends at a substituted success helper.

## Kill and Remove: selection, limits and immediate effects

The four wrappers share `0x4b8cc0`. Kill sets global `0x6c4c00=0`; Remove sets it
to one. Whole-player actions pass location sentinel255 and limit999999.
Location actions read only the low byte of the source location: zero returns
success without changing the mode or invalidating the cache; a nonzero byte is
decremented to the zero-based index. A zero quantity byte becomes999999.
All four successful nonzero paths call `0x4b9820` to invalidate the
[Command/Bring cache](native-trigger-unit-counts.md), even if no unit matched.

The selector helper expands player groups through a distinct two-stack-argument
helper family (`0x4b96c0`, `0x4b97a0`, `0x4b9b80`, `0x4b9cc0`, `0x4b9ee0`,
`0x4b9fe0`). Individual matching callbacks are:

| Requested type | Callback | Rule after matching owner |
|---|---|---|
| 229, Any Unit | `0x4b8c40` | Accept every type |
| 230, Men | `0x4b8e10` | DAT group byte bit8 |
| 231, Buildings | `0x4b8c60` | DAT group byte bit16 |
| 232, Factories | `0x4b8c90` | DAT group byte bit32 |
| Every other value | `0x4b8e40` | Exact u16 type equality |

This selection must not reuse the count getter's aggregate interpretation of
Any Unit or its explicit Egg/Cocoon/Lurker Egg fallback. The native cases use
stock types0,15,106,111,36,59,97,176 and requests228..232 as well as individual
types. Their ownership and object state remain fixtures.

**A location quantity applies per selected player.** The group helper calls
`0x4b8cc0` with the same original limit for each player; that helper returns zero
at `0x4b8db9` or `0x4b8daa`, rather than reporting the number consumed. Cases with three units
for each of three owners and quantity1 prove that All Players kills one for
each selected owner. It is not one shared limit across the group.

Whole-player iteration follows native player-list head
`0x665b08+4*player` and unit next link `+0x6c`. It skips DAT subunits (bit16 in
the special-property dword) and units rejected by `0x41fe40` (null sprite or
primary order zero). The next link is captured before issuing Death.
Location iteration uses the real `0x443110 -> 0x4224c0` spatial collector,
native elevation exclusion and recursive callback `0x4b7d00`.

The location callback visits loaded units through the original transport and
cargo helpers before testing the parent for death. It similarly visits a
worker's carried powerup. Recursive children have the location check disabled
and set global `0x6c4c08=1`; they do not decrement the root limit at `0x6c4c04`.
Six no-stub cases prove that quantity1 kills a Dropship and both loaded children
through actual cargo lookup and actual Death orders. The children are absent
from the spatial tables. Positive carried-powerup dropping and associated
special-unit cleanup remain outside this batch.

For ordinary selected units, `0x42a4d0` drops a carried powerup when necessary,
clears the real order queue, sets user-action bit1, allocates/appends primary
order0, activates it through `0x42a610`, then calls `0x48dbc0` to clear AI links.
The positive fixtures have no carried powerup or AI record. All actual queue,
allocator, list, order-activation and null-AI callees execute. Source and cached
unit-count arrays retain their prior bytes immediately after the actions;
the cache validity byte becomes zero. This proves Death-order issuance, not
the later destruction tick, free-unit pool return or final death/kill counters.
See [primary order machinery](native-primary-orders.md).

Remove additionally invokes `0x422ba0` before Death for ordinary units. The
positive Remove fixtures start with sprite hidden flag0x20, so that real
function returns through its already-hidden guard. Removing an initially
visible unit requires its visibility, selection, image/iscript, occupancy and
counter cleanup callees; those side effects are not inferred from these cases.
The transport-location branch uses actual `0x425700` before Death.

## Create, Move, Give and Order

**Create IDs11 and44 use the same wrapper.** Source location is a low byte;
zero returns success. A quantity byte of zero means **one**, not zero.
`0x4b64e5` reads auxiliary dword **`0x673934`**, `0x4b64f1` sets it to123456789,
and `0x4b6535` restores that auxiliary state after the entire action. These
get/set functions are `0x4781e0` / `0x4781f0`. They do **not** operate on the
simulation LCG state `0x67393c`, whose getter is `0x4781d0`. The only direct
absolute references to `0x673934` in this EXE are those two get/set functions;
indirect consumers remain unclosed. A name such as "save RNG seed" would lose
this distinction. Positive allocation calls the main source14 PRNG through
`0x424900`; this wrapper contains no restoration of that main state.
Player helper `0x4b7020` rejects resolved owners>=8 before creation, then
redirects resource types176..178 and188 to owner11 through `0x424420`.
It computes the location center from signed dword sums with truncation toward
zero and calls `0x4261f0`. On successful creation, a property index1..64 selects
`0x6c46ec+20*index` and invokes `0x46a420`; zero or>64 skips that application.
See [UPRP application](native-unit-properties.md).

Campaign Create probes explicitly clear availability bit2 in the native
u16 array `0x524708`, so the actual `0x4261f0` takes its unavailable-type return.
They close the wrapper, selector, count iteration, rejected-callee return and
auxiliary-state restoration, not successful allocation or post-creation spatial state.
Twenty-five discriminating cases additionally check the actual callee attempts
for quantities0/1/2/255, source owners2/11 and types15/176. They read RNG state
auxiliary value 123456789 in `0x673934` at every native creation-callee entry and prove its
restoration afterward. The distinct main state remains `0x12345678` on these
explicit unavailable-type returns. That unchanged-main result must not be
extended to paths that actually call the allocation PRNG.
All Players with quantity0 and resource176 makes eight owner11 creation attempts;
the extra neutral/unused player slots are rejected before the resource redirect.

Thirty-two further cases accept stock nonbuilding type 15 by setting availability
bit 2, then execute the connected original chain
`0x4b64d0 -> 0x4b7020 -> 0x4261f0 -> 0x424900 -> 0x424860`.
The allocator's exact `liveUnitCount == 1700` check rejects creation with error 61.
Before that failure, `0x424900` has already called main PRNG `0x478200` with
source 14. With RNG enabled, every attempt advances main state `0x67393c` using
`s = (s * 0x015a4e35 + 1) mod 2^32` and increments both the total draw count and
source-14 count. The trigger restores auxiliary `0x673934` but **does not restore
the main RNG state or draw counts**. With RNG disabled, the actual PRNG returns
zero without advancing them. Quantity 0 makes one attempt; 1, 2 and 255 make
their respective number of attempts. Both Create action IDs still return success
after these failures.

Those cases also execute `0x425020 -> 0x43f0c0 -> 0x4c13b0` and the actual
notification guard `0x46ae50`. A private zero-count TBL uses the native fallback
string. Either an owner different from the local player or mismatched local
indices causes the real notification wrapper to return before presentation.
The error word is cleared, the count remains 1700, the free-unit head remains
zero, and count-cache state remains unchanged. There are no notification stubs
or imported/UI calls. This closes accepted-type allocation failure, not successful
allocation. [Creation/lifecycle research](native-training-lifecycle.md) owns
positive allocation, sprite/image pools and normal hidden-unit placement;
the successful `0x4261f0` continuation and connected property application remain
dependencies.

**Move `0x4b5840`** requires a nonzero source low byte and nonzero destination
dword. The destination is subsequently narrowed to its low byte and
decremented. Zero quantity becomes999999 in `0x6c4c0c`. The spatial callback
`0x4b7b20` checks elevation, death, owner selection, landing/liftoff orders,
Nuclear Missile14, Dark Swarm202, mineral/geyser types, the resource-container
DAT flag0x2000 and trap/door types203..213 (`0x425ca0`; corrected after the
function atlas, [P40 `0x4b7b20`](atlas/p40.md); Flag is type215),
then individual/group type matching. It dispatches to `0x4b7a80` for status
bit2 clear or `0x4b7810` for bit2 set. Both eventually require placement and
spatial mutation. This batch closes 18 nonempty rejections and cache
invalidation; accepted relocation is coordinated with the normal hidden-unit
placement research, rather than approximated by assigning coordinates.

**Give `0x4b5a50`** uses source location byte, source selector, u16 type,
destination selector dword and quantity byte. Zero quantity becomes65535 in a
u16 callback field. Callback `0x4b7750` applies elevation, owner and type/group
selection, then calls `0x4b81a0`. That recipient helper resolves destination13
to Current Player, requires destination<12, and rejects hallucinations,
resource containers, child types73/85, DAT addon bit2 and an already-equal
owner. The callback decrements the remaining word only when that helper
returns success. Neither wrapper explicitly invalidates the count cache;
accepted helper/cache effects cannot be concluded from that absence. Twelve
nonempty cases close hallucination, resource and same-owner rejection. Actual
ownership transfer at `0x428df0 -> 0x426ed0`, player-list/counter/supply changes
and ordinary order reset are extended in the separate
[native ownership batch](native-trigger-ownership.md). Positive linked
addon/cargo/hangar transfer and nonnull AI reassignment remain
explicit dependencies.

**Order `0x4b65a0`** requires nonzero source low byte and destination dword,
computes the destination center from the full dword index, and visits the
source spatial set. Callback `0x4b75d0` applies elevation and selector/type
predicates, rejects building Land/Liftoff, units without status bit0x20000,
Nuclear Missile14, hangar children73/85 and controllers3/4. Modifier0 selects
Move6;1 selects Patrol152;2 selects Attack Move14; every other byte selects
Nothing23. For an ordinary unit it calls the actual general order gate
`0x407bb0` and, on exactly1, issues through `0x42a340`.

Thirty cases use stock units.dat and orders.dat plus original requirement
recipes, the actual lazy requirement-offset initializer/evaluator and real
private order pools. They prove accepted Move/Patrol/Attack Move issuance and
selector exclusions for types0/15. They preserve the count arrays and cache
validity. Successful Attack Move issuance does not prove that a unit can attack
or that combat occurs. The status-bit0x10 unburrow/queued-order branch and
nonnull guard-AI position updates remain separate transitions. See
[general DAT requirements](native-dat-requirements.md).

## Alliance mutation and target cancellation

Set Alliance57 reads its mode from the **u16 unit field at action+0x18** and
selector from dword+0x10. `0x4b6e90` rejects selectors>=27. Its selector jump
table maps Current13 and unused22..25 to a no-op. Direct self-targets are also
no-ops. Mode1 writes neutral1; mode2 writes allied2; every other u16 writes
enemy0. The relation matrix is `0x534914+12*from+to`.

After a nonzero relation write, if the chosen relation differs from the
current player's self-relation, the helper changes **every already-nonzero
entry in that row**, including self, to the new value. Zero entries remain
zero. Player groups are evaluated sequentially against that changing matrix.
For example, changing Allies to Neutral can change later group membership
during the same action. The 1,260 native cases compare the entire144-byte matrix
against an independent sequential model over ordinary, sparse and alternate
player iteration, rather than a frozen target list.

The wrapper then calls `0x4721c0` on Current's actual unit list. For a nonnull
primary target, it checks orders.dat byte `0x51ae40+current_order`; when that
byte is nonzero and Current's relation to the target owner is nonzero, it
invokes `0x420950`. That helper calls actual enemy test `0x4290e0` and clears
the target pointer if the target is no longer hostile. Owner11 uses its
sprite allegiance byte in the enemy test. Thirty native cases cover target
owners0/1/2/7/11, all three relations and the order flag. Nine additional cases
independently vary the owner11 matrix relation and sprite allegiance relation:
the sweep first requires a nonzero owner11 relation, then the callee separately
tests sprite allegiance. Both must be non-hostile to clear the target. This is target-pointer
cancellation, not a blanket primary-order reset.

If Current equals local player `0x508d50`, the wrapper calls `0x41d7b0` to
refresh color classification at `0x52904a`. With empty selection lists, all
its actual callees return without imports; local is color0, enemies2 and
non-hostile others1. Nonempty selected-unit image/color redraw and team-mode
selection rebuilding need their actual presentation callees.

## Pending results, commit and timers

Victory1 sets pending3; Defeat2 sets pending2; Draw56 sets pending5 in byte
`0x6ca758+Current`. Setter `0x4bd900` writes only runtime controllers1/2.
Victory always returns1. Defeat and Draw first read the pending byte and
return0 for pending3 or4 without changing the pending byte or delay.
The actual action VM at `0x4bdf76` suspends on zero and retains its action
index; nine cases prove this interaction. A successful wrapper sets the
u16 UI delay at `0x6ca74c` to one when byte `0x6ca740` is nonzero, otherwise45.
This does not directly write committed finished-player bytes at `0x5349e0`.

Resolver `0x4be020`, called after a trigger cycle, applies local cheat bits
when `0x672664==0`: bit128 forces pending3, otherwise bit64 forces2; bit256
then cancels a pending value>=2. If any active player has pending>=3, it
assigns defeat2 to active players still at zero and resets the local delay
to45 when this newly affects local.

The eight bytes at `0x6ca788` preserve the **initial participant mask**.
Initializer instructions `0x4be2b0..0x4be2e2` clear it and set one for each
player returned by the native iterator; the save/load routines also preserve
it. The resolver counts flagged participants and flagged pending values>=3.
When the counts match, it sets every flagged participant's pending result to5
(Draw), including when that initial participant is no longer active.
Only still-zero committed finished-player bytes receive nonzero pending
results. A newly committed defeat calls the notification helper and native
cleanup `0x4bd7a0`.

The 336 complete resolver cases use controllers3 and empty unit/trigger lists,
so the real notification helper takes its empty-text path. Game-mode byte
`0x672945=4` selects its actual death-order cleanup branch; no human defeat
text, owner transfer, trigger-node free/import or dialog display is executed.
Forty-eight separate complete cleanup cases prove controller1->11 and2->10;
team mode retires matching-force controllers as well. Human notification
presentation and other game-mode unit disposition remain unresolved callees,
not successful fixture stubs.

Timer `0x4be340` does nothing when replay global `0x67cc90` is nonzero.
Otherwise it post-decrements the u16 delay and checks its **old** value.
Old zero reloads45 and selects Defeat UI for committed2, or Victory UI for
committed3/5. Therefore a freshly set delay1 expires on call2, and45 on call46.
Thirty-two cases execute the real timer and original already-active-UI return
guards; they do not open a dialog or advance a campaign.

## Source comparisons and remaining dependencies

Action names and parameter families are correlated against the pinned
[BWAPI trigger schema](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/Triggers.h).
The native instructions and cases above establish 1.17 behavior. Pinned
[OpenBW action execution](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L19041)
is useful structural reference but differs: its Defeat unconditionally returns
true after writing2, its Create44 loops zero times for quantity0 and has no
matching temporary auxiliary-state wrapper, and its Remove25 implementation does not
consume the quantity limit. Several action IDs, including this native alliance
wrapper, are absent from that switch. These source paths cannot replace the
reviewed native contracts.

Required remaining transitions are accepted Create allocation/placement and
property application in one connected call chain; initially visible Remove
cleanup and complete Death destruction/counter/pool updates; carried-powerup
and AI-record cleanup; accepted Move placement/spatial insertion; accepted Give
ownership/list/counter/supply/linked-unit changes; burrowed Order and guard-AI
updates; human outcome presentation; result UI callbacks, Set Next Scenario,
campaign unlock/save/menu progression and full mission playthroughs. This batch
narrows R30/R32 but does not certify either requirement as completely closed.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_trigger_unit_actions_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --static-out docs/research/evidence/native-trigger-unit-actions-static.json \
  --execute-original-code \
  --out docs/research/evidence/native-trigger-unit-actions-probes.json
```

Omit the execution flag for static inspection only. The output records its own
script SHA-256 and the exact SHA-256 of three imported research modules, original
EXE/effective DAT/manifest provenance, runtime architecture/Python/libraries,
instruction allowlist and per-case original calls. General extraction and
runtime setup are in [reproduction.md](reproduction.md). Retail bytes remain
in local scratch; no retail campaign or asset is bundled by this report.
