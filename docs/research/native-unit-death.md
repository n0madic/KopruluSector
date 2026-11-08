# Native visible Death, IScript end and object reclamation (1.17)

This batch follows actual stock unit creation through visible Death, original IScript termination, image/sprite return and final CUnit recycling. It also connects hidden Train cancellation to those consumers and verifies the independent lifetime of Marine corpses and hallucination smoke. It builds on [image initialization](native-image-initialization.md), [populated Train and pending recycling](native-populated-lifecycle.md), [hidden-child completion](native-unit-creation.md) and [primary orders](native-primary-orders.md), without changing their published scripts.

The target is the local StarCraft **1.17.0.1** PE32 executable, ImageBase `0x400000`, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. All addresses below are preferred VAs for that exact image; subtract `0x400000` for RVAs. No 1.16.1 address was reused without a local instruction/data witness.

## Evidence and execution contracts

The [static witness](evidence/native-unit-death-static.json) contains **21 reviewed blocks and 1,383 decoded instructions**, with zero undecoded tails. The [probe evidence](evidence/native-unit-death-probes.json) contains **114 passing complete cases, zero prefix cases and zero helper stubs**. It records the executable/probe hashes, recursively imported local source hashes, stock DAT and asset hashes, original callees, VM opcode PCs, complete returns with exact stack cleanup, state and assertions. The tools are [the static inspector](../../tools/research/inspect_native_unit_death_117.py), [the execution probe](../../tools/research/probe_native_unit_death_117.py) and the unchanged [private asset reader](../../tools/research/inspect_native_image_assets_117.py).

| Cases | Complete original-call profile | Established result |
|---:|---|---|
| 28 | Seven stock types × visibility 0/255 × zero/two queued orders | Actual creation/completion, Death issuance/consumer, stock animation, image/sprite/CUnit return; populated Goliath/Tank turret transfer. |
| 14 | Seven types × initially hidden/forced Hide | Immediate sprite destruction followed by pending CUnit recycling, with no corpse/explosion animation. |
| 8 | Selected actual finite image/sprite/Thingy pools | Optional effect allocation failures and reuse of a released Marine shadow image. |
| 28 | Seven types × hallucination/self-destruct × empty/populated Thingy pool | Hallucination smoke success/failure and the precedence of self-destruct over hallucination. |
| 1 | Marine with two images and an additional actual corpse allocation | The released shadow is consumed before Marine's corpse attempt; original image allocation fails and rolls the attempted sprite/Thingy back. |
| 35 | Seven actual Train children × all five queue heads | Full-price cancellation, hidden Death consumption, subsequent producer Train cleanup and complete pending retirement. |

The execution runtime is Unicorn 2.1.4 in x86_64 mode, with pefile2024.8.26 and capstone5.0.6. Original instructions and real callees execute only inside reviewed ranges. Windows imports, WinMain, the rendering/audio backend, file loading and the whole-game loop do not execute.

Units/Flingy/Sprites/Images DAT and orders.dat use stock retail bytes and native descriptor destinations. The raw stock IScript SHA-256 is `cd35e04eaa8ea95cbbccc980bfaf4cae236f4b18b620dfb4a765822ec32c1cd0`. The [Death asset manifest](evidence/native-unit-death-assets.json) contains 22 selected image IDs and19 distinct GRP paths, including corpse241, small explosion332, large explosion333 and hallucination smoke557. Raw retail bytes stay in private scratch files. Original GRP geometry, linked images, VM execution and dirty-rectangle/native memset effects are covered; pixel decoding/drawing and a rendered frame are separate boundaries.

Owner3, viewer0, controller2, finite private pools, flat walkable terrain, one region, empty terrain edges, selected fog arrays and zero UI placement/selection gates are explicit fixtures. Ordinary created children have real stock populated images. Each visible fixture actually executes `0x424900` then `0x424da0`; it does not synthesize a visible CUnit. The forced-Hide profiles add user-action bit4 explicitly. Hallucination profiles add status `0x40000000` after ordinary creation, optionally also `0x80000000`; they prove those consumer branches, not the Cast Hallucination creator or its accounting initialization.

