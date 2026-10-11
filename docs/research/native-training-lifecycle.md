# Native 1.17 Train execution, creation and completion lifecycle

This report follows an accepted production queue into secondary Train, build progress, cancellation, rally orders and completed-unit AI assignment in the reviewed **StarCraft 1.17.0.1** executable. The target SHA-256 is `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. All addresses are local preferred VAs at image base `0x400000`; subtract that base to obtain RVAs. Older reconstruction addresses are not used as local evidence.

The [static witnesses](evidence/native-training-lifecycle-static.json) contain **59 blocks and 2,558 decoded instructions**, with no undecoded tails. The [execution evidence](evidence/native-training-lifecycle-probes.json) contains **539 passing cases: 534 complete returns and five explicit prefixes**. Every executed callee is original code; there are no helper stubs. The private x86-32 guest runs under Unicorn 2.1.4 in an x86_64 process under Rosetta. It executes no game loop, WinMain, Windows API, import, host-file or network operation.

The complete fixture paths establish the specified arithmetic, pool/list changes and order effects. **They do not close successful creation or the normal hidden trained-unit completion path.** Normal completion stops before spatial placement; complete completion cases deliberately use an already-visible child. Animation fanout uses an empty image list, with valid main-image pointers. Local-viewer sound/UI paths are excluded by an explicit owner3/viewer0 fixture. These limits remain distinct from a campaign-level result.

Queue admission, payment and spending-request removal are covered in [native-training.md](native-training.md). Actual primary-order issuance used by rally is covered in [native-primary-orders.md](native-primary-orders.md). Worker allocation and town fields are also covered in [native-workers.md](native-workers.md); regional node layout is covered in [native-military-ai.md](native-military-ai.md).

[native-unit-creation.md](native-unit-creation.md) now supplies complete selected hidden-child Train paths through actual placement, Show, spatial insertion, generated fog reveal, Observer cloak, AI assignment and rally. It also follows incomplete-child cancellation through actual Death-order issuance. Its explicit image, terrain, sound and destruction limits remain part of that separate evidence batch.

## Entry points and persistent state

| Local entry | Caller contract | Role |
|---|---|---|
| `0x439310` | ECX = unit | Secondary-order dispatcher; order38 selects Train. |
| `0x41b060` | ECX = producer | Train state machine. |
| `0x41bc30` | ECX = producer; stack = wanted ID, feedback flag; callee pops8 | Recheck supply and attempt creation. |
| `0x41b290` | ECX = child | Build HP gain, including cheat multiplier. |
| `0x41a280` | ECX = child; stack = HP gain, check-space flag; pops8 | Increase HP, decrement timer or finish. |
| `0x424900` | ECX = wanted ID; EDX = signed x; stack = signed y, owner; pops8 | Create parent and optional subunit. |
| `0x424860` | ECX = ID; EDX = x; stack = y, owner, direction; pops12 | Unit cap/bounds/pool/initialization. |
| `0x41b810` | ECX = producer; stack = relative queue position; pops4 | Refund/cancel and compact queue. |
| `0x48d320` | ECX = producer; EDX = finished child | Completed-unit AI assignment. |
| `0x41bd70` | ECX = child; stack = producer; pops4 | Rally Follow or Move. |

Important CUnit fields are queue words `+0x98..+0xa0`, head byte `+0xa4`, secondary order byte `+0xa6`, HP gain word `+0xa8`, shield gain word `+0xaa`, remaining build time word `+0xac`, status dword `+0xdc`, secondary state byte `+0xe2`, current child pointer `+0xec`, rally x/y words `+0xf8/+0xfa`, rally target pointer `+0xfc`, and AI pointer `+0x134`. ID228 is the empty queue sentinel. BuildingAI stores five tag bytes at `+9` and five context dwords at `+0x18`.

The dispatcher has separate hallucination order149 handling before its ordinary disable gates. The Train handler itself rejects status `0x400`, nonzero lockdown `+0x117`, stasis `+0x119`, or maelstrom `+0x124`. It also rejects a Zerg producer except ID130, the Infested Command Center. Its race helper `0x428d60` tests group bit1, then bit4, then bit2, producing race0/2/1; an unclassified group produces4. This precedence differs from the supply and request race predicates in the admission report.

## Train state transitions and payment timing

States0 and1 read the queue at the current head. If it is empty, Train issues secondary Nothing23 through `0x42a1e0`, clearing the secondary state/current child as that helper specifies, then requests WorkingToIdle animation20.

For a nonempty head, `0x41bc30` requires GroundedBuilding status2, or producer ID in `{72,82,83,81,36}`. It invokes the actual supply check `0x4084d0`, with feedback enabled only for state0. The spawn point is the producer's sprite x/y, sign-extended from 16-bit coordinates. It calls `0x424900`. Failure stores child0 and state1; success stores the child, sets state2, then requests Working animation19.

**This attempt does not debit minerals or gas again.** The initial queue helper already paid in the accepted ordinary path. Complete retry fixtures cover incapable producers, insufficient supply, exact unit cap, exhausted unit pool and out-of-bounds creation, from both states0 and1. The queue ID/head and current stocks stay unchanged. Creation errors pass through `0x425020`, which looks up the error, invokes the actual nonlocal notification early return, then clears error word `0x5da398`.

State2 with a null current child sets actual UI dirty flags, clears the current child and returns to state0; it does not advance the queue head. With a child it calls `0x41b290`, truncates that result to AX, and passes the zero-extended 16-bit gain to `0x41a280(child, gain, 1)`.

If progress returns success while the child is incomplete, state2 continues. A completed child receives AI through `0x48d320`, then nuclear missile ID14 is hidden through `0x422ba0`; other children use rally. Train clears the current slot's AI metadata through `0x484f70`, writes ID228 into that slot, advances the byte head modulo5, marks UI dirty, clears current child and sets state0. **Successful completion rotates the head; it does not compact the five words.** The next secondary invocation attempts the next head.

If progress returns0, Train cancels relative slot0 through `0x41b810`, then clears its current-child/state fields. The failed placement branch and complete removal/animation effects of cancellation are not closed by a successful ordinary creation fixture here.

## Build HP and shield arithmetic

Initializer `0x427d90`, ECX = unit, reads DAT max HP dword `0x520708[ID]` and DAT build-time word `0x5238b0[ID]`. For ordinary values:

```text
hp_gain = ceil((max_hp_fixed - floor(max_hp_fixed / 10)) / build_time)
hp_gain = low16(hp_gain)
if build_time == 0 or hp_gain == 0: hp_gain = 1
unit.word[0xa8] = hp_gain
```

The exact numerator is **u32(max_hp_fixed - floor(max_hp_fixed/10) + build_time - 1)** before unsigned division. Addition can wrap for extreme artificial DAT values. Forty-five original calls distinguish zero time, zero HP, division rounding, 16-bit storage and u32 numerator overflow.

Shield initializer `0x427de0` only writes when DAT shields-enabled byte `0x520458[ID]` is nonzero **and GroundedBuilding status2 is set**. Otherwise it leaves both current shields and shield gain untouched. Enabled/grounded behavior is:

```text
maximum = dat_max_shields_u16 * 256
unit.shields = floor(maximum / 10)
shield_gain = low16(floor((maximum - unit.shields) / build_time))
if build_time == 0 or shield_gain == 0: shield_gain = 1
unit.word[0xaa] = shield_gain
```

Shield gain uses floor division; HP gain uses ceiling division. The 140 calls vary enabled/grounded gates, zero/extreme shield values and build times. Unit-type initialization at `0x425a70` sets remaining build time from DAT, replaces zero with1, then calls both gain initializers. DAT time0 therefore gives remaining1 and gain1 through separate rules. This initialization caller's success path is static evidence; the gain helpers themselves return completely in the probes.

`0x41b290` returns `unit.word[0xa8]`, multiplied by **16** when Operation Cwal bit2 at `0x5432fc` is set. The Train caller's later AX truncation matters: gain4096 under Cwal becomes zero at that call site. This is established by complete Train progress calls, separately from the raw multiplier's 32-bit result.

`0x41a280` first calls the real HP setter `0x41bb50` with current HP plus the supplied gain, before testing remaining time. The selected profiles verify the max-HP clamp. If time was nonzero, it subtracts1 normally or16 under Cwal and returns success **even when the new timer is zero**. The Cwal path stores a word, tests its signed sign bit and clamps negative words to0; unusual large synthetic timers distinguish this from a generic nonnegative integer subtraction. Finishing only occurs on a later call that observes time0 at entry, and that call increases HP once more.

If time is already0 and check-space is nonzero, progress calls `0x424db0`: prefinish `0x424f40`, placement `0x426600`, then postfinish `0x424de0` only on placement success. With check-space0 it calls `0x424da0`, which omits placement. Nonbuilding completion then requests completion voice `0x446650`; Terran building completion can instead search/connect an addon through `0x431ae0`/`0x4359c0`, an unexecuted dependency here.

## Creation, pool failures and list ordering

`0x424900` consumes a raw PRNG draw with source14 **before allocation failure checks**; its low byte is the direction used for the parent and optional subunit. See [native-prng.md](native-prng.md). The allocation helper checks:

1. Unit count `0x665b38 == 1700`, exactly: return0 and set error61. The check is equality, not unsigned greater-or-equal.
2. `0x424d30` bounds using signed 16-bit DAT extents at `0x522c38 + ID*8`: `x-left >= 0`, `y-up >= 0`, `x+right < map_width`, `y+down < map_height`.
3. Free-unit head `0x665af0` must exist; failure sets error62.
4. Actual initialization `0x425710` must succeed. Only success increments the count and removes the unit from the free list through `0x4119a0`.

The eight bound cases distinguish equality at left/top and the strict right/bottom map bounds. The count cases include1699/1700/1701 and empty/nonempty unit pools. A nonempty private unit pool with an empty original sprite pool executes the complete real chain:

```text
424900 -> 424860 -> 425710 -> 425a70 -> 4178e0
       -> 40c2b0 -> 4183b0 -> 4154a0
