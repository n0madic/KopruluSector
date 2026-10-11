# Native 1.17 paid Refinery, physical gas and carried SCV

A Terran Refinery uses the existing geyser CUnit, while retaining the original SCV. The SCV physically constructs the building, keeps an ordinary carried campaign item through completion or cancellation, and can execute a complete gas entry/exit/deposit cycle after paid construction when unladen. Cancelled or destroyed gas buildings restore the same neutral geyser and its remaining gas quantity. This bounded final batch complements [Extractor](native-extractor.md), [carried-worker construction](native-carried-construction.md), [Terran construction](native-construction.md), [gas harvesting](native-gas-harvesting.md) and [mission UNIT](native-mission-units.md). Their frozen scripts and evidence remain unchanged.

## Original execution evidence

The target is original StarCraft PE **1.17.0.1**, preferred base `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs; subtract `0x400000` for RVAs. No address is transferred from 1.16.1.

[The execution report](evidence/native-refinery-probes.json) contains **4 passing complete cases, zero prefixes, zero bounded fragments and zero helper stubs**: **1,587 complete original top-level calls**, including **1,534 complete `445070` object callbacks**, **10,424,711 original instructions**, **17,687 distinct instruction addresses** and **1,824,883 guarded guest writes**. Runtime is Rosetta x86_64 Python 3.9.6, Unicorn 2.1.4 and pefile 2024.8.26. All **114 imported execution-source hashes** were checked against the published files. The complete game, WinMain/process startup, Windows imports, guest file/network access, renderer and audio backend do not execute.

[The probe](../../tools/research/probe_native_refinery_117.py), SHA256 029cf2726908cf74c43cbfa8b2f894d305a8ba3bb1a55a2272c31bb439984927, executes unchanged original code and actual native callees on authenticated stock DAT/IScript/GRP/LO resources. [The inspector](../../tools/research/inspect_native_refinery_117.py), SHA256 e464edd449b4a34965b8aeafb413ba60b8de982c1ab857af5a930c805ec427f5, produces [21 static bodies and 1,645 instructions](evidence/native-refinery-static.json), with exact bytes, file offsets and RVAs. Every original top-level call returns with the expected native stack cleanup within its 1,000,000-instruction bound.

After original unit creation, the host does not write progress, type, HP, order/stage, heading, animation signal, carry pointers, location, queue, resource amount or balance. Actual original queue/position/target-order/cancel/Death consumers supply subsequent requests. The external frame-number input`52651c` advances before each object callback. Slow-service timers are guarded; outer accepted-frame elapsed time, UI command parsing and whole mission scheduling are separate.

## Authentic gas source and explicit caller world

Each run authenticates all **68 physical CHKs** against [the retail manifest](evidence/retail-manifest.json) and audits **21,427 UNIT records**. This raw physical corpus coverage is separate from the 64 supported required-section loader profiles and does not identify campaign progression.

The source is unchanged Zerg02 UNIT section0, record14: Geyser188 at`[960,256]`, owner11, quantity4851. Map SHA256 is `5819629c7bf99c941ab64225de24e4aa60c5164ad5c00345258aa62fe6a0eadf`; complete 36-byte record SHA256 is `c18c4e7ffb3f7305196a9ce482d1d352a9689e8ecf9de4055ff4da3f22dfd025`. Actual mission creator`469460` creates its unchanged type/position/owner and actual`46a420(record+12)` applies the original properties/amount. The host never writes CUnit gas quantity.

The SCV starts at`[768,256]`, owner3. In carrying cases original`469460` also creates a neutral Chrysalis216 at`[776,256]`; automatic pickup occurs naturally during a complete object callback. These positions/race are explicit comparison-scene inputs, not a claim that this was the played Zerg02 mission. The unladen gas scene creates an actual Command Center106 at`[512,256]` before construction. Human controller2, initial supply/resources and empty AI-region arrays are declared before creation.

The world has 128×128 tiles, 4096×4096 pixels, a flat one-region graph, empty contours, declared clear tile flags`0x1f700`, zero creep-info words and finite original CUnit/sprite/image/order/path pools. The independent WORD search rectangle **`543228=[0,0,4096,4096]`** is supplied before any creation, in addition to pixel/tile dimensions. Nearest-depot`431f00` uses that rectangle through actual`442c20`; dimensions alone do not initialize it. No caller geometry is repaired after construction or harvesting starts. The same genuine 96×96 Zerg02 MTXM rows are available in explicitly padded 128-stride private backing, without importing the actual mission terrain/topology or claiming positive creep generation.

Resources reuse [the frozen CarriedConstruction asset manifest](evidence/native-carried-construction-assets.json), SHA256 `39cab4a7dc794c7f5589d434a0f92d12917d123b460a793b8ba8b6f58bfd1658`: 276 image IDs, 239 GRPs, 301 LO bindings and 297 unique paths, 4,334,397 private payload bytes. It already contains the entire 109-image gas-harvest set, including SCV gas overlays 403/404 and stock refinery/geyser graphics. DAT/IScript/GRP/LO and genuine [network.tbl inputs](evidence/native-mission-notification-assets.json) are authenticated; no retail payload is published.

## Paid in-place Terran construction

| Original body | Preferred VA range, end exclusive | Selected connected contract |
| --- | --- | --- |
| PlaceBuilding30 | `41a580..41a762` | Destination, late admission/payment, gas branch and SCV/building binding |
| Queued build validation/payment | `41bcb0..41bd63` | Actual resource/supply and full placement predicates before debit |
| BuildRefinery | `41b3f0..41b4ad` | Geyser lookup, in-place ownership/type/accounting and overlay |
| ConstructingBuilding33 | `41a870..41ad04` | Physical SCV progression and completion |
| Terran building44 | `41a770..41a854` | Original building state/animation consumer |
| Cancel construction | `41a340..41a4aa` | Selected incomplete-building refund and original Death issuance |
| Special gas release | `425270..42539f` | Restore original CUnit to completed neutral188 |
| Clear waiting workers | `423400..42347b` | Selected empty waiting list during completed Refinery Death |
| WaitForGas82 / HarvestGas83 | `4237a0..423805` / `4235c0..42379c` | Native queue, hide/timer/exit/cargo/release |
| Cargo extraction / deposit | `423fd0..424071` / `427480..427513` | Actual quantity8 and resource/accounting changes |

Actual queue admission`41b4b0(SCV,110)` succeeds without paying the construction cost. At the destination, PlaceBuilding calls`41bcb0` at`41a60a`. That original body checks stock resource/supply through`4086b0`, calls actual complete placement`412f10`, and only on accepted placement calls debit`408650` at`41bd53`.

At`41a67a`, queued type110 takes the gas-specific branch: **`41a682 → 41b3f0`**. Other ordinary Terran building types take the separate`424900` allocator branch. BuildRefinery returns the existing Geyser CUnit after native completed-accounting removal, ownership transfer and stock type change. Its resource amount is preserved. PlaceBuilding writes the SCV into building+`80`, stores the building in SCV+`5c`, clears the consumed build queue to228 and starts original orders33/44. The selected real`486200` construction-AI consumer executes under human controller2; populated computer/Town assignment is not claimed.

The **CUnit is preserved; the Sprite is not required to retain its address**. Native stock type changes replace the genuine resource sprite/images. Thus the finite-CUnit proof below does not establish success with exhausted sprite/image pools. Image allocation, LO placement, IScript construction animations, native spatial/list updates and HP/time gains execute through actual original helpers. No host construction counter or completion signal advances the build.

## Four complete scenes

Indices below count complete object callbacks after the respective construction/harvest request. They depend on these positions and native initial state, rather than being universal wall-clock durations.

| Scene | Native milestones | Result |
| --- | --- | --- |
| Paid Refinery → physical gas → completed Death | Conversion40; building completion642; Harvest79 request, hide30, exit66, deposit147; neutral geyser restored after1 later Death callback | Minerals900; current gas1008/cumulative8; same gas CUnit type188/owner11, amount4843;3 live CUnits |
| Carrying SCV, complete Refinery | Conversion39; completion641 | Minerals900; same completed110/owner3, amount4851;3 CUnits; original Chrysalis remains hidden/carried, resource4 |
| Carrying SCV, cancel Refinery | Conversion39; actual cancel59; gas restored after1 later callback, SCV leaves Constructing33 for ResetCollision150 after2 | Minerals975; same188/owner11/full HP, amount4851; original SCV/carry retained |
| Carrying SCV, genuine late cost failure | Admit110 at100; real Barracks/Marine queue pays50; late construction check rejects remaining50 | No conversion/debit/Drop; Geyser188 remains4851; queue110/carry retained; SCV idle3 |

The first two scenes initialize exactly three CUnits. Original creators consume SCV/Geyser/CommandCenter or SCV/Chrysalis/Geyser respectively, leaving the actual free CUnit head0 before placement. Paid Refinery conversion and full completion both succeed with that head still0; there is no authored1700-count cap or free-list substitution. The retained three objects prove the gas branch's in-place allocation behavior through its caller.

The gas scene begins with current gas1000 and cumulative0. Native order progression is **79→82→83→151→84→81**. The first hidden returned snapshot is harvest callback30 with the timer already decremented in the same complete object callback. Exit callback66 shows the SCV at`[892,288]`, with stock cargo8 and overlay403. It releases active refinery service, physically returns to the Command Center and deposits at callback147, center`[582,288]`. Actual extraction changes4851→4843; actual deposit makes current/cumulative gas1008/8 and removes/clears cargo. No direct extraction/deposit call substitutes for this physical sequence.

Actual Death issuer`42a4d0` then acts on the completed paid Refinery. A complete subsequent object callback runs original completed-resource cleanup, real`423400` on the genuinely empty waiting list and special release`425270`. It restores the **same gas CUnit** to188, owner11, full stock HP`100000*256=25600000`, completed status and unchanged remaining4843. A new genuine geyser sprite/image profile is allocated by the type change. The SCV remains alive with no cargo; its observed order is MoveToGas81, stage3. This scene stops after restoration and does not claim later neutral-source retargeting, another gas cycle or a nonempty queue-destruction contract.

Cancellation uses actual`41a340` while the gas building is incomplete. The reached original`408490` refunds floor(100×3/4)=75, giving 1000→900→975. Actual Death restores quantity4851 and the neutral gas source. The same SCV remains allocated; no replacement worker is created. That differs from the [Extractor cancellation](native-extractor.md), which attempts a separately allocated Drone and can fail on an exhausted CUnit pool.

All three carrying scenes have genuine reciprocal carry pointers and hidden stock item state. No Drop entry with the actual carried-item pointer occurs. Completion leaves the SCV in native ResetCollision150 at the recorded endpoint; cancellation also naturally reaches150. No claim of later idle has been inferred from that intermediate order. In contrast, the Drone gas branch drops the item before removing the original Drone.

The real late-resource refusal does not edit minerals. A genuinely created Barracks111 admits Marine0 through actual`41b4b0`, paying50 after the already accepted Refinery queue. Secondary Train is not issued. The SCV naturally approaches the source, then its late stock check rejects100 because only50 remain. Queue110 remains, idle3 is reached, and the original carried item remains hidden and reciprocal. Other occupied-source, sprite/image exhaustion and initial admission refusals are outside this bounded batch.

## Authentic Bring and evidence limits

The carrying scenes use the exact Zerg02 trigger1/condition0: Current Player13, At least1 Chrysalis216, location1, flags0, original MRGN`[2368,2400,2464,2464]`, elevation mask0. Actual`4b9820` invalidates the native count cache and actual`4b8f20` evaluates the unchanged predicate. Current Player3 is declared before creation. The predicate is false before and after each scene because the construction area lies outside that rectangle. These are genuine campaign-parameter negative controls, not a whole TRIG/Victory run or an assertion that the map contains a Terran Refinery mission path.

All52 original source counters sum to total`673938`; state`67393c` independently matches `(state*22695477+1) mod 2^32` from seed 12345 for every case. Native randomness used by construction/IScript is retained; no intermediate state or heading is restored by the host. Instruction hooks are read-only observations.

The connected evidence closes selected human SCV gas-construction/carry/cancel/harvest/restoration consumers. It does not close populated Town/worker/building AI transfer, damage-sensitive construction/cancellation, nonempty active/waiting-worker destruction, source depletion, further retargeting, sprite/image exhaustion, occupied source competition, non-flat actual mission terrain, the complete UI/player command frontend, outer cadence or the full six-campaign remake. Stock state/animation/list outputs are observed, rather than rendered pixels or audio playback.

## Reproduction and stopping point

[General reproduction](reproduction.md) documents extraction, pinned sources and the emulator runtime. Equivalent authenticated private paths can replace the local paths:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_refinery_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out /private/tmp/refinery-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_refinery_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-carried-construction-assets \
  --asset-manifest docs/research/evidence/native-carried-construction-assets.json \
  --notification-assets /private/tmp/koprulu-mission-unit-assets \
  --notification-manifest docs/research/evidence/native-mission-notification-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --out /private/tmp/refinery-probes.json \
  --execute-original-code
```

Without the execution flag the script prints a plan. Groups `build`, `carry`, `cancel` and `failure` select the four independent cases. The final published all-run is complete and source-fresh; scripts/evidence are frozen at the requested research stopping point. No new direction is started after this bounded gas-building batch.

[Pinned OpenBW BuildRefinery](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L3034) and [Terran placement](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L4600) describe the matching existing-geyser branch. [Pinned GPTP special gas release](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/unit_destructor_special.cpp#L145) is a structural restoration anchor. Those primary sources guide the comparison; exact 1.17 instructions and the complete isolated original consumers establish the results above.