Death issuance executes actual `0x42a4d0`. The primary dispatcher `0x439420` is then invoked with its original ambient unit/IScript context globals `0x543300/0x543304` initialized to the unit, matching the original object-step caller. Those globals are cleared before subsequent pending steps. Each selected update executes complete `0x438ef0` then complete `0x418990`. This relative order is a **static native caller fact**: `0x445070` calls the unit step at `0x4450d2`, bullet/flingy steps in between, then Thingy step at `0x4450e1`. The probe omits those intervening systems and the full active-unit dispatch/movement schedule. Its update counts begin **after** the explicit Death consumer call and must not be relabelled as an unmeasured trigger-to-first-death campaign frame count.

After ordinary body removal, active/hidden/special/cloak lists are genuinely empty, while pending CUnits and LoneThingys are nonempty. Cancellation uses a synthetic initialized existing producer with an empty image list; after real child Death and producer Train cleanup, complete native active-list removal `0x427080` excludes that fixture producer from the following pending-only object step. The producer's movement/animation is therefore an explicit caller boundary, rather than a substituted helper result.

## Native Death decision and ABI

| Entry | ABI and selected role |
|---|---|
| `0x42a4d0` | ECX=CUnit, no stack arguments: clear/issue/activate Death using real order pools, drop-powerup and null-AI paths. |
| `0x439420` | ECX=CUnit, no arguments, ambient current-unit context: first dispatch maps order0 through `0x439448` to `0x41e630`. |
| `0x41e630` | ECX=CUnit, no arguments: Death animation versus immediate destruction. |
| `0x415540` | ECX=destination sprite, stack[source sprite], RET4: transfer the subunit's main image. |
| `0x422ba0` | ECX=CUnit: native Hide, spatial/occupancy removal, movement reset, active-to-hidden list transfer and visibility clear. |
| `0x425050` | ECX=CUnit: body accounting/reference/AI/list removal; retain sprite for animation and move CUnit to pending list. |
| `0x425cd0` | ECX=CUnit: destruction including optional actual subunit, body removal and immediate sprite/image release. |

The dispatch destination table is at `0x4399c0`; its byte mapping is at `0x4399d4`. The witness preserves both tables. The Death consumer executes this decision:

```text
if user_action_flags & 4: Hide(unit)
if unit.subunit:
    transfer its main image to the parent sprite
    destroy its CUnit/sprite
    unit.subunit = null
unit.primary_order_state = 1
if sprite is visible:
    if not Hallucination OR SelfDestructing:
        force sprite Death animation1
        remove unit body, leaving its sprite pending
        return
    else:
        attempt hallucination sound and separate smoke Thingy
Hide(unit)
destroy body and sprite immediately
```

A nonnull subunit is destroyed by the native call at `0x41e652` regardless of whether that subunit itself has a nested link. `0x415540` unlinks the source main image, inserts it **before the parent sprite's first image**, changes `image+0x3c` to the parent sprite, clears CanTurn bit8, and nulls the source sprite's main-image pointer. Goliath image235 and Tank image251 then participate in the parent's Death animation; their original turret CUnits and sprites are already pending/freed. This is not a second independently animated turret sprite.

The ordinary Death consumer removes live and completed counters immediately through the actual `0x414590/0x414580` chains: both stock type counters change1→0. Allocated-CUnit count `0x665b38` stays positive until pending recycling. For Goliath/Tank, parent and turret remain two allocated pending CUnits until that step releases them. The probe validates the final complete pool membership, rather than merely comparing list lengths.

## Original stock animation and measured retirement

| Stock unit | Main image and Death PC | Relevant stock script/effect | CUnit retirement update | All effects/pools restored update |
|---|---|---|---:|---:|
| Marine0 | 239, PC29055 | Sound range276..277; facing0; frames221..228, wait2 each; `lowsprul` sprite236/image241; wait1; end. | 17 | 165 |
| SCV7 | 247, PC29477 | Sound369; overlay image332; wait3; end. | 18 | 18 |
| Wraith8 | 243, PC29405 | Sound257; overlay image332; wait3; end. | 18 | 18 |
| Zealot65 | 151, PC11887 | Sound678; facing0; frames221..227, wait2 each; end. | 14 | 14 |
| Vulture2 | 256, PC30415 | Sound353; overlay image332; wait3; end. | 18 | 18 |
| Goliath3 | 234, PC28630 | Sound8; overlay image332; wait3; end; transferred turret235 ends separately. | 18 | 18 |
| Tank5 | 250, PC29922 | Sound317; overlay image333; wait3; end; transferred turret251 ends separately. | 20 | 20 |

