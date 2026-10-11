# Original Nydus exit construction, pairing and travel

**Twenty-one complete cases execute 3,055 original invocations.** A genuine BuildNydusExit46 command creates an exit, writes both peer pointers, advances the stock Zerg construction animation and completes it. Native-created Zerglings and a Drone physically enter the pair through full object callbacks. Original placement failure restores the traveler and retains its queued order; Give and Death consume the actual pair.

The target is StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. Subtract that base for RVAs. This extends [creation and stock images](native-image-initialization.md), [object ordering](native-active-iscript.md), [movement](native-movement-fsm.md), [placement](native-placement.md), [construction](native-construction.md), [transport](native-transport.md), [Bunker](native-bunker.md) and [unit reclamation](native-unit-death.md). Their published sources remain unchanged.

## Evidence and caller inputs

- [Static extractor](../../tools/research/inspect_native_nydus_117.py), SHA256 `bd7f824e9d8bf60b2d1a1ca8d694355f977460138381c889909cad5f1e6d6376`, and [static evidence](evidence/native-nydus-static.json): **9 ranges / 508 decoded instructions**, without undecoded tails. One range is the static-only network-table initializer. The seven-state construction jump table and three main-order adapters are recorded.
- [Original-instruction probe](../../tools/research/probe_native_nydus_117.py), SHA256 `e155fdda32cc16946f77cedf7fbe2599b6a1ea859b8c32e704b7e220fbb0dae7`, and [probe evidence](evidence/native-nydus-probes.json): **21 complete cases / 3,055 checked returns / 16,567,384 original instructions / 380 visited range starts / 1,011 reviewed ranges / 84 authenticated source dependencies**. Every call returns with exact native stack cleanup. There are no prefixes, helper substitutions or executed imports. Independent integer LCG checks match native counters and final state.
- **Separate image provenance:** [native-nydus-assets.json](evidence/native-nydus-assets.json), SHA256 `16ed60804331b8181808506f633524ddad6b5f7749b66eefb32ac01466acdce3`, authenticates **129 image IDs / 113 distinct GRP paths / 137 selected LO inputs**. This includes the new Nydus and Zerg construction/death resources alongside common frozen dependencies. Presence in the manifest does not mean every resource or image branch was executed.
- **Separate string provenance:** [native-nydus-string-assets.json](evidence/native-nydus-string-assets.json), SHA256 `73f51a759ed0aeaaf32e5e3476174994f2399fde2d374f00e56c9cf8c0a6bc40`, authenticates effective `rez\\network.tbl` from Patch_rt.mpq: 5,713 bytes, SHA256 `0a432e596328f1de50a2aaf8b5c0394e13ad4ec1e4bf37dddb74c2e889761733`.

Authenticated **network.tbl is bound at `0x665de0`**, and **stat_txt.tbl separately at `0x6ab4f8`**. Original initializer `0x43f0d0` uses the network filename at `0x4ec2d0`, loader `0x4c13e0` and store `0x43f0f0`. The loader is static evidence; guest execution uses the authenticated private table buffers and a disabled sound/notification backend. Retail payloads remain outside the repository.

Execution uses x86_64 through Rosetta, Unicorn2.1.4, pefile2024.8.26 and Python3.9.6. The bound is 1,000,000 instructions per invocation; the largest completed call uses **292,194**, in the real placement-failure search. Raw stock Units/Flingy/Sprites/Images/Weapons/Orders DATs and IScript are hash checked. Finite normal pools contain 16 CUnits, 64 Sprites, 128 Images, 64 Thingies and 32 Orders. Unit/sprite exhaustion deliberately reduces its corresponding initial pool to one entry, occupied by the entrance.

The terrain is a declared synthetic 4096×4096-pixel, 128×128-tile clear map, uniform region/group1, empty contours, valid eight-player region arrays and explicit alliance/visibility inputs. Both the map rectangle `0x543228=[0,0,4096,4096]` and separate dimensions are initialized. The tile plane is explicitly supplied as **`0x5041f700`**, containing both grid creep masks and the existing-creep removal mask. The separate 128×128 creep-record plane is zero. Actual `0x4b44a0` initializes the 1,024-record creep-life pool. This is a coherent consumer fixture, not recovered campaign creep generation.

