# Native 1.17 paid Extractor, geyser restoration and carried Drone

A paid Extractor uses the existing geyser CUnit. The original Drone drops its campaign item and is removed; cancellation creates a separate replacement Drone and restores the same geyser. Exhausting the CUnit pool does not prevent the in-place conversion, but it can prevent the replacement Drone during cancellation. These observations extend the frozen [carried-worker construction](native-carried-construction.md), [paid Zerg construction](native-zerg-construction.md), [mission UNIT](native-mission-units.md), [original resource/image allocation](native-image-initialization.md) and [portable goals](native-powerup-goals.md) contracts.

## Evidence and scope

The target is original PE **1.17.0.1**, preferred base `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs; subtract `0x400000` for RVAs. No address is transferred from another executable version.

The execution report contains **6 passing complete cases, 1 passing prefix, zero bounded fragments and zero helper stubs**: **1,371 complete original top-level calls**, including **1,280 complete `445070` object callbacks**, **8,437,034 original instructions**, **17,328 distinct instruction addresses** and **1,755,316 guarded guest writes**. The prefix scene also contains 702 complete preparation/continuation calls and one stopped final callback; its executed instructions remain in the total, while the stopped callback is excluded from the complete-call counts. Runtime is x86_64 Python 3.9.6 under Rosetta, Unicorn 2.1.4 and pefile 2024.8.26. The complete cases return through unchanged original functions with actual native callees and stock DAT/IScript/GRP/LO resources. One scene stops before a Windows import during final creep stamping. Its completed status bit is an intermediate native observation: the object callback has not returned. No imported helper is replaced, and no execution resumes after the stopped prefix. The complete game, process startup, Windows imports, guest files/network and presentation backends do not execute.

[The probe](../../tools/research/probe_native_extractor_117.py) has SHA256 d965c59b1a22cdade3dbedf2afd0a1a11a3e64e5d002742622a419640484d595. [The inspector](../../tools/research/inspect_native_extractor_117.py), SHA256 3a1374d6cff679582fe7f499f37dce8fa81d9d0394f177dc35d6bad91ad3b5b1, produces [25 static blocks and 1,306 decoded instructions](evidence/native-extractor-static.json), including exact instruction bytes, file offsets and RVAs. [The execution report](evidence/native-extractor-probes.json) records per-scene inputs, real call entries, intermediate states, RNG checks, private-memory writes and complete/prefix boundaries. All 113 frozen imported source hashes are embedded and independently checked at publication.

After genuine unit creation, the host does not write a queue, carry pointer, CUnit type/order/stage, position, heading, animation signal, HP, resource amount or balance. Original command consumers issue queue admission, construction position, cancellation and later Move requests. Only the declared frame-number input advances between object callbacks. Callback counts are finite observations in this caller world, not wall-clock time or universal frame durations.

## Authentic record and declared caller world

All **68 physical CHKs** are reauthenticated against [the retail manifest](evidence/retail-manifest.json), and all **21,427 stored UNIT records** are audited. This raw corpus count is separate from the 64 supported-map required-section loader profiles and does not define campaign play order.

The gas input is original Zerg02 UNIT section0, record14: type188, position`[960,256]`, owner11 and resource amount4851. Map SHA256 is `5819629c7bf99c941ab64225de24e4aa60c5164ad5c00345258aa62fe6a0eadf`; the complete 36-byte record SHA256 is `c18c4e7ffb3f7305196a9ce482d1d352a9689e8ecf9de4055ff4da3f22dfd025`. Actual `469460` creates that unchanged type/position/owner; actual `46a420(record+12)` applies its original properties and amount. No CUnit amount is authored by the host.

The carrying pair is a genuine stock Drone41 owned by3 at`[768,256]` and a neutral Chrysalis216 at`[776,256]`. Those short-approach positions are declared caller inputs, rather than original UNIT positions. Automatic pickup occurs in a complete original object callback. The authentic Zerg02 Bring condition is Current Player13, At least1 Chrysalis216, location1 with original MRGN rectangle`[2368,2400,2464,2464]`, elevation mask0. Current Player3 and native force/controller inputs are declared before creation. The unchanged fresh predicate remains false because this composition takes place outside that rectangle; it is a genuine negative campaign-parameter control, not a Victory or mission walkthrough.

The world uses the frozen construction profile:128×128 tiles,4096×4096 pixel rectangle, a flat one-region path graph with empty contours, human controller2, valid empty AI-region arrays, finite native CUnit/sprite/image/order/path/creep-life pools, current/known-creep tile flags`0x5841f700` and a separately declared zero creep-info-word plane. Initial resources and supply are caller inputs; native code performs all subsequent admission, debit and refund. Slow-service timers are guarded; the full accepted-frame elapsed timer is outside this composition.

The genuine Zerg02 MTXM is96×96, 18,432 bytes, SHA256 `1b0dd13d03a51290be7daeeb1999b70d9e4e935883cf582f1759027b79eb4310`. Each original row is copied into a private 128-tile-stride backing before any unit creation; outside cells are explicit zero padding. The separate backing/byte planes at globals`6ca7a4/6ca7a8` are valid declared inputs. This embedding does not import the actual mission's terrain, region topology or full creep bootstrap. A contiguous 96-stride copy into a 128-stride world would be a different and invalid input contract.

Stock resources reuse the frozen [carried-construction asset manifest](evidence/native-carried-construction-assets.json), SHA256 `39cab4a7dc794c7f5589d434a0f92d12917d123b460a793b8ba8b6f58bfd1658`: 276 image IDs, 239 distinct GRPs, 301 LO bindings and 297 unique paths, 4,334,397 private payload bytes. This includes Geyser main image344, gas overlay345 and Extractor image93. All DAT/IScript/GRP/LO files and the genuine [network.tbl provenance](evidence/native-mission-notification-assets.json) are authenticated. Retail payloads remain private; no replacement graphic or audio routine is used.

## Original in-place gas construction

| Native body | Preferred VA range, end exclusive | Contract |
| --- | --- | --- |
| BuildRefinery | `41b3f0..41b4ad` | Existing Geyser lookup, ownership/type/accounting and overlay |
| Drone Extractor branch | `43bf80..43bfff` | Remove original Drone; initialize construction on returned Geyser |
| DroneBuild26 | `43b900..43ba8d` | Late stock admission, debit, Drop and saved-origin relocation |
| Cancel Zerg construction | `43b380..43b4fe` | Integer refund and separate replacement Drone attempt |
| Special gas release | `425270..42539f` | Restore same CUnit to completed neutral Geyser |
| Extractor old-creep scan | `4c0960..4c0a78` | Selected zero-info-word restoration scan with real callback |
| Below-main overlay | `4152d0..415335` | Original image allocation and intrusive-list placement |
| BuildSelf45 | `43b6e0..43b8e3` | HP/time, animation stages and final completion/creep consumer |

BuildRefinery takes ECX=worker and a stack requested type whose low8 bits are consumed. It accepts worker+`5c` only if that target is type188; otherwise actual `4132b0` searches the worker's order-target center for a geyser. No geyser returns0. A UI selection cleanup branch is guarded by actual `47cd70`, reading byte`6751e8`; its mapped-PE zero is a declared selected profile, not a proved post-bootstrap invariant.

At `41b43d` actual `414580` removes the old completed accounting, then the function clears completed status. Actual `426ed0` and `426e80` change CUnit/sprite ownership to the worker. Actual `424ad0` changes the same CUnit to requested149. Original `422f00` removes image231; actual `4152d0` allocates image345 below the main image. HP becomes integer maxHP/10 through the original multiplication/division sequence. The resource-container amount4851 and the original geyser address survive this conversion. No new building CUnit is allocated.

DroneBuild26 performs actual late admission `4086b0` at `43b9ce` and debit `408650` at `43b9da`. A nonnull worker carry reference from `421600` invokes Drop at `43b9ee` before the Extractor branch at `43b9ff`. Actual `43bf80` obtains the gas CUnit, calls the selected native worker/building AI guard, removes the original Drone through `425700`, initializes Zerg construction through `43bf10`, and allocates another below-main gas overlay. It does not turn the Drone CUnit into149.

The caller retains the item in ESI. After the gas conversion, `43ba10/43ba1a` read item+`d2/d0`, and actual `43a270` at `43ba22` relocates it to that origin. In these short-approach scenes, Drop itself already chooses`[776,256]`; the later original origin relocation is still executed and observed. Reciprocal carry pointers and worker resource4 clear, and real Powerup97/Show leaves the item visible in the conversion callback. Read-only instruction hooks record this order without modifying state.

## Seven connected scenes

| Scene | Native milestones after construction request | Minerals / CUnits at observation | Carry/gas result |
| --- | --- | --- | --- |
| Finite 3-CUnit paid conversion | Land 1/2/47; DroneBuild 83; Drop/conversion/Show 84 | 950 / 3 | Same gas CUnit becomes149; old Drone receives Death; visible item at origin |
| Normal early cancellation | Conversion 84; actual43b380; gas restored after 1 later callback, old Drone reclaimed after 2 | 987 / 3 | Separate Drone created; amount4851 retained; natural re-pickup after 66 return callbacks |
| Exhausted-CUnit early cancellation | Same conversion 84; freehead0 before cancel; gas restore1, old Drone reclaim2 | 987 / 2 | Replacement creation fails; same neutral188 restored; item visible/unclaimed |
| Genuine late mineral failure |50 admitted; real Marine queue debit→0; destination reached83; late rejection84 | 0 / 4 | Geyser remains188; queue149 and hidden carried item retained; order150 |
| Missing geyser | Actual placement result8; idle after 66 | 1000 / 2 | No gas/conversion/Drop; queue149 and carry retained |
| Existing completed Extractor at target | Genuine preplaced149; placement result8; idle after 111 | 1000 / 4 | Existing building unchanged; queue149 and carry retained |
| Final completion-creep prefix | Convert 84;606 complete continuation callbacks;607th reaches import before return | 950 / 2 | HP192000, completed bit/order23 already set; amount4851 retained; final caller incomplete |

The finite conversion/cancellation scenes begin with exactly three available CUnits. Original creation consumes Drone, Chrysalis and authentic Geyser, leaving the real free head0. Conversion succeeds because the gas CUnit is reused; it does not use an authored1700 count-cap failure. On normal early cancellation, actual `424900` can allocate a fourth temporary CUnit for the replacement Drone; after original death/recycling, three remain. With no free node, the same real cancellation allocator fails, no replacement worker exists, and native death restores the gas while the old Drone is reclaimed, leaving two CUnits.

Cancellation is requested immediately after the complete conversion callback, before the next BuildSelf45 callback. Actual `408490` refunds floor(50×3/4)=37, so 1000→950→987. In the normal scene the newly created Drone completes through actual `424da0`; native saved-Drone HP is applied by `41bb50`. Its address differs from the original worker. Actual Move6 then approaches the original item; stock movement/turning/IScript and the original Powerup spatial query naturally pick it up on return callback66, at center`[804,284]`. No host carry, heading or location correction occurs.

The genuine late-cost failure starts with 50 minerals. Original Extractor queue admission succeeds without payment. Original mission creation supplies a Barracks111, and real `41b4b0(Barracks,Marine0)` consumes the 50 through the paid queue consumer. Secondary Train is not issued. The Drone naturally reaches DroneBuild26, rejects the now-unavailable 50 cost, enters ResetCollision150, retains queue149 and keeps the hidden carried item. Neither Drop nor conversion occurs. Missing-geyser and occupied-building cases similarly preserve queue149/carry, finish idle3 and retain 1000 minerals. The occupied producer is a separately genuine preplaced completed Extractor created through469460 at`[1280,256]`; it is not a claimed earlier competing paid construction or an authentic stored UNIT149 tuple.

During early cancellation, actual Death special release calls `4c0960` and the real false callback`4265d0`; the selected zero creep-info-word plane has no eligible group-one cells. The function then restores type188 through `424ad0`, stock idle images/animation, full HP`100000*256`, owner11 through native ownership helpers and completed accounting through`414060`. The original gas address and amount4851 survive. Positive prior visual-creep restoration is a separate unexecuted branch; this observation does not prove all post-stamping cancellation states.

## Completion bookkeeping and the precise creep boundary

The continuation scene genuinely advances stock BuildSelf45 and its IScript signals. Starting after conversion callback84,600-tick build time and the original animation stages progress without host stage or signal writes. After606 complete continuation callbacks, the 607th enters the final stage: native `424da0` marks the Extractor completed, sets order23 and leaves HP`750*256=192000`. Those state changes occur inside an object callback that has not yet returned.

At `43b8db` the same BuildSelf body calls `43c220(type149,x,y)`. Predicate `4c0790` excludes149 from its 131..146 provider catalogue; that alone does not prevent this consumer. At `43c23d`, the wrapper also tests the stock RequiresCreep bit`0x20000`; Extractor lacks that bit and proceeds through footprint stamping. Actual `43c130 → 4c0dc0 → 4c0ca0 → 4c0c40` reaches the original variant callback, then statically linked CRT rand `4d14d4 → 4d744c → 4d7464`. The first Windows call is GetLastError through IAT`4eb17c` at **`4d7466`**. The probe stops before that instruction.

No import or helper is emulated, and no later query/cancellation/callback runs from that stopped guest. The prefix records completed status, type149, owner3, HP192000, unchanged4851 gas amount, two live CUnits, visible uncarried item and the original import boundary. This is a partially executed final completion/creep producer; it does not prove completed return, later gas harvesting or renderer parity. The earlier [trigger-creep report](native-trigger-creep.md) independently documents the same CRT/TLS dependency and a separate pure CRT arithmetic suffix, without equating that suffix with a complete rand call.

## Reproduction and remaining dependencies

[General reproduction](reproduction.md) documents extraction and the x86_64 emulator environment. Equivalent authenticated private paths can replace these local paths:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_extractor_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out /private/tmp/extractor-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_extractor_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-carried-construction-assets \
  --asset-manifest docs/research/evidence/native-carried-construction-assets.json \
  --notification-assets /private/tmp/koprulu-mission-unit-assets \
  --notification-manifest docs/research/evidence/native-mission-notification-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --out /private/tmp/extractor-probes.json \
  --execute-original-code
```