The common shadow/turret short Death sequence is wait1 then end at PCs31008/31010. SmallExplosion332 Init starts at6824, draws frames0..8 with wait2 each, then end6869. LargeExplosion333 Init starts at6882, draws frames0..9 with wait2 each, then end6932. These overlays belong to the dying unit's existing sprite. Its main image can end at update3 while the explosion remains, so sprite emptiness and CUnit recycling wait until18/20.

Marine's corpse is a separate native LoneThingy with sprite236 and image241. It is actually created at selected update16; both body and corpse coexist until update17, when the body CUnit/sprite return. Corpse Init PC29342 displays frames0,1,2 with wait50 each and ends at29357. The original Thingy step advances it in the same update in which it was created, so its150 update invocations finish at global selected update165. No extra CUnit is allocated for that corpse.

Wait opcode5 stores `operand-1` into the VM wait byte and returns. On later VM entry, a nonzero byte decrements and returns without reading another opcode; a zero byte resumes at the saved PC. This explains the exact durations and same-update effect advancement. The probe records every encountered opcode PC from real stock bytes; it uses no replacement script interpreter for execution.

## Image, sprite, Thingy and CUnit release

The native opcode22 `end` handler at `0x410b09`, in executing mode0, calls `0x40fe10` and returns after writing the next VM PC. `0x40fe10` marks the old rectangle dirty, unlinks the image from sprite image head/tail at `+0x1c/+0x20`, clears GRP pointer `image+0x2c`, and returns the image through real list insertion to pool head/tail `0x544694/0x592898`. It does not clear the entire record or explicitly null sprite.main `+0x18`.

Sprite image updater `0x417860` saves each next image **before** its update callback and VM invocation. When the linked image list becomes empty, it computes the row with `0x416ab0`, removes the sprite from row lists `0x5c51d0/0x5c55d0`, and returns it to pool `0x5c51c8/0x5c51cc`. Unit image wrapper `0x418b20` then nulls `CUnit+0xc` when the sprite has no images. Pending unit iteration in complete `0x438ef0` calls `0x4256b0` only after the sprite is null and the selected status/creep gate permits it. That recycler clears queued orders, unlinks pending `0x665ae8/0x665aec`, decrements allocated count and returns CUnit to unused `0x665af0/0x665af4`; generation is preserved until later allocation.

LoneThingy creation `0x417b80` accepts ECX=spriteID, EDX=x, stack[y,owner], RET8. It removes a16-byte Thingy from free `0x5c7a20/0x5c7a24`, sets its field+8 to1, calls actual sprite/image/IScript creation `0x4154a0`, and inserts successful records in active `0x5c7a28/0x5c7a2c`. Failed sprite creation returns that Thingy. Overlay-sprite helper `0x410320` has ECX=source Image and stack[spriteID,xoff,yoff,elevation], RET16; it creates the Thingy at image-offset-adjusted coordinates and initializes actual tile visibility with `0x417e50`.

Complete Thingy step `0x418990` saves next pointers and calls `0x418b50`. That consumer updates visibility and the original sprite image list; once empty, it nulls Thingy.sprite and returns the Thingy to its free pool. Its separate FOW-sprite list is empty in these fixtures. The same image/sprite release pipeline thus serves both pending CUnits and independent effects, while the CUnit and Thingy pools remain distinct.

## Pool exhaustion and Hallucination branches

A zero Thingy pool or a one-record sprite pool prevents Marine corpse allocation; the body still retires at17 and all available records return. An exhausted image pool during SCV/Wraith/Vulture/Goliath/Tank Death suppresses the optional explosion overlay; the remaining short Death images end and CUnits recycle at3. These are complete native allocator-failure returns, not a helper forced to return null.

A Marine image pool of two records initially has no free image. Nevertheless its shadow ends at update1, making a record available by the update16 corpse attempt; corpse creation succeeds and all effects retire at165. The separate competing-allocation case uses actual `0x417b80` at update14 to consume that returned shadow record for another stock corpse at300,300. The dying Marine then genuinely fails to allocate its own corpse image at16, and the attempted Sprite/Thingy roll back. Its CUnit still retires at17; the competing corpse expires at163. Thus initial free-image count alone does not determine a later Death effect's allocation result.

