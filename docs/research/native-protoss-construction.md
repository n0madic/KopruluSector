# Paid Protoss construction, stock warp and power lifecycle

This batch follows an original-created Nexus through paid Probe production, physical ProbeBuild 31, paid Pylon allocation, stock Warp 48 completion, and the original psi-provider list/aura producer. It also covers Probe interruption, cancellation before and after the build timer reaches zero, resource rechecking, finite pool failures, actual Town AI assignment, and a paid Gateway whose power disappears and returns after original Pylon Death/replacement. All guest execution uses original instructions and real callees; no function result, stage, signal, build time, position or payment is substituted.

The target is StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. Addresses are preferred VAs. This extends [training lifecycle](native-training-lifecycle.md), [normal populated Train](native-populated-lifecycle.md), [primary orders](native-primary-orders.md), [Terran construction](native-construction.md), [paid Zerg construction](native-zerg-construction.md), [movement](native-movement-fsm.md), [placement and stock power geometry](native-placement.md), [image/IScript initialization](native-image-initialization.md), [active object services](native-active-iscript.md) and [Death/recycling](native-unit-death.md). Frozen dependencies are unchanged.

## Evidence and reproducibility

The dedicated [inspector](../../tools/research/inspect_native_protoss_construction_117.py), [probe](../../tools/research/probe_native_protoss_construction_117.py), [static witnesses](evidence/native-protoss-construction-static.json), [execution report](evidence/native-protoss-construction-probes.json), [image/LO provenance](evidence/native-protoss-construction-assets.json) and [string provenance](evidence/native-protoss-construction-string-assets.json) specify code, input resources, private pools, calls and outputs. The report records exact executable, script, dependency and resource hashes, engine versions, instruction limits and independent PRNG validation. Without `--execute-original-code`, the probe prints a plan and executes no original instructions.

The fresh all-run passes **17 complete cases, zero prefix cases**, with **15,313 complete top-level original calls** and **68,897,156 original instructions**. The largest call executes 705,866 instructions, below the one-million bound. The allowlist contains 1,280 reviewed ranges; 473 reviewed entry labels are reached. All 100 source dependencies match their recorded hashes. The dedicated inspector publishes 12 witnesses and 680 decoded instructions, including two explicitly unexecuted static witnesses.

The probe SHA256 is `49f44c2ce3139ce190772a0dd10ffdb018197916fc568d75f92ab133758c2c9b`; inspector SHA256 is `afada14ecb8e94dd6b53fac14bcef179e7d8512de3279cdcd527a0ed0dfa4dc7`. The image manifest is `da05f3a9d795385d9e46be16756ca226e87dd9c93e27f56a6fedb22dbc934053`, and the string manifest is `73f51a759ed0aeaaf32e5e3476174994f2399fde2d374f00e56c9cf8c0a6bc40`. Image provenance covers 183 image IDs, 153 distinct GRPs, 189 LO references and 40 distinct LO files. Execution uses Unicorn 2.1.4 and pefile 2024.8.26 under x86_64 Python 3.9.6.

| Scenarios | Connected contract |
| --- | --- |
| 4 | Paid Probe → Pylon completion, with normal Probe continuation, actual Move 6, Probe Death, or real damage during timer-zero stage3 |
| 3 | Pylon cancellation immediately after creation, at timer zero, and in stock warp stage3; refund, Death and pool return |
| 4 | Original resource admission at Probe 49/50 and Pylon 99/100 mineral boundaries |
| 2 | Genuine paid Probe with exhausted CUnit or Sprite pool when Pylon allocation occurs |
| 1 | First Probe/Pylon admission followed by actual second-Probe payment; late resource recheck fails |
| 2 | Actual Town/WorkerAI → new BuildingAI, retained worker, selected completion/cancellation services |
| 1 | Paid Gateway on real Pylon list → original Pylon Death/power loss → paid replacement and power restoration |

## Declared world and resource inputs

The world is a clear 4096×4096 map, 128×128 tiles, an independent map rectangle `[0,0,4096,4096]`, uniform region/group 1, empty contours, and eight real private AI-region arrays. Tile flags are `0x1f700`; current/known creep and the WORD creep-record plane are zero. Original creep-life initialization provides 1024 records. Object-step counter `52651c` advances before complete `445070`; elapsed-game-time `5349d8` remains zero. Auxiliary countdowns start at 1000 and later expire naturally. Pools are finite. Empty command/script/resource/spatial auxiliary profiles are stated in the evidence.

