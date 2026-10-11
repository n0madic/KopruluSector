# Paid Zerg construction and the Larva–Egg–Drone producer

This batch connects original Larva spawning and paid Drone production to ordinary Zerg building construction, stock animation completion, cancellation, resource rechecks and Town AI lifetime. **Spawning Pool is unit 142; unit 141 is Spire.** The exact local executable and authenticated retail DAT, IScript, GRP and LO files determine the results. Pinned reconstructions provide structural leads and names; they do not supply transferable addresses or original parity proof.

The target is PE32 x86 version **1.17.0.1**, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Private Unicorn calls execute original instructions and actual callees. No helper substitutions, Windows imports, process startup or renderer execute. Complete object callbacks and complete selected per-unit phases are explicitly distinguished from the outer accepted-frame loop and campaign gameplay.

The fresh `--case-group all` publication passes **15 complete cases, 0 prefixes and 17,440 complete original top-level calls**, executing **119,214,474 original instructions**. The maximum single call executes 706,592 instructions under the 1,000,000 bound. The composed allowlist contains 1,271 reviewed ranges; 470 range starts are visited. These are trace/range counts, not a claim that every function branch is covered. All 95 imported source hashes and both asset manifests were checked fresh. Static extraction contains 43 witnesses and 2,721 completely decoded instructions.

| Frozen artifact | SHA256 |
|---|---|
| Probe | `d12111b18b8043a9f661215361b3fdfab689029733d033150af23fc3e34a55ed` |
| Static inspector | `2d568c5cf188b43e091f28687bc3a29220b49c4ac034730dbe952fc3dbb2ab9c` |
| Image/LO manifest | `6312fadea6d31ae3ae5824abfee18e1d8d87ea20c2df58f50caebbf5956432ad` |
| String manifest | `73f51a759ed0aeaaf32e5e3476174994f2399fde2d374f00e56c9cf8c0a6bc40` |

## Original producer and caller contracts

Addresses below are preferred VAs for this exact image. Static evidence records instruction bytes, RVAs and file offsets. No GPTP 1.16.1 address is reused as a local anchor.

| Local 1.17 entry | Native caller contract and established behavior |
|---|---|
| `0x43d0d0` | ECX=parent Hatchery/Lair/Hive. SpawningLarva secondary 78: counts visible parent-linked Larva in radius 256, requires count below 3 and the caller's update phase, decrements byte +ca or attempts a spawn and sets 37. |
| `0x43d360` | ECX=parent, stack=[Point32 output], ret 4. Searches stock-footprint candidates for the spawn point. |
| `0x43c490` | ECX=Point32 candidate, EDX=horizontal displacement, stack=[vertical displacement], ret 4. Clamps endpoints and applies tile-mask checks. |
| `0x43d910` | ECX=candidate CUnit, EDX=parent. Counts type 35 with a visible Sprite and matching CUnit +80 link, stopping at count 3. |
| `0x43d990` | ECX=new Larva. Uses its real parent Sprite and stock extents to select the initial side state. |
| `0x41b4b0` | ECX=producer, stack=[requested unit ID], ret 4. Raw circular build-queue admission: finds an empty slot, checks current resources and applicable supply, and immediately pays for a nonbuilding request. This caller does not replace the separate can-make/command-card requirement frontend. |
| `0x42a2c0` | ECX=CUnit, stack=[primary order], ret 4. Original order allocation, activation and queue processing. |
| `0x43bac0` | ECX=CUnit, UnitMorph order 42. Larva 35 becomes Egg 36, uses the stock Drone build time 300, waits for the Egg signal, then changes to Drone 41 with Birth order 41. |
| `0x43bd50` | ECX=CUnit, Birth order 41. Waits for stock signal 4, replaces construction images, completes/shows the unit and applies its rally/default continuation. |
| `0x42a340` | ECX=CUnit, stack=[primary order,x,y], ret 12. Original position-order issuer. |
| `0x43ba90` | ECX=Drone, DroneStartBuild order 25. Issues DroneLand 70 and inserts queued DroneBuild 26 at the requested position. |
| `0x43c970` | ECX=Drone, DroneLand order 70. Approaches y−7, checks the placement grid, lands at the requested point and activates queued order 26. |
| `0x43b900` | ECX=Drone, DroneBuild order 26. Rechecks current position and placement, rechecks resources, pays and converts the worker. |
| `0x4086b0` | CL=owner, EDX=requested type, stack=[check supply,show feedback], ret 8. Caches stock costs, performs the requested supply check, then compares current stocks. DroneBuild passes [1,1]. |
| `0x408650` | CL=owner. Deducts the cached mineral/gas cost. |
| `0x43c000` | ECX=Drone, stack=[building ID], ret 4. Removes completed-worker accounting, clears completion, changes type, transforms AI and initializes Zerg construction. |
| `0x43bf10` | ECX=new building. Cancels creep-life records in its area, installs construction graphics/buttons, sets HP to floor(maxHP/10), issues order 45 and clears the current queue ID to 228. |
| `0x43b6e0` | ECX=unfinished building, BuildSelf order 45. Advances stages, timer, HP and stock animation; completion requires the stock signal. This unchanged body is imported from the frozen Nydus batch. |
| `0x41a340` | ECX=unfinished building. Cancellation frontend; an ordinary unfinished Pool reaches `0x43b380`. |
| `0x43b380` | ECX=ordinary unfinished Zerg building. Refunds 75%, restores a completed Drone in the same CUnit, and reconstructs its images, orders, accounting, AI and saved health. |
| `0x486ba0` | ECX=worker being converted. For controller 1 with a real AI record, removes WorkerAI from its Town and inserts BuildingAI. |
| `0x487900` | ECX=cancelled building being restored. Releases BuildingAI and invokes the Town's real worker insertion path. |