With Hallucination set and SelfDestructing clear, the visible consumer attempts sound619, creates sprite374/image557 with **owner0**, sets elevation to parent+1, and copies parent visibility. It then hides and immediately releases the parent sprite; the next pending step returns CUnit at1. Successful smoke Init PC2760 has13 frames with wait3 each and ends at2825; its separate Thingy returns at39. A zero Thingy pool suppresses smoke and all retirement finishes at1. SelfDestructing overrides Hallucination and takes the ordinary stock Death animation, including Marine corpse behavior.

The native sound helpers `0x40f5a0 → 0x446a20` and `0x446940` execute their real `0x6d3140 == 0` backend guard in every fixture. Sound selection can still advance RNG before that guard: Marine's stock `playsndbtwn` uses source5. The probe records all50 source counters and independently validates the final seed against the exact integer32-bit LCG after their sum. Audio playback and enabled-backend/import behavior remain unexecuted.

## Actual populated Train cancellation

For each type/producer pair and all five queue heads, original secondary `0x439310 → 0x41b060 → 0x41bc30 → 0x424900` creates the hidden stock child and any turret with populated images. The synthetic producer begins with prepaid stocks1000/1000. Complete `0x41b810` on relative slot0 invokes actual cancellation `0x41a340`, refunds the **full stock DAT mineral/vespene price**, issues/activates Death, and clears that queue entry. Immediately afterward, producer.current_child still points to the child and Train state remains2; the child's user-action byte is0 after real order activation.

The actual primary Death dispatcher consumes the child's hidden branch, returns all images/sprites without playing visible Death, and leaves CUnit(s) pending. A subsequent complete producer secondary Train call detects the dead child and clears `producer+0xec` and `producer+0xe2`, preserving the original queue-head index. Complete native active-list removal of the synthetic producer then establishes the declared empty active-list profile. The next full pending object step clears any remaining orders and returns the child/turret CUnits; all image/sprite records are restored, no LoneThingy is created and allocated CUnit count reaches0. The evidence distinguishes cancellation issuance, child destruction, producer cleanup and deferred recycling instead of treating the refund as final removal.

## Source comparison, reproduction and remaining boundaries

Pinned sources supplied structural leads. Local native instructions/calls resolve the selected behavior:

- GPTP `orders_Die` describes user-action Hide, turret main-image transfer and hallucination Thingy creation. Its 1.16.1 code places part of subunit destruction under a nested-link condition; native1.17 calls destruction whenever the first subunit exists. Stock native parent/turret execution is authoritative here. [Pinned GPTP source](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/base_orders/die_orders.cpp#L95)
- Teippi `Unit::Order_Die` transfers the main image and clears CanTurn, but its modified hallucination allocation uses the unit player. Native1.17 passes owner0 and then copies visibility; the positive smoke cases prove this distinction. Teippi's modified concurrency/results logic is not transferred. [Pinned Teippi source](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit_death.cpp#L496)
- OpenBW models a retained dead-unit sprite and later queue/pool recycling. It is an independent reimplementation, not automatic original parity. [Pinned OpenBW dead-unit source](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L12800)

Prepare the22 private image resources using the unchanged asset reader and the image list in the manifest. Then reproduce:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_unit_death_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --data /private/tmp/koprulu-campaign-data --manifest docs/research/evidence/retail-manifest.json --assets /private/tmp/koprulu-death-assets --asset-manifest docs/research/evidence/native-unit-death-assets.json --pyms /private/tmp/koprulu-pyms-campaign --out docs/research/evidence/native-unit-death-probes.json --execute-original-code
python3 tools/research/inspect_native_unit_death_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --out docs/research/evidence/native-unit-death-static.json
```

Without the execution flag, the probe prints only its plan. Published evidence uses one full `all` run with its final source hash; restricted development groups are not merged into publication.

Remaining distinct consumers include the full accepted-frame primary/movement schedule, occupied terrain/local-path reactions, loaded cargo/transport death, nonnull worker/building/military AI removal scenes beyond prior dedicated batches, Zerg morph/egg/building-specific Death, selected positive creep retirement `0x4b47f0`, enabled sound and rendered GRP pixels. This batch does close the selected ordinary populated visible/hidden Death, actual Image/Sprite/Thingy/CUnit reclamation and populated cancellation compositions. A specification must preserve their separate lifetimes, saved-next iteration, same-update Thingy advancement, real finite-pool failure behavior and counter timing.