The stock psi mask is an explicit resource initialization input. Image 584 resolves to authenticated `unit\thingy\Juice01.grp`, SHA256 `cd98ce60f5afb8167896cc099f0e9650edf2dc03bcc8c14fda90a667bff3ae08`. Forty complete original `40fd50` opacity calls sample its 252×154 first frame. The host mirrors those returned samples into a 16×10 mask with half-extents 256×160 and verifies equality with the [previous original post-allocation output](evidence/native-placement-probes.json). This installs resource state, not a replacement for a gameplay helper. The complete initializer `42fe60` still crosses CRT allocation `4d001c`; that allocator/bootstrap is **not executed** here. Its native post-allocation behavior remains the separately labeled fragment in the placement report.

Stock DAT/IScript, GRPs and LO files are hash checked. No renderer or device upload runs. `665de0` receives authenticated `network.tbl`; `6ab4f8` receives authenticated `stat_txt.tbl`. Owner 3 differs from viewer 0, and the sound/notification backend remains disabled. Original allocation failures still execute their actual feedback guards. The correct table identity does not imply a displayed notification.

Supply values are raw half-units: 18 provided is 9 displayed supply, Probe 2 is 1, and cap 400 is 200. Supply uses the canonical `(provided, used, global cap)` triplet at `529544/529574/5295a4`, each plus 4×owner. The global cap is a declared 400; the actual completed Nexus adds 18 provided. A completed paid Probe uses 2. Pylon completion adds 16 provided: 18→34. No host write simulates that contribution. See [training accounting](native-training.md) for all race triplets.

## Native entry contracts

| Preferred VA | Actual ABI and role |
| --- | --- |
| `424900` | ECX=unit type, EDX=x, stack[y,owner]; populated allocation, stock images/IScript, ownership/list/count setup |
| `424da0` | ECX=unit; initial Nexus completion and later natural Warp 48 completion |
| `41b4b0` | ECX=producer, stack[type]; actual circular queue/resource admission; mobile unit costs debit immediately, building requests defer payment |
| `42a1e0` | ECX=Nexus, stack[38]; secondary Train |
| `42a340` | ECX=Probe, stack[31,x,y]; issue primary ProbeBuild 31 with original order pool |
| `42ee00` | ECX=Probe; native four-state ProbeBuild 31 |
| `42f230` | ECX=Probe; writes order 31/state 2 and tailcalls `42ee00` for order 32; static dispatch mapping, no separate positive order 32 case |
| `41bcb0` | ECX=builder; real resource recheck, shared placement and payment before allocation |
| `42f020` | ECX=building; native four-state autonomous Warp 48, dispatched through early order 48 adapter `43943c` |
| `411840` | ECX=main image, stack[210]; original temporary warp script/draw-method setup; stack cleanup 4 |
| `40e650` | ECX=image; draw method 17 parameter/timer update and native completion signal |
| `42f5b0` | ECX=completed Pylon; InitPsiProvider 164 inserts real psi links, creates aura and resumes stock idle |
| `42f270` | ECX=Pylon; actual Sprite 321/Image 584 aura allocation, stock image/VM initialization and viewer gates |
| `430670` | No arguments; dirty-provider pass through original active list, power query and disable/enable animation handling |
| `430610` | ECX=Pylon; remove real `+f8/+fc` psi links/head during destruction |
| `41a340` | ECX=incomplete building; selected Protoss refund and original Death issuance |
| `486200` | ECX=Probe, EDX=new building; actual BuilderAI/Town-derived BuildingAI assignment |

The common constructors, accounting, movement, placement, order pool, unit/image update, damage, AI and reclamation callees come from the frozen dependencies. Direct entry calls and full object callbacks are distinguished. The two AI scenarios use selected original unit phases and explicitly omit the full computer Town scheduler. Selected Town/unit phases enable and restore the original RNG domain through actual `4781c0`, matching the outer object caller.

## Paid Probe and independent Pylon warp

The starting Nexus 154 is genuinely allocated/completed at (400, 400). Its own construction is not this producer. `41b4b0(Nexus,[64])` pays 50; Train 38 creates the real Probe child with stock images 137/138 and time 300. Complete `445070` callbacks finish and show that Probe at callback 302. No host production timer or readiness write advances it.

Queueing Pylon 156 costs no immediate additional minerals. ProbeBuild 31 moves toward (832, 528). In the baseline, state 1 becomes state 2 at callback 102; callback 117 runs real placement/payment and allocates a **new** Pylon CUnit. This is independent of the surviving Probe slot. Minerals 950→850, stock build time 450, raw HP 7,680 (30 HP), WarpAnchor Image 211 and primary Warp 48 are native outputs. Callback 118 resumes Probe's stock PlayerGuard 3. Issuing real Move 6 to (1000, 720) or real Probe Death after callback 117 does not pause the Pylon. Move reaches the requested point; Death actually recycles the Probe and restores its used supply 2→0.