Without the execution flag the script prints a plan. Groups `start`, `cancel`, `failure` and `creep` select independent scenes; `creep` retains its precise final completion-stamping prefix. The published all-run establishes all seven cases with 1,000,000 instructions allowed per top-level call. Main PRNG state`67393c`, total`673938` and all 52 source counters are independently checked against `(state*22695477+1) mod 2^32` from seed 12345 for each complete or stopped case. It does not restore an intermediate RNG state.

[Pinned OpenBW BuildRefinery](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L3034), [DroneBuild](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L6489) and [Extractor cancellation](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L2353) provide the matching structural in-place conversion/separate-Drone mechanism. [Pinned GPTP gas release](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/unit_destructor_special.cpp#L145) helps locate restoration. These sources are structural anchors; exact 1.17 instructions and original isolated consumers are the evidence for this version.

Remaining dependencies include final variant/TLS/resource-bootstrap completion, positive visual-creep restoration after stamping, later gas harvesting and resource depletion, other cancellation timing/damage states, computer/Town AI transfer, local-player placement/error UI, finite sprite/image failures during this specific producer, non-flat original mission terrain, full command admission frontend, outer frame cadence and the whole mission trigger/Victory/progression. CTF remains outside the six-campaign priority; no Flag delivery/scoring executes. This batch narrows original gas/carried-item contracts without claiming complete six-campaign or playable-remake parity.
