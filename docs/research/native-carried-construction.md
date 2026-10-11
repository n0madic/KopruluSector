# Native 1.17 carried-worker construction and campaign Bring

The original construction consumers distinguish races. A carrying SCV keeps its mission item while it creates, completes or cancels a Supply Depot, and also after a paid allocation failure. A carrying Drone drops the item before converting into a Spawning Pool, then relocates the item to its saved origin. Cancelling that construction restores the Drone; ordinary movement then naturally picks up the original item again and restores an authentic campaign Bring predicate. These are connected original-code observations, extending [portable campaign goals](native-powerup-goals.md), [Terran construction](native-construction.md), [Zerg construction](native-zerg-construction.md) and [Drop/Show](native-powerup-lifecycle.md). Their frozen sources remain unchanged.

## Evidence and scope

The target is the original PE **1.17.0.1**, preferred base `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses below are preferred VAs; subtract `0x400000` for RVAs. No 1.16.1 address is transferred to this executable.

[The execution report](evidence/native-carried-construction-probes.json) contains **7 passing complete cases, zero prefixes, zero bounded fragments and zero helper stubs**: **2,716 complete original top-level calls**, including **2,618 complete `445070` object callbacks**, **15,514,675 original instructions**, **18,107 distinct instruction addresses** and **3,011,438 guarded guest writes**. Runtime is x86_64 Python3.9.6 under Rosetta, Unicorn2.1.4 and pefile2024.8.26. All **111 imported research source hashes** were checked against the published files. Private mapped memory contains unchanged original code and authenticated stock resources. Process startup, Windows imports, guest file/network operations and the complete game do not execute.

[The probe](../../tools/research/probe_native_carried_construction_117.py) SHA256 is `00c601bb3bcf9456da2022d9d120db3d3923de66e2769dfbe80e7fa8f2331abe`. [The inspector](../../tools/research/inspect_native_carried_construction_117.py) SHA256 is `03b784599286eba6d64ae2944f789d61642cfbbe0eb9d725ede2f5cb97be9271`; [its static report](evidence/native-carried-construction-static.json) contains seven complete bodies and 758 decoded instructions, including file offsets and instruction bytes.

All carrier/item pairs are created by original `469460`, authentic CUnit/sprite/image pools, stock DAT/GRP/LO/IScript and real placement/completion. Automatic pickup occurs in the first complete object callback. Original `41b4b0` supplies actual queue admission; the host does not write a build queue. Original `42a340` supplies a position order. After creation no host write changes a CUnit/sprite carry pointer, order, type, stage, heading, position, animation signal, HP, queue or resource balance. Cancel and later Move requests are actual original command consumers, rather than host state replacements.

## Original construction/carry contract

| Complete body | Preferred VA range, end exclusive | Contract examined |
| --- | --- | --- |
| Terran PlaceBuilding30 | `41a580..41a762` | Position/placement, late cost debit, allocation and transition |
| Terran ConstructingBuilding33 | `41a870..41ad04` | Natural SCV/building progression and carry retention |
| DroneBuild26 | `43b900..43ba8d` | Admission at destination, payment, Drop and saved-origin relocation |
| Drone-to-building conversion | `43c000..43c031` | Original same-CUnit conversion and construction initialization |
| Carried-object getter | `421600..421618` | DAT Worker bit8, then worker+`c0`; non-workers return0 |
| Drop | `4272c0..4273b5` | Overlay removal, actual placement/relocation, order and reciprocal-pointer clearing |
| Powerup97 | `427290..4272bc` | Original Show if hidden, spatial worker selection and automatic pickup |

The Drone body first calls placement `412f10`. At `43b9ce` it calls real stock resource/supply admission `4086b0`; a failure branches to `43ba3a`, before debit or Drop. Success calls actual debit `408650` at `43b9da`. `43b9e1` then calls the worker/carry getter `421600`. A nonnull carried item invokes `4272c0` at `43b9ee`.

The original body saves the item in ESI across conversion. Ordinary building construction calls `43c000` at `43ba07`. Afterwards `43ba10` and `43ba1a` read item+`d2` and item+`d0`, and `43ba22` calls original relocation `43a270(item,originX,originY)`. The separate Extractor149 branch calls `43bf80`; that positive branch is outside this batch.

Drop first requires a nonzero resource byte+`e0` with its low two bits clear. Genuine pickup here creates value4; the original predicate is not restricted to exactly4. Drop removes the real carry overlay, chooses placement around the carrier through original predicates/placement search, relocates the item and issues Powerup97 for these non-Flag objects. It clears item+`d4`, worker+`e0` and worker+`c0`. For ordinary Pool construction, the caller then **overrides the Drop position with the saved origin**. The item is shown by a later original Powerup97 consumer in the same complete callback for the two positive scenes.

Read-only hooks at the original entry and instructions `43b9f3`, `43ba0c` and `43ba27` record this intermediate ordering. They never modify guest state:

| Original observation | Drone complete scene | Drone cancel scene |
| --- | --- | --- |
| Drop entry: type41, reciprocal carry, resource4 | Carrier `[384,304]` | Carrier `[2656,2448]` |
| After Drop, before conversion | Item `[384,304]`, origin `[88,304]`, pointers/resource cleared | Item `[2656,2448]`, origin `[2424,2432]`, pointers/resource cleared |
| After original conversion | Same CUnit type142, item still at construction point | Same CUnit type142, item still at construction point |
| After caller origin relocation | Item `[88,304]` | Item `[2424,2432]` |

The Terran PlaceBuilding/Constructing bodies do not contain this Drone Drop→conversion→origin sequence. The positive Terran scenes additionally verify that **no Drop entry with the actual carried-item pointer** occurs. A cancellation cleanup call on an unrelated building with zero carry is not interpreted as a carrying-SCV Drop.

## Seven connected scenes

Callback indices start at the first callback after the construction position order, unless a table cell explicitly describes the later return. These are finite fixture observations, not universal frame durations or wall-clock seconds. Gas stays1000 in every scene.

| Scene | Native milestones | Final minerals / CUnits | Item/carrier result |
| --- | --- | --- | --- |
| SCV7 + Chrysalis216, Depot109 complete | Debit/create53; complete655 | 900 /3 | Hidden item remains carried throughout; actual Move returns to Bring location |
| Same, Depot cancellation | Debit53; native `41a340` cancel74; original Death/recycling reclaims Depot | 975 /2 | 75 refund; carry remains; original movement restores Bring |
| Same, finite allocation failure | Two-CUnit pool consumed by carrier/item; payment53; no Depot; worker returns to idle54 | 900 /2 | Debit remains; queue clears to228; no Drop; carry remains |
| Drone41 + Uraj128, Pool142 complete | Convert/Drop/origin/Show93; completed Pool1301 | 800 /2 | Same worker CUnit becomes completed Pool; Uraj visible at origin, uncarried |
| Drone41 + Chrysalis216, Pool cancellation | Convert/Show84; native `43b380` cancel104; completed Drone restored105 | 950 /2 | 150 refund; item remains visible at origin; natural movement re-picks it after65 return callbacks |
| Drone41 + Khalis129, late resource failure | Pool admitted at200; genuine Barracks Marine admission debits50; original Drone reaches destination, then cost check rejects150 | 150 /3 | No conversion/Drop; queue142 remains, item hidden/carried, worker order150 at observation |
| Drone41 + Psi Emitter217, initial admission failure | Actual `41b4b0(142)` rejects199; no building position order issued;32 further callbacks | 199 /2 | Queue remains228; item remains hidden/carried; no payment/Drop |

For the finite Terran allocation failure, the free CUnit head is genuinely exhausted after creating the two existing objects. It is not an authored `1700` count-cap substitution. Payment happens before the unsuccessful original creation attempt; no building exists and no automatic refund is observed before the later return. This selected complete original caller path establishes the retained100 cost, rather than extrapolating from a raw allocator failure alone.

The late Drone failure also avoids a host balance edit. After actual Pool queue admission succeeds without payment, original `469460` creates a completed Barracks111. Actual `41b4b0(Barracks,Marine0)` admits and pays the50-mineral request. Secondary Train is deliberately not issued: the evidence proves the real paid queue consumer and resulting late Pool failure, not a Marine-to-completion composition. The Drone naturally advances through25→70→26, arrives at `[3968,3472]`, rejects the200-mineral cost at150, keeps queue142 and enters ResetCollision150. Carry state remains reciprocal and hidden.

The successful Uraj scene finishes with Pool HP`750*256 = 192000`, completed status, no worker carry pointer and two live CUnits. No replacement Drone is authored. Cancellation instead restores the original worker CUnit to completed type41 while preserving the separately allocated original item.

## Actual campaign Bring through movement and re-pickup

Every run reauthenticates all **68 physical CHKs** against [the retail manifest](evidence/retail-manifest.json) and audits **21,427 UNIT records**. The existing portable-object inventory of all seven stored Bring records and three destruction actions is included unchanged. This is raw physical corpus coverage, separate from the64 supported-map required-section loader profiles; neither number defines campaign progression or a complete mission walkthrough.

The three SCV scenes and the Drone cancellation use the exact Zerg02 trigger1/condition0: type216, location1, selector13 Current Player, At least1, no ignore flag. Actual MRGN rectangle is `[2368,2400,2464,2464]`, elevation mask0. This trigger stores Victory among its actions. The scene declares Current Player3, workerowner3 and neutral itemowner11 before creation; it does not execute the original owner loop, other actions or whole mission VM.

Original `4b9820` invalidates the real query cache, and complete `4b8f20` evaluates the unchanged condition. No host counter or query-result write is used. The carried ordinary object is counted under the worker owner by the original completed-count family; it is not an independently visible root. The three carrying SCVs make Bring true initially, false while outside, then true after genuine original Move6 and natural path/movement/IScript progression back to `[2416,2432]`. The later return takes82,76 and55 callbacks respectively in these scenes.

Drone cancellation first makes that Current Player condition false: the newly visible Chrysalis has owner11 and the restored Drone is outside the goal. The original position issuer requests `[2416,2432]`; native movement and the original powerup spatial query naturally re-pick the item on return callback65. At that observation the worker center is **`[2451,2448]`**, the saved item origin is `[2424,2432]`, the worker is already idle3, reciprocal carry pointers are restored, resource4 is set and the item is hidden. The spatial test includes unit extents; this is not evidence of exact arrival at the requested center. Fresh Bring becomes true without any host position, heading or signal change.

The completed Pool/Uraj scene evaluates the exact expansion Protoss04 trigger23/condition1: type128, location35, selector17 All Players, At least1. Its actual rectangle is `[32,256,128,352]`, elevation mask56. Initial Drone `[80,304]` and Uraj `[88,304]` are genuine creations, with itemowner6. Fresh Bring is true while carried inside and also after original Drop/origin/Show because the now-visible Uraj lies at its authentic goal origin. The late Khalis and initial Psi Emitter failure scenes use their authentic stored MRGN inputs as caller geometry, but do not claim executing those whole mission goals.

## Declared world and resource inputs

The world profile is prepared **before** unit creation:128×128 tiles,4096×4096 pixel rectangle, a flat one-region graph with empty contours, finite unit/sprite/image/order/path/creep-life pools, human controller2, self-alliance diagonals and all eight valid empty AI-region arrays. Terran tiles use declared flags`0x1f700`; Drone scenes use declared current/known-creep flags`0x5041f700`. Full terrain import or positive creep stamping is not implied. Original `425610`, `40a0e0` and `4b44a0` initialize selected native pools. The Brood War caller flag at `536720` is enabled before genuine Uraj/Khalis creation; it is distinct from an executed campaign bootstrap.

Resources and supply are initial caller inputs, then actual original admission/debit/refund routines change them. Stock worker creation contributes its genuine supply use. The host increments frame-number input `52651c` before each complete object callback. Slow-service countdowns are guarded, and elapsed game timer`5349d8` is not advanced by an outer accepted-frame call. Thus complete object/movement/construction/IScript progression executes, while the full accepted-frame timer, AI/trigger activation, UI input and presentation backends remain outside this composition.

The report independently validates the original main PRNG for every case: all52 source counters sum to `673938`, and original state`67393c` equals repeated integer recurrence `(state*22695477+1) mod 2^32` from initial seed12345. This checks the reached source-consumption composition; it does not restore or author intermediate RNG state.

[The new resource manifest](evidence/native-carried-construction-assets.json), SHA256 `39cab4a7dc794c7f5589d434a0f92d12917d123b460a793b8ba8b6f58bfd1658`, contains **276 image IDs,239 distinct GRP paths,301 LO bindings and297 unique GRP/LO paths**. Private payload size is4,334,397 bytes, aligned5,095,424 bytes within the declared6MiB guest asset arena. These are the authenticated union of the frozen Powerup Goals, Terran Construction and Zerg Construction resource sets, not redistributed retail bytes or substituted graphics routines.

Extraction uses the frozen [image-resource extractor](../../tools/research/inspect_native_image_assets_117.py), SHA256 `4e82f06230a943c7d225c751aac8db7d1ac9c7c47386a95f9915102a7c11a909`, and its pinned StormLib provenance. Each GRP/LO, all stock DAT/IScript and the authentic [network.tbl notification provenance](evidence/native-mission-notification-assets.json) are checked before execution. Native resource pointers contain the declared genuine inputs; no guest file-loading/import stub is installed. Image-state and stock animation progression are observed, not rendered pixels or audio output.

## Reproduction and remaining dependencies

Rebuild the private image directory with the image IDs in the published resource manifest and the frozen extractor. [General reproduction](reproduction.md) documents the original extraction, pinned source and x86_64 runtime setup. The current private paths can be replaced with equivalent authenticated local inputs:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_carried_construction_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out /private/tmp/carried-construction-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_carried_construction_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-carried-construction-assets \
  --asset-manifest docs/research/evidence/native-carried-construction-assets.json \
  --notification-assets /private/tmp/koprulu-mission-unit-assets \
  --notification-manifest docs/research/evidence/native-mission-notification-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --out /private/tmp/carried-construction-probes.json \
  --execute-original-code
```