```

Initialization resets fields through actual SSE/scalar instructions before sprite allocation fails. Probes observe cleared orders/status/AI, `+0xae = 1`, sprite0 and restoration of global initialization context `0x543300`. The unit remains the free-list head and the count does not increase. This is partial initialization followed by a complete failed creation return, not an atomic untouched-record failure.

Three further complete calls provide1/2/3 real free sprites and an exhausted actual image pool `0x544694`. They execute `0x4154a0 -> 0x4162f0 -> 0x4151f0 -> 0x40eba0`, fail image allocation, and restore the sprite through `0x411930`. **That list helper inserts after the surviving head.** For `[A,B,C]`, allocating A then rolling back produces `[B,A,C]`; for `[A,B]` it produces `[B,A]`. This change in future allocation order is part of native failure behavior.

The unexecuted creation success tail uses separate active building/nonbuilding lists, and optional subunit DAT `0x51fb90[ID]` for requested IDs<=105. It connects unit `+0x70`, then performs image/offset work; a subunit failure invokes parent teardown `0x425cd0`. Successful graphics initialization, unit tracking/owner lists, spatial insertion and this rollback are not claimed complete from the failure evidence.

## Cancellation and AI metadata

`0x41b810` rejects unsigned relative offsets>4. For accepted offset r it computes `(head+r)%5`. An empty selected ID is a no-op. If r=0 and current child is nonnull, it invokes `0x41a340`; otherwise it refunds the full selected DAT unit cost through `0x408460`, except for requested types with Building property1.

It shifts up to `4-r` subsequent circular queue entries and their AI metadata through `0x484f90`, stopping at an empty queue word. It writes a final ID228 but **does not clear that final slot's AI tag/context**. Fifty full queue cases cover every head/relative position with and without AI, and additional calls cover sparse holes, building refund exemption, empty slots and unsigned invalid offsets. The head byte does not rotate during cancellation.

Current-child cancellation has a different contract. An already completed child is rejected by the real `0x41a340` gate; the caller can still compact its producer queue. For an ordinary incomplete child, a prefix executes the actual full refund and stops **before `0x42a4d0`**, the remove/death-order dependency. At that point queue/AI compaction has not run and producer current-child still points to the child. In particular, `0x41b810` itself does not clear that pointer; the Train failure caller does so later.

## Rally uses actual primary issuance

`0x429250` accepts producer IDs `{106,111,113,114,130,131,132,133,154,160,167,155}`. `0x41bd70` returns for a null/unsupported producer, rally target equal to producer, or rally x=0. A nonzero x with y=0 is accepted.

For a nonnull same-owner target it calls actual `0x42a380(child, Follow49, target)`, whose issuer samples target sprite coordinates. Otherwise it calls `0x42a340(child, Move6, signed rally_x, signed rally_y)`. Seventy complete calls cover every supported producer plus null/unsupported controls and position/same-owner/other-owner/self/zero-x relations. They use a real private order pool and the complete primary allocation/activation/free chain, not a substituted order setter. Resulting primary order, target coordinates and target pointer are observed directly.

## Completed-unit AI and morph reassignment

`0x48d320` only assigns AI for computer player type1. It first calls `0x482520`, which maintains the player's initial Medic reference. Worker property8 routes to `0x486340`; Overlord42 to `0x486200`; Broodling40 to `0x4b0980`; other units to `0x487990`. Complete new-worker/Overlord profiles use the actual town pools and allocator `0x484cd0`, including pool exhaustion, absent producer AI and all three worker types.

Worker assignment installs WorkerAI type2 from the pool at town-area `+0x5dc0`, adds it to town `+0xc`, sets unit/town pointers at AI `+0x10/+0x14`, initial resource-selection byte `+9=1`, clears unit `+0xc8`, and increments town worker byte `+0x1a`. Overlord uses BuildingAI type3 from area `+0xabe0`, linked at town `+0x14`, with zeroed training metadata. Campaign-mode fixtures deliberately take the native guard-generation skip for this flying unit; noncampaign guard request generation remains a separate contract.

When producer and child are the **same unit**, both worker/Overlord handlers clear relevant gas references through `0x486000`, detach the previous BuildingAI through `0x483b10`, clear matching town main-building references `+0x24/+0x28`, clear unit AI, then allocate its replacement. Four complete Drone/Overlord morph assignment cases cover available/empty replacement pools. Releasing an old BuildingAI makes that node available for immediate reuse by the Overlord path; an exhausted WorkerAI pool leaves the Drone without AI. These are completion reassignment helpers, not the entire Egg/UnitMorph execution state machine.

Ordinary assignment `0x487990` excludes Guardian44/Devourer62 immediately and several merge/morph/special forms later. Hallucinations use current-region military assignment with force1. Otherwise a distinct child's metadata comes from the producer's current head; an equal-type child uses slot0. Tag2's guard context is **dereferenced without a null guard**. With guard parent0, `0x4b25b0` assigns it and copies reserved home coordinates `+0x16/+0x18` to active `+0x12/+0x14`. Ten complete cases establish slot selection and this assignment.

An occupied tag2 guard is converted in producer metadata to tag1 plus the child's actual current-region pointer, then uses military allocation. Other tags use `0x4b2010` guard search/allocation. Complete exhausted-pool cases execute these decisions, including the real region lookup for the occupied-guard fallback. Empty-pool success of the surrounding caller does not mean the unit received AI.

`0x4ae760` pops a MilitaryAI node from region-area `+0x4e20`, links it at region `+0x30`, writes type4, unit `+0xc`, region `+0x10`, and increments region ground-unit count `+0xa` unless unit status4 is flying. It obtains the actual region center, then invokes `0x4a9b20` to issue military movement and apply regional behavior. Two ground/flying prefixes stop exactly before that function after the allocation/list/count writes. They do not validate its movement, random destination or regional reaction.

## Comparison with pinned reconstructions

[GPTP's Train hook](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/unit_making/unit_train.cpp) supplied the state-machine and helper identification pattern for its older executable. [Teippi's unit-build reconstruction](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit_build.cpp) has modified behavior: Cwal speed10, clearing final cancellation metadata, and compacting successful completion instead of rotating the native head. Its selectable executable-edit check for Zerg producers is not present as an optional rule in the reviewed local Train body. These are concrete reasons to follow the local witnesses.

[OpenBW's Train/rally implementation](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L8194) corroborates major disabled/race/retry/rally structure, but its vector queue representation does not prove local circular-slot/AI metadata behavior. [Teippi's AI assignment](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L771) also supplies names; its extra null-guard and list migration behavior must not silently replace the local tag2 contract.

## Reproduction and remaining boundaries

The extractor [inspect_native_training_lifecycle_117.py](../../tools/research/inspect_native_training_lifecycle_117.py) has SHA-256 `6c17063b98c6bb730a3906c7694c191c10228b7b41a971fe15a5bb1d90bc6b61`; the runner [probe_native_training_lifecycle_117.py](../../tools/research/probe_native_training_lifecycle_117.py) has SHA-256 `0ea9364dc4e234edacd092703484759fc113489363d8a960d994cd0040b4fdad`. The JSON records hashes of all five imported inspector dependencies, the exact target, original calls/register arguments, actual/expected outputs, instruction counts and complete-return/prefix distinctions. A 50,000-instruction bound and an instruction allowlist reject unexpected execution. Default probe mode prints a plan and does not execute retail code.

```sh
python3 tools/research/inspect_native_training_lifecycle_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-training-lifecycle-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_training_lifecycle_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-training-lifecycle-probes.json \
  --execute-original-code
```

The five recorded prefix stops are two normal hidden completions before `0x47dc30`, two military assignments before `0x4a9b20`, and one ordinary incomplete-child cancellation before `0x42a4d0`. Exact boundary registers/stack remain in each case. The subsequent [military arrival report](native-military-arrival.md) closes selected completed-unit assignment paths through actual `0x4ae760/0x4a9b20` and primary orders. Further necessary closure is successful sprite/image/iscript initialization and unit registration, hidden-unit placement/show/spatial relocation, subunit creation rollback, complete child removal, subsequent military order/strategic progression, nuclear missile hide, and UnitMorph/merge/race-specific construction execution. The 534 complete calls and static caller structure do not make these dependencies disappear.