Owner3 is human controller2, with null AI pointers. Resource balances start at1000 minerals/1000 gas. The entrance itself is created by actual `0x424900` and completed/shown by `0x424da0` at(200,200). **It is not produced by a paid Drone construction command in this batch.** The exit at(600,400), peer pointers, build timer, HP, completion, images and signals are produced by original BuildExit46 and later object callbacks. Direct public order issuers are used; the GUI/network command admission layer is outside these calls.

Full `0x445070` callbacks receive an externally incremented counter `0x52651c`. Returned Unit/Flingy/Bullet contexts are asserted zero. Empty script/resource/auxiliary lists and countdowns starting1000 close selected scheduler guards. These are complete populated **object callbacks**, including actual motion, unit orders, image/IScript and removal services, without outer accepted-frame, trigger, command-receiver or renderer execution. Selected direct Enter47/travel/cancel/Give calls are identified separately in each case.

## Native entry contracts

| Entry | Original contract and role |
|---|---|
| `0x43bc50` | ECX=entrance; BuildExit46 consumer. Calls placement and allocation, initializes the exit and symmetrically pairs `+0xd0`, then tail-calls order completion. |
| `0x43b6e0` | ECX=building; ZergBuildSelf45 state machine. This batch executes the nonmorph Nydus path, including actual completion. Other building-morph paths remain static here. |
| `0x41a540` | ECX=building; construction timer service. Returns1 only when WORD `+0xac` is already zero on entry; a decrement to zero returns0. |
| `0x41b900 → 0x41bb50` | ECX=building, stack=[raw HP increment], `ret4`; adds current HP and invokes original assignment/clamping and strength refresh. |
| `0x43c320` | ECX=type; returns1 for `133/132/137/144/146`, zero otherwise. Nydus134 selects ordinary completion rather than the building-morph accounting path. |
| `0x43ccc0` | ECX=traveler; EnterNydus47 eligibility, movement and effect dispatch. |
| `0x43d290` | ECX=traveler, stack=[entrance], `ret4`; actual hide, relocation, placement, rollback/show or successful order activation. EAX is not a documented Boolean result. |
| `0x4b47f0` | ECX=dying Zerg building; creep-area removal scheduling. The supplied existing-creep mask closes the scan without allocating receding records. |
| `0x41a340` | ECX=unfinished building; original cancellation frontend. A nonnull Nydus peer rejects cancellation before refunds or Death. |
| `0x4b81a0` | stack=[subject,new owner], `ret8`; actual Give frontend, including the Nydus peer transfer. |

The actual main dispatcher routes immediate order45 through adapter `0x439507`, throttled order46 through `0x439895`, and order47 through `0x43979d`. These call `0x43b6e0`, `0x43bc50` and `0x43ccc0` respectively.

## Exit placement, creation and pairing

Stock Nydus134 has Flingy29, Sprite174, Image86/IScript51, construction Image104, 250 HP, build time600, mineral cost150 and DAT flags `0x05030081`. Peer storage is **CUnit `+0xd0`**. Its idle/return order is23 and it provides no transport capacity.

BuildExit46 converts the requested center into placement tile coordinates using stock footprint half-dimensions and signed integer division truncated toward zero. It invokes original `0x412f10` with type134, output entry0, **vision mode0**, include-unseen1 and ignore-visibility0. Because the builder is Nydus, `0x412720` is selected before the ordinary RequiresCreep branch. For this caller, the passing creep mask is **`0x00400000`**. Supplying only `0x40000000` with the other declared flags still rejects. The human owner mask `1<<(player+8)` takes priority and rejects. These are complete BuildExit consumer failures, beyond the previously recovered isolated grid rules.

On placement success, the original creates unit134 at the requested center and runs `0x423330(exit,[1])` to replace its images with construction resources. It sets raw HP to **`floor(stock_max_HP/10)=6,400`**, issues ZergBuildSelf45, selects blank buttons228 and calls `0x4b4dd0` to cancel receding-life records in the footprint. It then writes **entrance `+0xd0=exit` and exit `+0xd0=entrance`** itself. No host-written relation simulates pairing.

The consumer contains **no resource debit**. All successful and rejected/exhausted cases retain1000/1000 balances. The stock150-mineral cost belongs to entrance production, which remains outside this batch. This distinction matters for accounting and interruption.