`42ee00` state 0 starts motion through `43a070`. State 1 checks native distance 70, or 88 for Assimilator 157, then holds and enters state 2. State 2 waits for movement flag 2 to clear, rechecks distance, and either retries approach or invokes `41bcb0`. Ordinary success calls `424900`, clears the active build-queue slot to 228, replaces the new Sprite's images with 211, sets Probe target to the new building, assigns AI through `486200`, issues Warp 48 and enters state 3. State 3 resumes a queued/default Probe order; the selected non-Assimilator branch plays WalkingToIdle 12. Assimilator's replacement/WaitForGas 82 branch is static here (order 82 is WaitForGas, handler `4237a0`; HarvestGas is 83. Corrected after the function atlas, [P10 `0x42ee00`](atlas/p10.md)).

## Progress, temporary image program and three signals

Stock Pylon has 300 HP/300 shields, cost 100/0 and time 450. `427d90` initializes the u16 HP gain as the ceiling of `(max_raw_HP-floor(max_raw_HP/10))/DAT_time`, with minimum 1. The Pylon gain is 154. `427de0` sets initial shields to floor(max_raw_shields/10) and uses floor division for shield gain; Pylon gain 153. The regular shield timer also runs in full object callbacks. Native gain/clamp helpers, rather than a linear host interpolation, advance these fields.

Warp 48 state 0 calls `41a540` to reduce the WORD time, adds the WORD HP gain through `41b900`/`41bb50`, adds the WORD shield gain and clamps shields to the stock maximum. Cwal bit 2 statically multiplies progress by 16 with the original AX/WORD truncations; no positive Protoss Cwal case is claimed. The completion stages are separate:

| Baseline callback | Original output |
| --- | --- |
| 117 | New Pylon/Warp 48 state 0/time 450/Image 211 |
| 568 | Timer was zero at entry; original SpecialState 1 animation 13; state 1 |
| 572 | Stock signal bit 1 consumed; normal images 189/191 restored; `411840`; state 2 |
| 590 | Next signal bit 1 consumed; images restored and WarpIn 21 started; state 3; overlay 190 exists |
| 637 | Overlay 190's draw method 17 reaches its native termination/signal path; signal 1 remains for the next order call |
| 638 | Third signal consumed; actual completion, order 164, supply provided 34 |
| 639 | Actual InitPsiProvider 164 creates aura/list links and resumes order 23 |

`411840` temporarily selects **IScript ID 193**, not Image 193. It starts that program's Init, restores the ordinary image's IScript header, selects draw method 12 and continues the existing VM state. The program counter and selected animation header can therefore originate from different programs. A port must not restart Init merely because the header is restored. The stack 210 value is not an image allocation performed by this body.

Native draw method 17 begins parameter 48/timer 2. `40e650` decrements a nonzero byte timer; otherwise sets it to 2 and increments parameter while it is below 63. At 63 it starts the stock Death program if needed, ORs signal 1 into the current **Flingy** context `543304+4f`, and marks the image dirty. The baseline reaches this via 48 real updates of overlay 190. Original VM end and linked-image removal release it before the subsequent order call consumes the signal. Timer 0 alone is not completion; signal and order/image traversal are part of the contract.

## Cancellation and failure distinctions

Cancellation at callback 117, at timer 0 callback 567, and in state 3 callback 590 calls complete `41a340` on the real unfinished Pylon. All refund 75 minerals: 850→925, independent of progress. Original Death and stock image termination return the Pylon slot; complete selected object callbacks end at 129, 579 and 618 respectively. The Probe survives, Nexus provided 18 and used 2 remain, and the psi-provider list is empty. Cancellation, accounting removal and physical pool return are distinct phases.

A separate selected `41dfc0` call applies raw 400×256 weapon 0 damage during the real state 3/time 0 at callback 590. It uses no host HP/shield write and does not claim an attack/projectile producer. The Pylon finishes with **51,354 raw HP** (200 HP plus 154/256) and **496 raw shields** (49 subsequent 7-raw regeneration steps plus 153). The original completion branch falls through to the common progress tail and adds one final HP/shield gain. It preserves damage instead of assigning maximum health. Natural completion sees build time 0; the shared forced prefinish `424f40` only assigns maximum HP/shields when its time is still nonzero. Timer-zero warp completion and forced early completion therefore have different health contracts.