The exact main-order adapters are retained in the static report. Orders 26, 41, 42 and 45 use the immediate main dispatch; 25, 70, 77 and 101 use the throttled main dispatch. A direct call to a helper is not presented as proof of that dispatch cadence.

## Paid Drone production

The parent is genuinely allocated and completed by `0x424900/0x424da0`. Complete original `0x43d0d0` then allocates and completes a Larva, writes its parent link +80 and chooses its side state. No CUnit, Sprite or Image record is authored by the harness after pool setup. Initial resource, supply, controller and terrain inputs remain explicit.

Actual `0x41b4b0` queues Drone 41 and deducts **50 minerals**. Actual `0x42a2c0` issues UnitMorph 42. Subsequent complete `0x445070` calls naturally produce the following sequence:

| Object callback after issuing Morph | Original output |
|---:|---|
| 1 | Larva becomes Egg 36, timer 300, state 1, images 21/22. |
| 302 | Timer reaches zero and the Egg's completion animation begins. |
| 318 | The stock Egg script produces signal 4. |
| 319 | The same CUnit changes to Drone construction image 19 and Birth order 41. |
| 333 | The stock Drone birth script produces signal 4. |
| 334 | Drone completes, replaces image 19 with images 17/18, clears build time and applies the birth displacement y=442→435. |

These callback numbers describe this declared initial phase and stock script. They are not a universal wall-clock duration for every production context. The harness never writes a unit type, build timer, order state, animation stage, health or IScript signal to advance this sequence.

This composition starts at raw queue/morph issuance. The graphical command card and network-command frontend are outside its caller boundary. The prepared Hatchery's own construction and a campaign's supply/tech startup are not implied by the successful Drone lifecycle.

## Deferred payment and same-slot construction

Building admission differs from ordinary unit payment. `0x41b4b0` checks resources but the DAT Building property avoids its immediate debit. DroneBuild later calls `0x412f10` with the current stock footprint and owner, then `0x4086b0/0x408650` to recheck and deduct payment. A queued building can therefore fail after successful admission.

