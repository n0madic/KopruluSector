# Paid Carrier production, Interceptor flight and hangar repair

**Eighteen complete cases pass through14,378 original invocations.** The original Carrier admits and pays for Interceptors, trains them in the hidden-unit list, launches them through the original hangar links, fires with stock IScript and returns them to the hangar. The selected complete object callbacks also reclaim a dead unit and automatically reuse its slot for a previously paid Interceptor order. Capacity, healing and shield thresholds are recovered separately from their scheduling.

The target is StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. Subtract that base for RVAs. This extends [paid training](native-training.md), [requirements](native-requirements.md), [image initialization](native-image-initialization.md), [object ordering](native-active-iscript.md), [projectiles](native-projectiles.md), [pending recycling](native-populated-lifecycle.md) and [physical Scarab delivery](native-subunit-scarab.md). Their published scripts and evidence are unchanged.

## Evidence and declared inputs

- [Static extractor](../../tools/research/inspect_native_carrier_117.py), SHA256 `f5f0a98f3a327d3530570b476e3a9f17819cadb95b20317aa6b137858584a3be`, and [static evidence](evidence/native-carrier-static.json): **17 ranges / 625 decoded instructions**, with no undecoded tails. The parent-death range and network-table binding initializer are recovered statically; their positive initialization/death branches are not executed in this batch.
- [Original-instruction probe](../../tools/research/probe_native_carrier_117.py), SHA256 `eb6455e857659d44febda4b5d0cf6ca5ae7e27cbfef457ac7c8603942e1ccdf3`, and [probe evidence](evidence/native-carrier-probes.json): **18 complete cases /14,378 checked sentinel returns /5,785,930 original instructions /378 visited range starts /879 reviewed ranges /69 authenticated source dependencies**. Every invocation returns through the checked sentinel with exact stack cleanup. There are no prefixes, helper substitutions or executed imports. RNG final state and counters are checked against an independent integer LCG.
- **Separate new asset provenance:** [native-carrier-assets.json](evidence/native-carrier-assets.json), SHA256 `4f296850295bc789225142f4a35b09f837fa801c0a5b5d145dd39da96b20c249`, covers **78 image IDs / 68 distinct GRP paths / 80 selected LO inputs**. The Carrier112–115, Interceptor116–118, Bullet522 and shield-hit424 resources are populated. The shield LO path is `unit\protoss\sshield.los`, SHA256 `2abb7dfd736abdde7af22be78d501d9f58211e722b3c582e8d9ac092bf7783b7`. Private retail payloads are not included in the repository.

- **Separate string provenance:** [native-carrier-string-assets.json](evidence/native-carrier-string-assets.json), SHA256 `73f51a759ed0aeaaf32e5e3476174994f2399fde2d374f00e56c9cf8c0a6bc40`, authenticates effective `rez\network.tbl` from Patch_rt.mpq:5,713 bytes, SHA256 `0a432e596328f1de50a2aaf8b5c0394e13ad4ec1e4bf37dddb74c2e889761733`.

**Corrected table binding:** the original initializer `0x43f0d0` passes path`0x4ec2d0=rez\network.tbl` to loader`0x4c13e0` and writes the result to`0x665de0` at`0x43f0f0`. Failure notification `0x425020 → 0x43f0c0` reads that global. The final fixture binds authenticated network.tbl there and stat_txt.tbl separately at`0x6ab4f8`; it does not run the native loader. The earlier probe hash`7935a8327d17ed0b787a0b9267b617ccdd6b49c3686b45fd13aca9edd0dacca0` incorrectly supplied stat_txt.tbl to the network global and is superseded. All18 cases were rerun together with the corrected input; counts and observed state contracts remained the same.

The engine is x86_64 through Rosetta, Unicorn2.1.4 and pefile2024.8.26. The per-invocation instruction bound is250,000. Original DAT descriptors load authenticated Units/Flingy/Sprites/Images/Weapons/Orders files; stock IScript, GRP, LO and `rez\stat_txt.tbl` supply the actual image and notification consumers. Raw DAT inputs remain distinct from original live acquisition-range initialization.

The normal fixture has16 CUnits,64 Sprites,128 Images,64 Thingies and32 Orders. The two unit-pool-failure cases deliberately restrict the CUnit pool to1 or2. Original creation, completion/show, spatial insertion, lists, child links, animation and damage callees run. No child heading, speed, position, main order, timer or hangar link is authored to simulate flight.