Placement rejection leaves one unpaired entrance and returns it to23. Genuine empty CUnit or Sprite pools execute actual allocation failure/rollback and correct network notification, again retaining one entrance and no peer. The failed allocation attempt consumes a second source14 RNG call; a placement rejection never makes that attempt and retains the single entrance-creation call. The live allocation count and original free pools are checked.

## Actual Zerg construction and IScript completion

The created exit starts at **25 HP, time600, HP increment96, order45/state0, buttons228**, with genuine Images104/106. The original state machine uses strict thresholds and its real VM signals:

| Returned object callback | Exit state and effect |
|---:|---|
| 0 | Time600, raw HP6,400, state0. |
| 152 | Time448, HP20,992; state0 crosses the strict three-quarter threshold and becomes1. |
| 153 | Plays animation13, becomes2, time447. |
| 302 | Time298, HP35,392; strict half-time threshold selects3. |
| 303 | Plays animation14 and becomes4. |
| 600 | Timer reaches0; state remains4. |
| 601 | Existing zero timer selects5. |
| 602 | Plays animation15, becomes6 and calls actual completion-voice service. |
| 607 | Stock construction IScript produces **order signal4**. |
| 608 | The order consumes that signal, completes/shows the building and returns to23. |

Normal HP progresses by96 raw units while constructing, with original max-HP clamping. The final state has250 HP, time0, completed status, buttons134 and Images108/86/87. The temporary Image108 is an original completion effect, not a replacement renderer artifact. No timer, signal, HP or completion bit is assigned by the host.

A separate explicit original **Operation CWAL bit2** input subtracts16 from the timer per service, clamps negative values to zero and shifts the raw HP increment left4. Full object/VM construction completes on callback46. The cheat bit is reset after the built pair for selected eligibility/owner/death fixtures; the normal physical travel cases use ordinary608-callback construction. This exercises the original effect of the supplied cheat state, not cheat-text command parsing.

At completion, `0x43c220(134,x,[y])` calls `0x4c0790(type,1)`, obtains0, sees DAT RequiresCreep `0x20000` and returns before the generic footprint-spread path. Thus this Nydus completion does not need a substituted CRT random function or Windows context import. It does not prove positive Hatchery/Tumor-style creep production.

## Enter47 eligibility and physical traversal

The complete order requires a nonnull target, completed target, nonnull completed peer, matching traveler/target owner, no Air status4 and actual race0 (Zerg). It then follows the target through `0x43a0a0`. State1 evaluates original movement completion `0x41a250`: zero waits; one advances to state2 and calls actual travel; other results finish the order.

Six direct complete consumer cases reject a real Marine, real Overlord, enemy-owner Zergling, unfinished peer, unfinished entry and completed unpaired entry. Positions remain(300,240), state returns0 and no loaded/hidden status is created. The Overlord resumes stock idle93; Marine/Zergling resume2. This uses actual stock ReturnToIdle data rather than a universal hardcoded idle. The incomplete entry and peer are genuine newly built exits, with no injected completion or pair fields.

Three **whole-object** cases issue actual unit-target47 and let native motion, order throttling and effect execute:

| Traveler and direction | Start | Teleport callback | Chosen exit position | Immediate resumed order |
|---|---|---:|---|---|
| Zergling37, entrance→exit | (300,240) | 18 | (568,440) | Stock Guard2. |
| Drone41, entrance→exit | (300,240) | 28 | (576,448) | Queued Move6. |
| Zergling37, exit→entrance | (700,440) | 18 | (168,240) | Stock Guard2. |

The Drone's Move is inserted by the real queue allocator/append function before travel. Successful travel activates and removes that queued node. The host never writes movement, target heading, speed, hidden flags or final position. The chosen points include stock extents, real spatial borders and native placement; they are fixture-specific results, not universal offsets from a Nydus center. Count remains3 and both peer links remain intact.

## Hide, placement, rollback and retry

The actual effect saves the traveler's original **Sprite** center and its peer's Sprite center, runs `0x430390([1])`, relocates to the peer via `0x43a270`, then invokes `0x47dc30`. Its null rectangle supplies the native default **center−128 through center+127** placement search, clipped to native map bounds, including the bottom32-pixel exclusion. It checks real unit rectangles and ground placement; a remake must preserve the underlying search order rather than arbitrarily picking a nearby clear point.