The normal paid Drone receives position order 25 toward **(832,528)**. Its genuine physical movement, stock image updates and original position/queue consumers run inside complete `0x445070` calls. Relative to that build order, the observed chain is:

| Callback | Original type/order/state | Timer | Raw HP |
|---:|---|---:|---:|
| 1 | Drone 41 / Land 70 / 0 | 0 | 10,240 |
| 2 | Drone / Land 70 / 1 | 0 | 10,240 |
| 129 | Drone / Land 70 / 2 | 0 | 10,240 |
| 138 | Drone / Build 26 / 0 | 0 | 10,240 |
| 139 | Pool 142 / BuildSelf 45 / 0, incomplete | 1,200 | 19,200 |
| 441 | Pool / 45 / 1 | 898 | 62,688 |
| 442 | Pool / 45 / 2 | 897 | 62,832 |
| 741 | Pool / 45 / 3 | 598 | 105,888 |
| 742 | Pool / 45 / 4 | 597 | 106,032 |
| 1,340 | Pool / 45 / 5 | 0 | 192,000 |
| 1,341 | Pool / 45 / 6 | 0 | 192,000 |
| 1,346 | Still incomplete: stock script produces signal 4. | 0 | 192,000 |
| 1,347 | Completed Pool, order 23; stock images 108/64/65. | 0 | 192,000 |

The stock Pool cost is **200 minerals, 0 gas**, build time **1,200**, maximum HP **750**, initial HP **75**, and raw build HP increment **144**. Timer zero alone does not establish completion: the final stock signal is consumed later.

`0x43c000` reuses the Drone's CUnit; it does not allocate a separate building CUnit. `0x424ad0` reconstructs type-dependent Sprite, Image, spatial and accounting state while preserving the relevant caller fields. `0x426750` saves previous type +88 and whole HP WORD +ae, clamped to at least 1, before proportional HP rescaling and stock type initialization. `0x43bf10` then sets the initial construction health. The finite two-CUnit case leaves no free CUnit after creating Hatchery and Larva/Drone, yet completes the same-slot Pool at callback 1,346 in that distinct world profile. Subsequent Larva allocation attempts really fail against that finite pool; no fake allocator is installed.

A 1,000-mineral fixture reaches **950** after Drone payment, remains **950** after Pool queue admission, reaches **750** at construction start and reaches **900** if cancelled. Unit/live/completed accounting, owner/active/hidden lists and supply are captured alongside the graphic and order changes. Pool construction releases the Drone's two supply units; cancellation restores them.

## Admission boundaries and a real competing debit

Four stock admission cases distinguish 49 minerals from the Drone's exact 50 cost and distinguish remaining balances 0/199/200 from the Pool's 200 cost. At 49, real Larva queue admission returns 0 without payment or a queued request. After producing a Drone, remaining 0 and 199 reject Pool admission; exactly 200 accepts while leaving the balance at 200.

The late-payment case starts with 250 minerals. The first genuine Drone consumes 50; Pool admission at 200 succeeds without payment. Full object callbacks naturally create another parent-linked Larva. Its actual paid Drone queue consumes another 50 and begins Egg morphing. The original first Drone then physically travels through 25→70→26. At its destination, the repeated stock check rejects the 150-mineral balance. No Pool CUnit transformation, Pool completed counter or Pool debit occurs; the Drone remains completed and activates its reset order 150. The remaining 150 minerals are preserved. No host resource write occurs between admission and this failure.

This separates raw admission, deferred payment, a competing real producer and the native failure continuation. It does not infer a resource race from static disassembly alone.

## Cancellation and AI lifetime

The ordinary cancelled Pool becomes a completed Drone in the same CUnit. `0x43b380` invokes `0x408490` for the three-quarter building refund, attempts the cosmetic cancellation Thingy, changes type to 41, restores AI through `0x487900`, completes the Drone and assigns hover-reset order 150 with its stock idle continuation. Early cancellation, cancellation after 350 build callbacks, cancellation after 1,202 callbacks while timer=0/state=6/signal=0, fractional original damage, and an empty cosmetic Thingy pool are separate cases. The zero-timer case still cancels as an unfinished building. Cosmetic allocation failure does not prevent refund or Drone restoration.

