# Paid Assimilator, original Geyser conversion and Probe gas lifecycle

This batch connects actual paid Probe production to ProbeBuild 31 on an authentic Vespene Geyser record, original resource rechecking/payment, in-place conversion, stock Warp 48 and later physical gas harvesting. It also tests three cancellation stages, completed Assimilator Death, fully occupied finite pools, competing payment, a genuinely occupied resource, and an original carried mission item. Guest execution uses original 1.17 instructions and real callees without helper substitutions, Windows imports or host progress writes.

The executable is StarCraft 1.17.0.1, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`. Preferred VAs below belong to that exact artifact. The frozen [Protoss construction](native-protoss-construction.md), [gas harvesting](native-gas-harvesting.md), [mission UNIT consumers](native-mission-units.md), [carried construction](native-carried-construction.md), [movement](native-movement-fsm.md), [orders](native-primary-orders.md) and [image/IScript lifecycle](native-image-initialization.md) sources are reused unchanged.

## Evidence and declared inputs

[Inspector](../../tools/research/inspect_native_assimilator_117.py), [probe](../../tools/research/probe_native_assimilator_117.py), [static witnesses](evidence/native-assimilator-static.json), [complete execution evidence](evidence/native-assimilator-probes.json), [image/LO manifest](evidence/native-assimilator-assets.json) and [string manifest](evidence/native-assimilator-string-assets.json) record exact code, resources, dependencies, calls and outputs. Without the explicit execution flag the probe emits a plan and runs no original code.

The final all-run contains **12 complete cases, zero prefixes, all passing**: **12,637 complete original top-level calls**, **85,645,965 original instructions**, maximum **876,017** instructions in one call. The allowlist has **1,358 reviewed ranges** and **496 reached original function entries**; all **118 source dependency hashes** match the final evidence. Static extraction supplies **17 fully decoded witnesses / 849 instructions** and separately marks **two unexecuted static witnesses**. Execution uses x86_64 Python 3.9.6 / Unicorn 2.1.4 / pefile 2024.8.26. These counts describe the bounded compositions below, not complete game execution.

| Frozen file | SHA256 |
| --- | --- |
| Probe | `f2257edf282adaafd890bc49dafb1811d70a87553100ccc905be118c709118a4` |
| Inspector | `2814871c2fa75c5478b46db5d52f78d503d5d93256710c3c56cc710b401ee12c` |
| Execution evidence | `1250160c290419ba08b65d66883f11d161018cdbc9bd530657463f9153985940` |
| Static witnesses | `eb2e5000cca2e0a52240c8f82f7783b8a5bbf563c3c42fe2d5fce26b9d567b0a` |
| Image/LO manifest | `c82b98a3ed720a637dfad066c48bc0cd8002432a1669564ef6bda98dcdf4f862` |
| String manifest | `73f51a759ed0aeaaf32e5e3476174994f2399fde2d374f00e56c9cf8c0a6bc40` |

The separate image manifest is **`native-assimilator-assets.json`**, with **304 image IDs, 256 GRP paths, 322 LO references and 63 LO paths**. String identity is recorded in **`native-assimilator-string-assets.json`**. The installed psi-mask input additionally pins prior native evidence `native-placement-probes.json`, SHA256 `af11f5ba631b971960f4cac3edc75bb33f230cb6c9a9047369746ef6c110671a`. No retail resource payload is embedded in these reports.

Terrain is an explicit clear 4096×4096 / 128×128 world with map rectangle `[0,0,4096,4096]`, region/group 1, empty contours, zero creep, native initialized finite pools and eight private AI-region arrays. Owner 3 uses controller 2; viewer 0 and disabled audio/display backends differ from that owner. Complete `445070` object callbacks run, with caller increments of `52651c`; the separate elapsed time `5349d8` remains zero. The complete outer accepted frame, WinMain, original renderer and six-campaign world are excluded.

Nexus 154 at (400,400) is originally allocated/completed, then actually pays 50 for Probe 64 through `41b4b0` and secondary Train 38. Stock time/image consumers produce the completed Probe after 302 object callbacks. No host stage, timer, readiness, position, HP, queue, count or payment update produces that child. Protoss provided/used/global-cap arrays are `529544/529574/5295a4` plus 4×owner. The Nexus contributes 18 raw provided supply, the paid Probe uses 2, and the declared global cap is 400; these are half-units.

The neutral resource is the unchanged first Geyser 188 UNIT record in original Zerg02: record 14, center (960,256), owner 11, stored amount 4851. The campaign/map/record hashes are included in each relevant case. Original `469460` creates/completes/defaults it, and `46a420` consumes its authenticated record property block to set the real amount. The host copies the raw record into private input memory; it does not assign the CUnit amount/type/status. This authentic record is combined with declared clear terrain, rather than a claim that the complete Zerg02 map has run.

Stock DAT/IScript/GRP/LO resources are authenticated. The stock psi mask is the same explicitly installed resource state as the previous Protoss batch: 40 original opacity samples equal the prior native post-allocation mask. The complete `42fe60` malloc/bootstrap remains unexecuted. Assimilator does not acquire power by a fabricated provider. `665de0` receives network.tbl and `6ab4f8` stat_txt.tbl; the original table binding and backend guards remain distinct from visible notification output.

## Native contracts and object identity

| Preferred VA | Actual contract |
| --- | --- |
| `41b4b0` | ECX=Probe, stack[157]; raw circular build request admission; no immediate building payment |
| `42a340` | ECX=Probe, stack[31,x,y]; original primary build order issuance |
| `42ee00` | ECX=Probe; physical four-state build FSM; Assimilator uses approach threshold 88 |
| `41bcb0` | ECX=builder; current resource and real placement recheck, then payment |
| `41b3f0` | ECX=builder, stack[refinery type]; resolve Geyser, clear completion, owner/type conversion and stock image setup; ret 4 |
| `47cd70` | Reads placement/UI mode byte `6751e8`; selected original-zero branch omits UI helper `437050` |
| `4152d0` | ECX=Sprite, stack[image,xoffset,yoffset,parent]; original below-main image producer |
| `40be80` | ECX=Flingy; commit reviewed blocked-motion fields, direction and actual Walking/WalkingToIdle animation |
| `42f020` | ECX=Assimilator; ordinary four-state stock Warp 48 |
| `4237a0` | ECX=worker; WaitForGas 82 eligibility and actual resource wait insertion |
| `4235c0` | ECX=worker; HarvestGas 83 enter/hide/timer/exit/extract/return consumer |
| `41a340` | ECX=incomplete Assimilator; original refund and Death issuance |
| `423400` | ECX=resource; clears busy byte `+d3` and unlinks all waiting workers `+d4/+d8`, clearing their `+d0` resource references |
| `425270` | ECX=unit; special release restores visible refinery types 110/149/157 to Geyser instead of returning the CUnit pool slot |

`41b3f0` first accepts a current unit target only if its type is 188; otherwise it resolves the live Geyser at the builder's order point through `4132b0`. It decrements completed accounting through `414580`, clears completion, applies actual owner and Sprite-owner transfer through `426ed0/426e80`, and calls `424ad0` with the requested refinery type. It selects buttons 231 and creates below-image 345. Its final HP is unsigned stock max raw HP divided by 10. It returns the **existing Geyser CUnit**, without a new CUnit allocation.

The Probe caller then replaces Sprite images with WarpAnchor 211, adds Geyser image 344 below it, targets the converted building, invokes actual `486200`, and issues Warp 48. Its build-queue slot becomes 228 after the conversion call. The original type-change path rebuilds/recycles the Sprite and image resources; CUnit identity and Sprite identity are separate. Three-CUnit and three-Sprite finite profiles both complete; no extra permanently allocated object is required. Original allocation order decides whether the rebuilt Sprite uses another free slot or its old slot.

## Normal paid build, stock warp and gas command

The normal request is admitted at 950 minerals, without immediate debit. The paid Probe approaches the genuine resource, reaches build state 2 at callback 138 and converts it at callback 142, after actual 100-mineral payment: 950→850. The source retains amount 4851 while type 188/owner 11 becomes type 157/owner 3. Unit count stays three: Nexus, Probe and the same resource/building body.

Stock Assimilator has 450 HP/450 shields and build time 600. Conversion starts at 11,520 raw HP/shields, with native WORD HP/shield gains 173/172. Ordinary native shield regeneration also runs. The measured sequence is:

| Build-relative callback | Original output |
| --- | --- |
| 142 | Same resource body becomes incomplete Assimilator; Warp 48 state 0/time 600/Image 211 |
| 143 | Probe leaves build state 3, reaches native WaitForGas rejection and idle 2 |
| 144 | Stock human Guard 2 selects PlayerGuard 3 |
| 743 | Timer was zero at entry; SpecialState1 starts, warp state 1 |
| 747 | First stock signal consumed; ordinary images 158/160 and temporary warp script; state 2 |
| 765 | Second signal consumed; WarpIn 21 and overlay 159/draw method 17; state 3 |
| 813 | Third signal consumed; original completion to order 23, stock max HP/shields, amount still 4851 |

A critical caller distinction is that Build state 3 issues WaitForGas 82 **while the building is incomplete**. The complete object callback reaches `4237a0`; its mandatory completed-target check fails, so it holds/resumes idle. This selected native flow does not retain a 600-tick automatic wait. The evidence records target completion/carry flags at actual gas-handler entry. A remake must preserve the consumer check and traversal rather than infer successful harvesting from the builder's issuance alone.

After actual completion the test issues a **new original Harvest 79 command**, using `42a380(Probe,[79,Assimilator])`. Native travel/wait service reaches HarvestGas 83, hides the Probe at callback 834 with timer 36 after the same-callback decrement, exits/shows/extracts at callback 870, and delivers gas at callback 991. Source amount becomes 4843; current and cumulative gas gain 8; cargo amount/type and stock gas overlay clear at delivery. Hidden and active membership, finite spatial endpoints, positions, fixed coordinates, images and queues are original outputs. The transfer mechanism is the frozen gas consumer; these timings belong to the declared positions and composition.

## Cancellation, Death and resource restoration

Cancellation after conversion, after timer zero, and during stock warp stage 3 uses actual `41a340`. Each refunds 75 minerals through the original fractional refund helper: 850→925. The next complete object callback restores the **same** Geyser CUnit at callbacks 143, 743 and 766. It does not destroy a resource and synthesize a replacement by host action.

Completed Assimilator Death at callback 813 restores it at callback 814, without refund. Native resource cleanup reaches `423400`, then special release `425270` clears completion, changes type back to 188, updates images/owner/accounting, assigns stock max HP (100,000×256), returns owner 11 and reinstates completion. The original neutral amount remains 4851 in all four cases; unit count remains three, provided supply 18 and used supply 2. This restoration path must be distinguished from ordinary CUnit/pool reclamation.

These profiles have no positive waiting-worker list and destroy no occupied hidden worker. The reviewed list-clear loop exists in full original code, but empty-list execution is not evidence for every occupied-source interruption.

## Admission and failure boundaries

After genuine paid Probe production, raw Assimilator admission rejects 99 minerals and accepts 100 while retaining the balance. A later actual second-Probe queue consumes 50 from an admitted 100-mineral balance. Physical Build 31 then fails the live resource recheck at callback 142: no conversion or further debit, Geyser owner/type/amount unchanged, and build queue 157 retained. The second original Train child is already allocated; total unit count is four, not three.

Another original Probe is paid and completed while the first genuine Assimilator is still building. Its raw Assimilator request is accepted, but the original physical placement/Geyser lookup rejects the occupied type-157 source. Minerals remain 800 after two Probes and one Assimilator; no second 100-mineral debit occurs. Queue 157 remains, the first building is intact and source amount stays 4851. Target-aware placement is separate from raw queue admission.

Fully occupied CUnit/Sprite pools are discriminating success profiles, unlike the earlier ordinary Pylon allocation failure. Conversion reuses the original resource body and genuinely releases/rebuilds Sprite storage; neither profile gets a fabricated free slot or resource refund. These profiles establish this consumer's storage demand; higher-level command admission at the retail 1700-unit limit is not executed.

## Carried mission item

The paid Probe moves through original Move 6 to the authentic Zerg02 destination rectangle `[2368,2400,2464,2464]`. Original `469460` creates stock Chrysalis 216 eight pixels from its actual position; normal callbacks perform pickup. The unchanged campaign condition is CurrentPlayer Bring at least one type 216 to location 1, raw-record SHA256 `7af1048989b45117dd455e783f80b152ec5c9afdee4616785c4e653c309f5d59`. The original condition consumer returns 1 while the genuine carrier is in that goal.

The carrier then travels by actual Move 6 toward (800,256), and ProbeBuild 31 pays/converts the authentic Geyser. On this approach conversion occurs at callback 26 and complete stock warp at 697. Probe `+c0` and item `+d4` remain reciprocal, carry flags remain 4, and the item remains hidden. Actual `4272c0` Drop is never called. This preserves the same Probe body and item throughout an independently living Assimilator, unlike the paid Drone transformation/drop contract.

The original Bring consumer returns 0 while the carrier is away and 1 after its physical return to the goal. These are complete original condition calls with authenticated MRGN/FORC/condition inputs; full campaign trigger scheduling, mission outcome and an authentic whole map are excluded.

The two long routes each stop once short of the requested point: outward at (816,512), return at (2372,2112). The fixture issues one **fresh real Move 6** to the same requested point after each native idle stop, and records the command. It does not write position, heading, path, velocity or readiness, and does not claim automatic retry of the original command. This limits the route-composition claim while preserving the construction/carry evidence.

## Reconstruction leads, port implications and remaining scope

Pinned [GPTP Probe construction](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/building_making/building_protoss.cpp) and [OpenBW refinery conversion](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L3034) agree on the broad Geyser reuse and builder special branch. Their source versions and modified architecture do not establish original 1.17 scheduling parity. The real calls here discriminate raw request admission, late checks, conversion identity, stock signals, immediate incomplete-target rejection and physical gas delivery.

For Defold, preserve the resource/building union and object identity, owner/live/completed accounting, independent Probe/building lifetime, actual Sprite/image replacement, queue clearing only after conversion, integer health/shield gains, generic Flingy IScript signals, consumer eligibility checks, neutral Geyser restoration and finite pool ordering. Keep authentic resource records, caller terrain/mask initialization, direct original commands and complete object callbacks as distinct evidence layers.

Remaining scope includes occupied hidden-worker destruction, positive waiting-list clearing under Death, cross-owner interruption, AI Town/gas request scheduling, depleted-source creation from other authentic records, the separate [Extractor](native-extractor.md)/Refinery producers, blocked exits, broader actual campaign terrain and complete original resource/bootstrap/rendering execution. This batch supplies complete selected native compositions, not full six-campaign or reconstruction parity.