Without the execution flag the probe prints a plan. `--case-group terran/zerg/cancel/failure` selects independent scenes; the Terran selector includes success/cancel, the Zerg selector includes the two positive Drone scenes, and the failure selector includes the three failure scenes. The published all-run is the complete seven-case batch.

[Pinned OpenBW `order_DroneBuild`](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L6462) contains the same structural payment→Drop→conversion→origin order. [Pinned GPTP powerup handlers](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/powerup.cpp) describe carried-pointer refusal and ordinary pickup/Show. These primary sources help locate the mechanism; exact original1.17 instructions and connected native calls establish this batch's behavior. Their other implementation choices are not accepted as universal original parity.

Remaining connected boundaries include carried Probe warp, Extractor/geyser conversion and failure, other race/building/cargo combinations, conflicting placement and non-flat actual mission terrain, computer/AI worker reactions, the complete player command/UI admission frontend, outer timer/cadence and entire mission trigger/Victory/progression execution. CTF remains outside the requested six-campaign consumer priority: the prior raw corpus has no UNIT/Create/THG2-unit-like Flag215 or flag-beacon197–199 producers, but this is not a proof of every possible runtime producer or CTF scoring. No CTF delivery/feedback is executed here. The batch establishes selected complete carried-construction algorithms and authentic Bring parameters; it does not claim all six campaigns are complete or playable.