The terrain is a declared clear synthetic4096×4096-pixel,128×128-tile map, uniform region1, empty contours and a finite path record. Both `0x543228=[0,0,4096,4096]` and the separate map dimensions are initialized. Visibility, owner3/4 hostility and eight valid player-region arrays are fixtures. Controllers are human and CUnit AI pointers are null; the reached Interceptor-parent alert radius queries execute their actual spatial callbacks, whose first null-AI rejection closes these selected profiles. Positive computer retaliation is separate.

Initial image readiness uses64 actual image/Bullet/Thingy steps. A passive Marine victim receives real Nothing23 and an explicit order-wait250. Resource stock starts1000 minerals/1000 gas. Upgrade43=1 is an explicit prerequisite fixture in the upgraded capacity case; the hero case obtains its capacity from the authentic Gantrithor type. Requirement availability is explicitly enabled, and the original lazy packed-recipe offset initializer executes.

Complete `0x445070` calls use a supplied external frame counter at `0x52651c`. Current unit/Flingy/Bullet context globals must be zero after every return. AIScript/resource-area/auxiliary lists are empty; creep, slow-service and deferred countdowns start1000, exercising their guarded countdown paths. This is complete selected **object-callback execution**, including populated units, images and projectiles. It excludes the outer accepted-frame, command receiver, trigger, pause/readiness and renderer phases.

## Entry contracts and stock identities

| Entry | Native caller contract |
|---|---|
| `0x4b32a0` | CX=requested unit ID, EDX=player, stack=[producer], `ret4`; complete stock can-make/packed-recipe gate. Carrier73 recipe includes queued and completed fighters. |
| `0x41b4b0` | ECX=producer, stack=[unit ID]; actual paid circular-queue admission. Its caller must first enforce the can-make recipe. |
| `0x439310 → 0x41c630` | Secondary63 dispatcher/TrainFighter execution; original allocation, construction countdown and hangar completion. |
| `0x41cc80` | ECX=parent, stack=[completed fighter], `ret4`; registers the completed inside fighter. |
| `0x41cba0 / 0x41cc50` | Carrier/Reaver capacity and byte-sized total-count helpers, including all five queue slots. |
| `0x41ccd0` | ECX=parent; selects and moves an eligible inside fighter to the outside list, shows it and returns its pointer, or0. |
| `0x41c290` | ECX=Carrier; Carrier50/Fight56/AttackMove57 consumer. Checks range, launches a fighter, refills its shields and issues its stock attack order. |
| `0x41c310` | ECX=Carrier; Attack53 consumer. Validates its target, commands existing outside fighters, queues Fight56 and activates it. |
| `0x41c410` | ECX=Carrier; Stop52 consumer. Stops the parent, commands outside Interceptors to return69 and issues parent50 with its existing target. |
| `0x41c4e0` | ECX=Interceptor; Attack64 shield check, target validation and orbit/firing state machine. |
| `0x41cb20` | ECX=Interceptor; validates/acquires target and checks its parent-dependent range, selecting Return69 when continuation fails. |
| `0x41d100` | ECX=Interceptor, DL=offset scale, stack=[packed target position], `ret4`; consumes two source11 RNG calls, constructs and commits an orbit destination. |
| `0x41c450` | ECX=Interceptor; Return69 consumer. Uses actual `0x417aa0` distance tests to lower elevation and load the fighter, or updates its destination toward the parent. |
| `0x41ce40` | ECX=parent, stack=[fighter], `ret4`; outside-to-inside unlink/relink, count update, outside flag clear and actual hide. |
| `0x41b900` | ECX=unit, stack=[raw HP increment]; original clamped HP addition through `0x41bb50`, including strength refresh. |

Stock Carrier72 and Gantrithor82 use Flingy39/Sprite190/Image112/IScript151. Interceptor73 uses Flingy41/Sprite193/Image116/IScript155,40HP,40 shields and build time300. Its weapon79 uses damage6, normal damage type and BulletImage522. Primary IDs are parent50/53/56/57, **CarrierStop52**, child64 and Return69. Generic Stop8 is not substituted for CarrierStop52.