On success, original sound19 is requested at the entry, the chosen point is assigned, `0x430430` restores visibility/spatial membership, sound19 is requested at the peer, and `0x42a5a0` activates the next/default order. The disabled sound backend is reached normally; no sound helper is replaced.

The complete **direct-effect** failure case uses the actual constructed pair and an actual Enter47 order with a pending Move6. The entire tile plane is explicitly changed to unwalkable zeros. The native search returns failure; relocation and Show restore original(300,240), fixed coordinates and visibility, while order47/state0, target and the same queued node remain. Restoring the declared walkability plane and retrying the original effect succeeds and activates Move6. This case proves the effect's rollback/queue contract, not a naturally arrived state2 failure or a campaign terrain-change producer. The separate positive cases close full physical Enter47.

Nydus travel uses no packed transport slot, cargo capacity or connected-passenger link. Transport boarding/unloading contracts must not be reused as its state representation.

## Cancellation, owner transfer and pair destruction

Original `0x41a340` observes a genuinely paired, unfinished exit and returns unchanged: order45, time600, 25 HP, peer, images and1000 minerals are retained. At `0x41a382..0x41a38e`, type134 with nonnull `+0xd0` rejects before the race/refund branch. The free exit is therefore not a way to receive a three-quarter refund through this consumer.

Actual Give `0x4b81a0([entrance,4])` transfers both completed pair members. Original `0x4b82a0..0x4b82ab` passes the peer to `0x428df0`. Both owners become4, the owner3 list becomes empty, and live/completed Nydus counters change **[2,0]→[0,2]**. Peer pointers remain symmetric. No owner, counter or list field is changed by the host.

Actual entrance Death followed by full object callbacks destroys both pair members whether the exit is completed or still at25 HP/time600. The type-specific `0x4254ae..0x4254cd` clears both peer pointers before issuing peer Death. Both become Death0 on the first callback, and original image termination/pending recycling reclaims both by callback24. The final allocated count is0, live/completed counters are0, pending list is empty and all16 CUnit slots are free.

This path reaches `0x4251c0 → 0x4b47f0`. Its real creep-area scan executes the stock bounds/group/ellipse logic, but the supplied `0x10000000` existing-creep mask skips receding allocation. That closes the selected Death dependency without claiming positive receding-record production or disappearance scheduling. Genuine Zerg death Image60 and both Nydus/construction resources execute their stock VM termination; no host recycler call stands in for scheduling.

## Reconstruction comparison and remaining contracts

Pinned [GPTP Enter Nydus](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/enter_nydus.cpp) supplies the same broad eligibility/movement/effect structure. [OpenBW Nydus orders](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L6703) also create an exit at one-tenth HP and restore the previous position after placement failure. OpenBW's helper explicitly tests Nydus target type; native `0x43ccc0` relies on its caller-provided target/peer contract and does not add that type test in this body. This static distinction is not a claim that ordinary command admission accepts arbitrary fake pairs. [Teippi cancellation](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L2131) rejects paired Nydus cancellation; the actual1.17 call confirms that selected rule. No1.16.1 addresses are transferred without local matching.

This batch closes selected human Nydus exit creation, stock construction/VM completion, symmetric pairing, both travel directions, physical Zergling/Drone approach, queued/default resumption, complete rejection and direct placement rollback, finite allocation failures, cancellation guard, Give and both completion states at Death/recycling. Remaining work includes paid Drone entrance production, command admission, computer/nonnull-AI Nydus decisions, naturally arrived state2 placement failure/interruption, other traveler footprints/statuses, positive creep producer/recession interactions and campaign-map compositions. The [mission UNIT loader](native-mission-units.md) has a separate relation-record pairing prefix that ends before Storm free; this batch instead obtains pairing from complete runtime BuildExit46. None of these selected calls proves renderer output, outer-frame gameplay or six-campaign parity.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_nydus_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-nydus-static.json

PYTHONDONTWRITEBYTECODE=1 arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_nydus_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-nydus-assets \
  --asset-manifest docs/research/evidence/native-nydus-assets.json \
  --string-manifest docs/research/evidence/native-nydus-string-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --execute-original-code \
  --out docs/research/evidence/native-nydus-probes.json
```