Restored health is the saved whole-HP WORD +ae shifted left 8. The fractional case receives actual original `0x41dfc0` damage before flight and naturally regenerates during travel. At the native conversion entry its HP is **8,876 raw = 34.671875 HP**; cancellation restores **8,704 raw = 34 HP**, rather than reproducing the prior fractional value. No harness HP assignment produces this difference.

The AI cases use real `0x486a20` pools and `0x487030` Town startup after the paid Drone's complete native Move 6. They obtain a real WorkerAI linked to the Town, then explicitly select controller 1 as a caller input. Same-position DroneBuild 26 removes that WorkerAI, changes Town worker count **1→0**, and allocates BuildingAI kind 3 linked to the same Town. Completion preserves BuildingAI and count 0. Cancellation releases BuildingAI, inserts WorkerAI kind 2 into the same Town and restores count **0→1**. No AI pointer, kind or linked-list field is authored.

The computer completion case executes selected real per-unit phases in original order: movement preparation `0x43a5f0`, status/cooldowns `0x43a320`, primary dispatcher `0x439420`, secondary dispatcher `0x439310`, populated-image update `0x418b20`, and Thingy cleanup `0x418990`. It reaches completion after 1,208 such phase sets. The completing primary call also naturally reaches `0x48cd70→0x48e180→0x487110` with an empty spending heap. **`0x43a320` alone does not execute primary orders or stock image waits.** The case does not execute the whole computer Town scheduler or claim its campaign startup.

Extractor 149 and upgraded-building cancellation have different paths. Extractor can allocate a replacement Drone and issue Death for the Extractor; `0x43b380` delegates upgraded buildings to `0x43b500`. Neither is assigned the ordinary Pool contract.

The frozen harness/evidence retain historical supply field names `cap` for `0x529424` and `total` for `0x529484`. Those labels do **not** define the native semantics: for Zerg, `0x529424` is **live provided capacity**, `0x529454` is **used supply**, and `0x529484` is the **global cap**. The declared initial writes therefore mean **provided=400, global cap=20**, not the reverse. The original available-supply check uses `min(provided,global_cap)-used`; the selected producer fits that effective cap. This naming correction leaves the recorded addresses, original execution, counter deltas and frozen hashes unchanged. See the corrected [training supply contract](native-training.md).

## World, graphics and scheduling boundary

The declared world is a synthetic **4,096×4,096-pixel / 128×128-tile** clear map, uniform region/group 1, zero contours, finite original pools and valid eight-player region arrays. Both the map rectangle `0x543228=[0,0,4096,4096]` and separate dimensions are initialized. Tile DWORDs are **`0x5041f700`**, supplying current/known creep and the existing-creep removal mask. The separate 128×128 creep-record plane contains zero WORDs. Actual `0x4b44a0` initializes all 1,024 creep-life records. Existing creep is a caller input, not a proven provider producer.

Original creation establishes owner, active, spatial, Sprite-row and Image lists. Authenticated raw retail DATs are loaded through their original descriptors and normalized by actual `0x425610` before initialization. Authenticated GRP/LO/IScript resources populate private memory. The native VM and Image mutations execute; pixel rasterization does not. The dedicated image provenance includes 146 Image IDs and 130 distinct GRP files, with 160 LO references to 32 distinct LO files, including inherited frozen assets and the added Larva/Egg/Drone/Pool construction resources.

The host increments the original caller's object-step counter `0x52651c` before each complete `0x445070`, matching the original main-loop caller boundary. It never rewrites unit timers, stages, signals or positions to advance construction. The elapsed-game-time input `0x5349d8` remains 0; these are object callbacks, not complete accepted frames or a complete time-advancing strategic simulation.