Parent `+0xc0/+0xc4` are inside/outside heads and byte `+0xc8/+0xc9` are their counts. Fighter `+0xc0` points to the parent, `+0xc4/+0xc8` are its sibling links and byte `+0xcc` distinguishes outside membership. These meanings depend on the unit subtype; they are not uniform CUnit fields for every unit.

## Paid admission and the4/8 capacity boundary

The complete stock Interceptor recipe counts **inside + outside + all queued73/85 entries** and accepts only a total strictly below capacity. Ordinary Carrier72 capacity is4. Carrier Capacity upgrade43 raises it to8; stock Gantrithor82 also has8 through its hero property. The helper uses byte-sized arithmetic, as recovered in the requirements report.

Each capacity case genuinely pays and trains4 or8 children through complete direct secondary calls. The next `0x4b32a0` returns0/error3 without charging resources. A real `0x41ccd0` launch moves one inside child outside; the same recipe still rejects because the total has not changed.

The queued-capacity case passes the recipe and pays25 four times, leaving900 minerals and four queued73 entries. The fifth recipe check rejects. Deliberately calling the lower-level `0x41b4b0` after that rejection nevertheless accepts, charges another25 and produces five queued entries. Preserve the frontend can-make check: the paid enqueue helper alone does not implement the Carrier cap.

## Hidden training and first-damaged repair

Actual paid admission and secondary63 create the hidden Interceptor on the first production call with build-time300. Exactly301 subsequent production calls decrement300 to0 and perform completion on the following call: **302 complete production callbacks in total**. The successful whole-object case runs all302 through `0x445070`; the capacity/repair fixtures run the same original secondary dispatcher directly and do not claim complete world scheduling for those training phases.

When no queue entry remains, the original next secondary call reaches state3. That state follows the inside list and restores **128 raw HP =0.5HP** to the first damaged child, then returns. It does not distribute healing among every inside fighter in one callback. HP clamps through the original HP setter and strength refresh; no mineral payment accompanies repair.

The three-child repair case obtains its reverse-creation inside order from actual links. Real incoming HP-damage calls prepare raw HP `[10176,10176,7680]`. Three complete secondary calls produce `[10240,10176,7680]`, `[10240,10240,7680]`, then `[10240,10240,7808]`. Two subsequent complete object callbacks heal only the remaining damaged child to7936 and8064. Minerals remain925 after the three paid children.

## Launch, shields and actual object-phase thresholds

`0x41ccd0` refuses the selected Interceptor below **20HP**. Raw5119 is refused;5120 and5121 are accepted. These boundary inputs are prepared by the real HP-damage helper, followed by complete direct parent53/56 main calls. No intervening secondary repair masks the predicate.

The main launch consumer sets the accepted child's shields to its authentic maximum40×256, gives it parent elevation−1, issues child64 with the parent's target, and sets parent order timer7. Launch **preserves HP**: the selected child prepared with21HP/5 shields leaves with21HP/40 shields. Inside repair and shield refill are distinct mechanisms.

Interceptor64's first check is:

```text
if parent != NULL and signed_current_shields < (stock_interceptor_max_shields <<8) >>2:
    issue Return69
```

The threshold is **one quarter**,10 shields for stock type73. Direct original calls with raw2559 return69;2560,2561 and5119 remain64. Real native weapon damage prepares those shield values and creates populated Image424 through the authentic shield LO and image pool. No shield field is directly assigned.

The full object callback regenerates **7 raw shield units before the main-order predicate**. After a natural parent launch, preparing2552 produces2559/Return69 on the next object callback; preparing2559 produces2566/Attack64. A standalone predicate test and a complete object callback therefore have different boundary inputs. This is a scheduling contract, not an alternate threshold.

## Natural flight, fire, Stop and return

The complete flight case creates Carrier72 at(200,200), trains its child through302 object callbacks, then originally creates a stationary Marine at(360,200). Only parent Attack53 is issued. Interceptor orders, headings, movement, orbit destinations, Bullet creation and damage all come from original consumers.