The resource boundaries are exact: Probe 49 rejects, 50 accepts/debits to 0; after genuine paid Probe production, Pylon 99 rejects and 100 accepts **without debiting at admission**. In the competing-payment case, initial 150 pays the first Probe 50, admits Pylon at balance 100, then a real second Probe queue consumes 50. Physical Build 31 reaches its late resource check at callback 117 with balance 50. It resumes idle without creating/debiting a Pylon, but **retains queue ID 156**: the rejection branches before `42ef3e`'s clear.

Finite two-unit or two-Sprite pools let real Nexus/Probe production complete but prevent the later Pylon allocation. The native placement/payment succeeds first, consumes 100, and the failed allocation then clears queue 228 and resumes idle through the real feedback guard. Balance remains 850; this consumer does not implicitly refund. These are allocation/consumer contracts. Upstream player command cap admission has not been executed and must not be inferred to allow such a command under every retail UI condition.

## Town AI, provider maintenance and power loss

An actual paid Probe first completes real Move 6 to (832, 528). Original `486a20`, `482ee0`, `487030` then produce a Town and WorkerAI kind 2. Controller 1 is an explicit subsequent input. Three selected native Probe service sets allocate the Pylon at that position; `486200` creates BuildingAI kind 3 referencing the same Town. The **Probe's WorkerAI remains allocated and Town worker count stays 1**. Zerg's same-slot WorkerAI→BuildingAI replacement is a different contract.

Completion runs 522 selected building service sets: `43a5f0`, `43a320`, `439420`, `439310`, `418b20`, `418990`. Cancellation runs actual Death/removal, then selected pending image/recycler/Thingy calls; it does not continue active timers on a sprite-less pending unit. Both paths retain the original worker/Town relation. Completion keeps BuildingAI; cancellation releases it. This is complete original helper composition, without a full computer Town/world scheduling claim.

After ordinary Pylon completion, InitPsiProvider 164 inserts the CUnit at `665b54`, prev 0 at `+f8`, next-old-head at `+fc`, and updates the old head's prev. It creates Sprite 321 at the real building center, stores its pointer at `+d0`, sets aura elevation 1 and applies actual viewer/visibility gates. It sets dirty DWORD `665b60=1` and resumes stock idle. Original `430670` processes that dirty flag through the live active list and `430000`, then clears it and dirties UI helpers.

The connected power scenario pays 150 for Gateway 160 at (960, 528), within the stock mask of the genuinely linked Pylon. The Gateway finishes through stock Warp 48. Real Pylon Death removes provider links/aura, removes its 16 supply, and the next actual dirty pass sets completed Gateway Disabled bit `0x400` with the stock disable animation. A new paid Pylon restores the provider and native enable animations clear that bit. Final minerals 600, provided 34/used 2/cap 400; Gateway remains completed throughout the outage. There is no direct status, aura, power-head, supply or position patch.

Returned CUnits enter the native unused list **after the surviving head**. The replacement therefore uses the actual remaining free head, a different slot from the removed Pylon; the probe records both. Recreating a Pylon in the destroyed slot by host assumption would break the observed allocation order.

## Source leads, implementation constraints and remaining scope

Pinned [GPTP Protoss build orders](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/building_making/building_protoss.cpp) describe the same broad approach/payment/new-unit/autonomous warp stages; its modified event paths and 1.16.1 addresses are not substituted for native 1.17. The fetched source matches the clean pinned checkout, SHA256 `c2c732ed3f68cc17888de78c1119a679d9ad070660b2a541f67c24594ee87079`. [OpenBW's warp callback](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L15189) corroborates the 17-mode timer/parameter/signal structure; local instructions and actual stock consumers establish this report's behavior. Their agreement does not prove full reconstruction parity.

For Defold, keep numeric state/queue/timer, original fixed-point gain and byte/WORD wrapping, absolute IScript program state, generic Flingy context, image-list traversal, independent builder/building lifetime, deferred recycling, Town metadata and separate supply/power contribution. Keep late admission rejection distinct from paid allocation failure. Power is a maintained source-list/mask contract, not just distance from any Pylon-like object.

Remaining scope includes full mask allocator/bootstrap, genuine Nexus construction, Assimilator/geyser replacement and gas continuation, broader RequiresPsi footprints/admission, Cwal under Protoss consumers, other damage timings during construction, interrupted movement/placement, computer Town/frame/request scheduling beyond the selected phases, imported/rendered output and authentic campaign-map compositions. This selected batch does not execute WinMain, Windows imports, the complete outer accepted frame, UI command receivers, renderer or six-campaign gameplay.