Auxiliary countdowns begin at 1,000 and expire naturally. Complete callbacks genuinely reach deferred fog-cache scan `0x40af30`, visibility clearing `0x46c180` and native dirty-window helpers. The zero-CV5/cache-equal profile and clipped empty window are explicit; populated campaign terrain rendering is unclaimed. The actual creep scheduler reaches `0x4b4a40`, whose active-bucket counts are empty in the genuine initialized pool. The player/Town round-robin services and strategic elapsed-time guard also execute when their countdowns expire. Their human/inactive or zero elapsed-time profiles are kept separate from positive strategic decisions. In the ordinary 16-CUnit completion case, actual calls include fog scan **7**, visibility clear **7**, empty creep-life consumer **69**, player round-robin **86**, Town round-robin **86**, and strategic elapsed-time guard **86**. Positive `0x4b47f0` removal scheduling is not reached by that completion chain.

The Hatchery remains active and continues its real Larva-spawning secondary service. Newly spawned Larva execute their stock main order 77 and movement; selected state-15 wait/retry `0x42bfd0` is a reached dependency. Blocked/alternate Larva spawn candidates, multi-parent spawning interactions, all Larva movement, positive creep stamping/recession and all Zerg building types remain separate work. Provider paths that require original CRT/Windows context imports are not replaced, executed or counted as closure here.

## Evidence and reproduction

The publication consists of [the static inspector](../../tools/research/inspect_native_zerg_construction_117.py), [the native probe](../../tools/research/probe_native_zerg_construction_117.py), [static witnesses](evidence/native-zerg-construction-static.json), [complete probe results](evidence/native-zerg-construction-probes.json), [image/LO provenance](evidence/native-zerg-construction-assets.json), and [network-string provenance](evidence/native-zerg-construction-string-assets.json). The correct original table bindings are **network.tbl→`0x665de0`** and **stat_txt.tbl→`0x6ab4f8`**, with a separate native loader witness. Retail resource bytes remain in private scratch.

Each generated result pins the exact executable, probe, imported source dependencies, DATs, stock IScript and asset/string manifests. Complete return and stack restoration are required; every executed instruction must lie inside a reviewed original range. RNG state and source counters are checked against an independent exact integer LCG. No partial prefix or helper replacement contributes to the complete-case total.

Frozen dependencies include [Nydus](native-nydus.md), [primary orders](native-primary-orders.md), [populated creation](native-image-initialization.md), [active/IScript ordering](native-active-iscript.md), [construction placement](native-construction.md), [Town pools](native-towns.md), [AI construction](native-ai-construction.md), and the preceding movement/lifecycle batches. Pinned source leads are [GPTP UnitMorph](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/unit_making/unit_morph.cpp), [GPTP Larva/Creep spawning](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/larva_creep_spawn.cpp), and [Teippi order constants](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/order.h). Their local source trees were inspected at those pins.

Reproduction requires the exact user-supplied executable, authenticated effective retail data, private extracted graphics/strings and pinned PyMS decoder. The probe is inert without `--execute-original-code`; the executing command must use the reviewed x86_64 Rosetta Python runtime. `--case-group all` regenerates the published result rather than merging earlier group runs. No six-campaign playability, complete renderer or whole-game execution is implied by this batch.

The private paths used for the published run are:

```sh
PYTHONDONTWRITEBYTECODE=1 arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_zerg_construction_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-zerg-construction-static.json

PYTHONDONTWRITEBYTECODE=1 arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_zerg_construction_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-zerg-construction-assets \
  --asset-manifest docs/research/evidence/native-zerg-construction-assets.json \
  --string-manifest docs/research/evidence/native-zerg-construction-string-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --execute-original-code \
  --out docs/research/evidence/native-zerg-construction-probes.json
```

A faithful Defold simulation needs separate paid unit admission and deferred building payment, same-slot Drone morphing, reversible AI pool transitions, saved whole-HP cancellation state, original staged stock-script completion, and finite cosmetic allocations. The object, order and image phases must preserve their original sequence: timer zero and a convenient animation substitute are insufficient.