| Combat callback / external frame | Native result |
|---|---|
| 1 /303 | Parent53 activates56; child remains inside with Nothing23. |
| 2 /304 | Child launches64; inside0/outside1; elevation15 with parent16. |
| 3 /305 | Child state1/order timer15; real source11 orbit destination selected. |
| 21 /323 | Child state4 at(380,204), elevation17 after the original **+2** transition. |
| 25 /327 | Original Bullet impact reduces Marine40HP to34HP. |
| 26 /328 | After parent Stop52 issuance, parent becomes50 and child69 at(381,152). |
| 44 /346 | Child is actually hidden/inside, Nothing23, at(200,200), elevation14; inside1/outside0. Parent has already reached56 again. |

State0 starts the real launch sound616, chooses a scale3 orbit destination and sets timer15/state1. State1 waits for the timer, then adds2 to elevation and enters3. States3/4/5 use real range,50-pixel proximity tests, firing and further scale1/2 destinations. The destination helper draws each offset in **[-127,128]**, multiplies by the selected scale, mirrors off-map candidates using stock Interceptor bounds and commits the native clamped result. These are original RNG calls, not host-generated orbit coordinates.

Return69 kills a child with null parent. Otherwise its actual proximity helper tests60 pixels to enter state1/lower elevation to parent−2, and10 pixels to load it through `0x41ce40`, hide it, select Nothing23 and refresh UI state. The static caller uses the inclusive helper tests; the selected natural flight closes actual movement/loading without claiming separate exhaustive distance-boundary cases.

**CarrierStop52 preserves the parent's existing target.** Its return to parent50 can subsequently choose Fight56 again. The recorded endpoint is the first successful inside return; it does not claim that Stop permanently disables attacking or that the child cannot relaunch later. Replacing the command with a target-clearing idle assignment would change the observed sequence.

## Pool exhaustion, real death and automatic retry

With a one-CUnit pool occupied by the Carrier, the recipe admits and charges25, but TrainFighter's genuine allocation fails. The producer remains secondary63/state1, current-child pointer0, queued73 retained and minerals975. The allocator sets notification WORD62; the actual failure-notification path resolves the stock string, executes its disabled-backend guard and then clears `0x5da398` to0 before return. An assertion that expects62 at the final caller boundary would misread that lifecycle. Actual visible message rendering is not executed.

The two-CUnit case additionally creates a Marine occupying the only other slot. After the same paid failure, real Death is issued to the Marine. Complete object callbacks perform its image termination, pending-list processing and CUnit recycling. At callback17 the native producer immediately allocates a new Interceptor at the **same recycled address**, with a new generation, and resumes training. At callback318 the child is completed/inside. There is no injected free-pool record and no second payment: minerals remain975 throughout. Final allocated count2 is Carrier + Interceptor, with empty pending and free lists.

This closes the selected natural retry producer, including failure notification and the same-callback recycling-to-allocation transition. It does not close every sprite/image exhaustion, producer destruction, queue cancellation or parent-death branch.

## Reconstruction comparisons and remaining scope

The pinned [Teippi Interceptor implementation](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L3887) uses half maximum shields and elevation+1; its Return rewrite measures to a fixed-point parent center with an extra128/256 and strict distance comparisons. These are reconstruction choices. The authenticated1.17 instructions instead use quarter shields, elevation+2 and the original proximity helper. The pinned [OpenBW Carrier/Interceptor implementation](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L7224) agrees on the quarter/+2 choices and first-damaged128-raw repair, but that agreement is not original execution evidence. [GPTP's1.16.1 Return hook](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/interceptor_return_order.cpp) provided structural leads; none of its addresses were reused without matching the1.17 executable.

The recovered caller boundaries support paid capacity, hidden training, native flight/fire/return, first-damaged repair, shield phase ordering and natural allocation retry. Damage-preparation helpers are complete real callees but their numeric inputs are declared boundary fixtures, not natural incoming-weapon trajectories. Capacity training uses selected direct secondary scheduling; the principal flight and retry use full object callbacks. Parent-death cleanup is static only, and multiple simultaneous outside Interceptors, moving parents/targets, live computer AI, supply availability changes, Carrier command reception, sprite/image exhaustion, transport/Bunker/Nydus and outer campaign execution remain separate.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_carrier_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-carrier-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_carrier_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-carrier-assets \
  --asset-manifest docs/research/evidence/native-carrier-assets.json \
  --string-manifest docs/research/evidence/native-carrier-string-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --execute-original-code \
  --out docs/research/evidence/native-carrier-probes.json
```
