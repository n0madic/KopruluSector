# Native 1.17 populated harvesting, queues and complete mineral return

This batch follows genuine SCV7, Drone41 and Probe64 through the original whole-object callback from a Harvest79 order to movement, mining, return-to-depot and actual mineral accounting. Separate complete original calls establish cargo extraction/deposit, mineral animation thresholds, depleted gas, populated waiting queues, the computer-worker bonus and Hatchery/Lair/Hive deposit-side weights. These contracts extend [economy](native-economy.md), [worker AI](native-workers.md), [primary orders](native-primary-orders.md), [physical movement](native-movement-fsm.md) and [unit blockers](native-blocked-movement.md).

The executable is StarCraft **1.17.0.1**, preferred ImageBase `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses below belong to this exact artifact. [Static evidence](evidence/native-harvesting-static.json) preserves **65 reviewed ranges / 3,560 decoded instructions**, without undecoded tails. [Probe evidence](evidence/native-harvesting-probes.json) preserves the final single all-run, exact complete-call counts, stack-checked returns, original callees/IScript/impact/movement traces, independent state comparisons and source-counter/LCG validation. [Private asset provenance](evidence/native-harvest-assets.json) covers **98 image IDs / 81 distinct GRP paths** and selected real LO inputs; no retail payload is published.

**879 complete cases pass**, with no prefixes or substituted helpers: **5,630 complete direct returns / 25,180,724 original instructions**, maximum single invocation77,129. The final report records843 reviewed ranges,358 visited function starts and64 imported source hashes. Counts describe these contracts rather than a percentage of game coverage.

## Execution boundary and declared world

The [inspector](../../tools/research/inspect_native_harvesting_117.py) reads PE instructions as data. The [probe](../../tools/research/probe_native_harvesting_117.py) executes only with `--execute-original-code`; otherwise it prints the plan. Every executed entry must return through the actual function ABI, remain within reviewed original ranges and complete within100,000 instructions. There are no helper stubs, Windows imports, WinMain/process startup, guest host-file/network access, rendering or full accepted-game-frame orchestration.

All workers, resources and depots are created/completed/shown through real **424900 → 424da0** with authenticated stock DAT, IScript, GRP and LO. Original code maintains their Sprite/Image chains, lists, counters, fixed-point positions and spatial indices. Private finite pool capacities are explicit. A clear same-group region raster, no contours, visible tiles, owner3/human controller2 and valid empty player-region arrays supply the world. The map has4096×4096 pixels,128×128 tiles and the separate WORD rectangle **543228 = [0,0,4096,4096]**. Setting width/height fields alone does not populate that search rectangle.

The creep record plane is a declared128×128 zero-WORD plane, consistent with no creep flags in the selected tile input. Hatchery's real primary/secondary consumers execute; their no-creep spawn/rank gates return normally. This is not a positive larva-spawn or new-creep-stamp composition. The Hatchery exists through direct original construction in this prepared world, without claiming original placement admission.

## Complete original call shapes

| Entry | Caller input and result |
|---|---|
| `423e40` | ECX=resource; EAX carries extracted quantity. |
| `423fd0` | ECX=resource, stack=[worker], `ret 4`; remove previous resource cargo, extract, choose stock overlay and store cargo. |
| `427090` | ECX=worker, stack=[imageID,quantity,isMineral], `ret 12`; guarded cargo setter. |
| `427410` | ECX=worker; remove populated397..404 resource overlays. |
| `427480` | ECX=worker, one ignored resource-pointer stack argument, `ret 4`; deposit/account/clear cargo. |
| `482f40` | ECX=worker; computer bonus predicate. |
| `424230` | ECX=worker, stack=[resource], `ret 4`; start service or insert into waiting list. |
| `423ed0` | ECX=resource; unlink and return the selected eligible waiter, or zero. |
| `4240d0` | ECX=worker, stack=[resource], `ret 4`; occupy resource and emit83/87. |
| `424130` | ECX=worker, stack=[resource], `ret 4`; release current worker and start the next eligible waiter. |
| `424170` | ECX=resource, stack=[worker], `ret 4`; cancel waiting or active membership. |
| `43da30` | ECX=Hatchery-family depot, stack=[worker], `ret 4`; decay/increment deposit-side words. |
| `445070` | Complete original whole-object callback; updates real workers, depots, images and Bullets. |

Preferred virtual addresses are written without a leading0x in prose; subtract`0x400000` for RVAs. Generic logged stack slots are raw observations, not an implied ABI for every nested helper.

## Extraction, low reserves and overlays

The matrix creates each of three real worker types against each of Mineral176/177/178 and Refinery110/Extractor149/Assimilator157. Resource WORD`+d0` is explicitly initialized to0,1,2,7,8,9,15,16,257,258,507,508,757,758 or1500. This isolates the exact threshold/low-reserve branches without claiming those amounts came from a loader.

| Source state | Native quantity | Native remaining amount / lifecycle action |
|---|---:|---|
| Minerals with amount≥8 | 8 | Subtract8; issue real Death0 when the result is0. |
| Minerals with amount<8 | amount | Issue real Death0; the resource WORD retains that amount in this leaf. |
| Gas with amount≥8 | 8 | Subtract8; when the remainder is <8, execute the original depletion-warning owner gate `4461f0`. |
| Gas with amount<8, including0 | 2 | Set resource amount0; no warning call. |

An exhausted gas building continues to yield2. A low mineral resource's unchanged WORD does not authorize another extraction after Death; these leaf cases do not execute resource reclamation. Amount0 minerals produce no cargo/image. Warning code **4461f0** (`ECX`=owner, `EDX`=20, stack 875) returns on the genuine nonlocal-owner branch (owner3 vs local0), without substituting its UI/sound consumer. It is called at `423e93` only on the subtract-8 gas path when the remainder is below 8 (initial amounts 8, 9 and 15 in the matrix); the recorded calls agree. Corrected after the function atlas ([P08 `0x423e40`](atlas/p08.md)); the table previously attached the warning to the amount<8 row.

Actual **4247b0** changes mineral animation according to the *remaining* amount:≥750→20,≥500→15,≥250→14, otherwise13. It updates cache byte`+d2` and starts real Sprite animation only when that byte changes. The257/258,507/508 and757/758 inputs distinguish post-subtraction sides of250/500/750. The complete run independently checks the cache. Death issuance at amount≤8 takes the separate destruction branch rather than this remaining-amount animation update.

| Resource | Large / small carried image |
|---|---|
| Minerals176..178 | 397 / 398 |
| Refinery110 | 403 / 404 |
| Extractor149 | 401 / 402 |
| Assimilator157 | 399 / 400 |

Large/small choice uses extracted quantity≥8 / <8 **before** the computer bonus. Actual allocation runs stock image initialization, IScript, direction, LO cargo offset and overlay flags. The image allocation and gameplay cargo write are distinct: a genuinely exhausted Image pool still produces the native cargo quantity/type while leaving no visual overlay.

Direct **423fd0** first removes previous resource cargo when its carried-type has resource bits. A second direct service replaces the old cargo; it does not silently deposit it. Twelve native replacement cases cover mineral→mineral, mineral→gas, gas→mineral and gas→gas for all three workers. Direct **427090** has a separate guard: any already nonzero carried-type prevents its requested replacement, retaining existing quantity/image/free-pool state. These are direct consumer contracts, not a normal player command sequence allowing simultaneous cargo.

## Native deposit and computer bookkeeping

For a nonzero genuine cargo, complete **427480 → 427410 → 482f00** removes populated images and changes the owner's actual resource counters:

- minerals: current`5263d0`, cumulative`526460`;
- gas: current`526400`, cumulative`526430`;
- player offsets are owner×4;
- AI resource totals at player structure`67d074/67d078 + owner×4e8` receive the same amount;
- carried quantity/type become0; UI dirty writes execute normally.

If both resource-type bits are present, the mineral-bit branch has priority. Ordinary positive matrix cases use genuine unambiguous mineral/gas cargo. The function's stack pointer argument is not used to identify a destination or calculate quantity; depot selection belongs to the return-order consumer.

## Computer-worker bonus uses genuine completed counts

Original **482f40 → 486070 → 4939e0** requires a nonnull worker AI pointer, AI mode byte`67d297 + owner×4e8` equal2 and at least12 completed workers of the owner's native race. The three race profiles create and actually complete11/12/13 workers. Completed counters are produced by original lifecycle code, not written to manufacture the threshold. Modes0/1/3 and absent AI-pointer cases reject the bonus.

The mode byte and opaque AI-pointer presence are explicit predicate inputs. Their existence is not a complete original Town/worker allocator composition. Native worker assignment is documented separately in [worker AI](native-workers.md).

With the threshold met,1500 minerals become1492 while cargo becomes9. Extra0/7/8/9 source-amount cases for each race distinguish low-resource behavior: zero extraction still creates no cargo; extraction7 becomes cargo8 with **small image398**; extraction8 becomes cargo9 with **large image397**. Extra cargo does not subtract an extra source unit. Overlay selection precedes the bonus addition.

## Resource queues: head insertion, oldest eligible service

Eighteen cases genuinely create three SCVs and either Mineral176 or Refinery110. Actual target-order issuer **42a380**, stack=[86 or82,resource], followed by **424230** establishes membership. The first worker owns the resource and gets Mining87/HarvestGas83. Later workers prepend to resource head`+d4`, with worker previous/next links`+d4/+d8` and harvest target`+d0`. The resulting waiting chain is third→second.

**423ed0 scans the complete waiting chain and retains the last eligible worker.** On an ordinary list this selects the oldest inserted waiter, yielding FIFO service despite head insertion. Eligibility requires primary order82 or86, clear status400 and zero Irradiate`+117`, Stasis`+119`, Lockdown`+124`. A disabled older waiter lets the newer eligible worker start. With both waiters disabled no worker starts; the list remains. A bad-order newest waiter does not displace the older valid one.

Cancellation uses actual **424170**. Removing a waiting worker keeps resource busy with the active worker; canceling the active worker releases it and starts the oldest eligible waiter. Complete **424130** clears the old worker's active byte`+ce`/resource pointer, decrements resource busy byte`+d3`, unlinks the chosen waiter, removes queued StopHarvest89 and invokes genuine **4240d0**. All bidirectional links and busy/active ownership are checked. Status/timer rejection inputs are explicitly authored fields; this queue matrix is not a claim that its disabilities were produced by spell casting.

## Whole-object mineral movement, mining and return

Each case creates a worker at`(512,512)`, Mineral176 at`(768,512)` with1500 units and its depot at`(256,512)`: SCV/CommandCenter106, Drone/Hatchery131, Probe/Nexus154. It issues real Harvest79 through **42a380** and then repeatedly executes full original **445070**. The host advances the declared accepted-frame-counter input **52651c**, but never writes worker positions, speed, heading, order state, mining stage or cargo between callbacks.

The original chain reaches Harvest79→MoveToMinerals85→WaitForMinerals86→Mining87→ReturnMinerals90, with inserted ResetHarvestCollision151 (handler `42e0d0`; ResetCollision is order150, corrected after the function atlas, [P08](atlas/p08.md)/[P10](atlas/p10.md)) and actual movement/path/Sprite/index/image work. Real mining timers and VM instructions execute; no empty animation substitute produces cargo. In particular the SCV's stock mining VM reaches explicit useweapon14 and creates/updates the actual mining-flash Bullet. The Probe's stock mining VM reaches useweapon63 and Image520; all required resources are authenticated rather than ignored.

Nearest depot search uses **431f00 → 442c20 → 442d90**, the real spatial indices, map rectangle, candidate predicate **4323d0**, depot availability **424460** and same-ground-group/reachability gates. Travel and arrival then reach real **427480**. Endpoint, Sprite, fixed coordinates and all four spatial borders agree throughout; both index arrays remain sorted. Ambient unit/Flingy/Bullet contexts return to zero after every complete callback.

| Worker / depot | Complete whole-object callbacks to the first deposit |
|---|---:|
| SCV7 / CommandCenter106 | 251 |
| Drone41 / Hatchery131 | 242 |
| Probe64 / Nexus154 | 242 |

Every final state is current/cumulative minerals8/8, resource1492 and worker cargo0/type0. These are exact measured callbacks for this geometry and declared clock, **not a universal throughput, real-time seconds or campaign frame-rate claim**. Initial readiness, turn, movement and final depot contact contribute to the count. No full map loader, mission AI scheduler, player command admission or campaign rendering runs.

## Hatchery-family deposit-side weights

Complete **43da30** operates on four WORDs: left`+d0`, above`+d2`, right`+d4`, below`+d6`. It first decrements every nonzero word. Worker Sprite position is classified against the depot's stock extents in priority order:

1. y strictly above the upper boundary→above;
2. y strictly below the lower boundary→below;
3. otherwise x strictly left of the left boundary→left;
4. otherwise→right, including an interior point and equality on the left boundary.

The selected decayed word gains25 only when its **signed16-bit** value is<100; addition wraps as a WORD. This is not a clamp to100. Thus initial100 decays99 and becomes124, initial101 decays100 and stays100, while65535 decays65534 (signed−2) and wraps to23 after addition.

264 cases use actual completed Hatchery131/Lair132/Hive133 and native-created Drones at eight discriminating boundary/side/interior positions. Initial words0/1/24/99/100/101/124/125/32767/32768/65535 are explicit inputs. The ordinary whole-object Drone deposit also reaches this consumer and records the resulting side words. The real larva-position consumer **43d360** reads these words when comparing spawn candidates; its instructions are reviewed. Positive larva creation, free-side blockage, cap/cadence and creep stamping require a separate populated composition.

## Reproduction and remaining distinct contracts

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/inspect_native_harvesting_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --out docs/research/evidence/native-harvesting-static.json
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_harvesting_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --data /private/tmp/koprulu-campaign-data --manifest docs/research/evidence/retail-manifest.json --assets /private/tmp/koprulu-harvest-assets --asset-manifest docs/research/evidence/native-harvest-assets.json --pyms /private/tmp/koprulu-pyms-campaign --out docs/research/evidence/native-harvesting-probes.json --execute-original-code
```

The final publication uses one all-run rather than merging filtered development runs. Probe SHA256 `d4c555bdab580ed3d31e36b8cc6c0518093dd41075a05eceaf692d6bda5e28cd`; inspector `64cb932c49dfe307a5bd5ed2c336d0dbad723539624664d1f39faaf2bf9f38fa`; asset manifest `0aa4a16a55cf957732e0145f3e2e4a329f491302784bf2d10f6571ff33d93419`. The JSON records all dependency/retail input hashes and runtime versions. Verification checks those witnesses for freshness; it does not re-execute the game.

The [gas follow-up](native-gas-harvesting.md) establishes selected complete enter/hide/timer/exit/return paths and no-depot emergence. Remaining distinct work includes positive exit failure/retry and gas-building destruction, multiworker physically moving queue/throughput, target depletion followed by actual mineral Death/reclamation and retargeting, disabled/dying workers during service, depot destruction/ownership/nearest ties, actual Town/AI selection/bonus-mode lifecycle, terrain-separated/blocked resources, positive larva/creep production and full accepted-frame/campaign interleavings. Selected complete contracts do not close those compositions or six-campaign parity.
