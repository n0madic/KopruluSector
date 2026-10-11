# Connected native AIScript startup, AI spending and Terran construction

The original AIScript VM now creates a Town and its Supply Depot request, the original player economy produces the spending entry, and the original SCV service chooses the position and construction order. Complete object callbacks then move the SCV, debit the player's minerals, allocate and construct the Depot, publish supply and attach its BuildingAi to the Town. Two positive chains and six rejection/retry chains extend the four startup profiles. No host-authored build queue, construction point, movement, HP, timer or BuildingAi assignment closes this chain.

The exact target is StarCraft **1.17.0.1**, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred base `0x400000`. Addresses are preferred VAs; subtract that base for RVAs. This connects the previously separate [Town lifecycle](native-towns.md), [AIScript opcodes](native-ai-opcodes.md), [economy](native-economy.md), [worker selection](native-workers.md), [AI placement](native-ai-placement.md), [spending](native-spending-dispatch.md) and [populated Terran construction](native-construction.md). Their frozen scripts and reports are unchanged.

## Evidence and fixture boundaries

[The original-instruction report](evidence/native-ai-construction-probes.json) contains **12 complete passing cases, zero prefixes and zero helper stubs**. Its actual execution comprises **1,538 complete calls and 19,165,638 original instructions**, with 462 visited range starts, 1,173 reviewed ranges and 79 authenticated source dependencies. Every selected call reaches the exact return sentinel with original stack cleanup. The runtime is Rosetta x86_64 Python3.9.6, Unicorn2.1.4 and pefile2024.8.26. The largest call executes4,589,226 instructions under an8,000,000-instruction bound: full positive placement must not be mistaken for a short leaf helper.

The [probe](../../tools/research/probe_native_ai_construction_117.py) SHA256 is `0a0dea7fc3c457e7c3e6e11b15476e29ab6ce7438a8f19f5dd61a75df20b964e`. The [inspector](../../tools/research/inspect_native_ai_construction_117.py) SHA256 is `fe0e074481cf65ae34b9491cb0c84e15abbbe6342953095d46f5d671a73a64dd`; [its report](evidence/native-ai-construction-static.json) contains16 blocks and926 decoded instructions without undecoded tails. Regional and unit-type dispatch tables are extracted as data, separately from instructions. These tables do not prove every destination's behavior.

Original DAT, IScript, GRP and LO inputs are hash authenticated. [Shared construction asset provenance](evidence/native-construction-assets.json), SHA256 `85085b5d52a740a9fb1510303b918562c009a7a692a254dcbb2630b0a86cdd71`, covers143 image IDs and122 distinct GRP paths. Original bytes stay in private scratch storage.

The caller supplies owner3, controller1 or2, diagonal self-alliance1, a flat4096x4096-pixel world with128x128 tiles, one populated region1, no obstacle contours, tile flags`0x1ff00`, an empty creep plane, finite native allocation pools and eight valid AI-region arrays. Actual `425610` normalizes the acquired DAT, `40a0e0` initializes Bullets, `486a20` initializes shared Town/WorkerAi/BuildingAi pools, `4b2640` initializes Guards and `482ee0` initializes campaign PlayerAi. Genuine `424900` creation and `424da0` completion provide SCV7 at512,512 and Command Center106 at768,512.

Script-pool initializer `493330` executes on the real pool at`6a1a18`; the caller separately binds global`6a2e6c` to that pool and clears active head`6a2e70`. That binding is an explicit loader fixture, not recovered full initialization. The script is synthetic stock bytecode, not a campaign program:

```text
56 start_campaign
3 start_town OR 4 start_areatown
6 build(count=1, unit=109, priority=80)
2 wait(1)
36 stop
```

The supplied rectangle is400,400,900,800. Full script creation `494d90` and VM `493a60` produce the Town, worker/main references, request, PC and wait themselves. The host never calls the Town creation or request insertion helpers directly in these cases.

No Windows import, WinMain, external process, sound/graphics backend or guest host-file/network operation executes. Code execution is restricted to reviewed ranges; memory-write guards reject executable-section changes and writes outside declared private memory. RNG state and source totals match an independent integer LCG. Actual map acquisition, generated terrain/visibility, full accepted-frame orchestration and campaign play remain distinct from this selected composition.

## Startup and request identity

Four profiles combine regular/area startup with controller1/2. Every profile creates a Town, attaches one WorkerAi and identifies the Command Center. The VM returns at PC10 with wait1. Controller1 produces request bytes`[8,80,109,0]`; controller2 leaves the slot zero through the actual owner gate. This is an intentionally invoked VM under each controller, not a claim that human players automatically run campaign AI.

The regular Town center is the rectangle midpoint650,600; the area Town overrides it with the actual main building's768,512 position. The packed request is a desired count1, kind0, special0 and priority80. It persists after a successful building and after every tested failure. The disposable spending heap is a separate structure.

## Budget refresh, full builder service and deferred payment

Owner3 starts with1000 minerals,1000 gas and native Terran supply `(provided,used,cap) = (20,2,400)`. The player-major availability byte`52655c + 3*228 + 109` is supplied as1. The fixture selects the next player through `67f7a8 = 2`, then executes complete `483400`.

Its actual `4823d0` refreshes available resources and supply, clears demand/the spending heap, and calls pending-worker reservation `487730`. Full request processing `4865b0` generates one priority80, kind3, ID109 entry pointing to the Town. PlayerAi's first six DWORDs become `[100,0,0,1000,1000,18]`: mineral/gas/supply demand followed by available mineral/gas/supply. Automatic supply/gas/Bunker/Turret helpers also execute their selected guard profiles; their other request-producing branches are not claimed by this report.

Complete `487580` attempts actual Terran repair/resume selection, then `485400` construction. The latter runs original requirements, race/resource checks and full `4962c0` placement. The regular/area outputs are respectively752,672 and944,608. Original order/queue helpers issue PlaceBuilding30 and write `[109,228,228,228,228]`. The host supplies neither output.

At this return boundary, the spending heap is empty and budget becomes `[0,0,0,900,1000,18]`, while **the player's current minerals remain1000**. Original `482bc0` consumes the available AI budget. Queue helper `41b4b0` defers current-resource debit for the building's DAT property bit1; later accepted PlaceBuilding placement pays the stock cost. Reserving AI budget and spending player stock are separate transitions.

## Native movement, construction, completion and AI attachment

Each subsequent step invokes complete `445070`, with the logical frame counter supplied externally. The host does not change the worker/building position, heading, queue, stage, HP, remaining build time or AI assignment. Slow-service countdowns at`6c46d4/69b568/69b564/6663e4` start at10,000; resource-area count is0. Native script and regional services still execute, but this is not all periodic AI services under natural generated world state.

| Startup | Original placement goal | First allocated building callback | Completed callback |
|---|---|---:|---:|
| Regular Town | 752,672 | 79 | 681 |
| Area Town | 944,608 | 111 | 713 |

Initial building HP is12,800 and time600, with current minerals900. The [construction contract](native-construction.md) determines the later native movement/welding,192-raw-HP progress and zero-on-entry completion distinction. Both positive chains end with completed HP128,000, time0, three live units, one completed Depot109 and native Terran supply36/2/400. Full elapsed times in seconds are not inferred from these selected object callback counts.

Original AI assignment attaches the new BuildingAi kind3 to the same Town. Its building list is `[new Depot, Command Center]`; the builder is the genuine SCV. The native script service consumes wait and Stop, leaving active script head0. A further complete `483400` leaves the persistent request `[8,80,109,0]`, creates no spending entry because the desired building now exists, and refreshes available supply34 and minerals900. A persistent request is therefore satisfied by world state rather than deleted on payment.

## Rejection and original regeneration

Six complete cases combine both Town starts with three failures. Each first executes actual script startup and complete player request processing, then full WorkerAi service. No mineral objects exist, so failed construction executes the genuine neutral mineral-count fallback and returns without a replacement resource order. Worker wait remains0 and stock queue remains five empty228 entries.

| Failure input | Heap/demand after worker service | Shared requirement error | Current stock |
|---|---|---:|---|
| Minerals99 | Entry retained; mineral demand100 | 0 | 99/1000 |
| Depot availability0 | Entry retained; mineral demand100 | 2 | 1000/1000 |
| Placement throttle1 | Entry removed; all demand0 | 0 | 1000/1000 |

Every case keeps Town request`[8,80,109,0]`, two live units, idle SCV order2 and no allocated Depot. An already set throttle makes placement return0; original failure service `485dc0` invokes `482c70`, which pops the spending entry without consuming available resources, then runs the original secondary request service.

A **second complete `483400`** regenerates the spending entry from the Town request in all six cases. Its demand is100 and available resources are the unchanged current stocks. The supplied throttle1 decrements to0 through this actual player service. These observations distinguish resource/requirement retention from placement-failure popping, and establish the native next-pass regeneration boundary; eventual retry under changed resources/terrain is not executed here.

## Defold implementation contract and remaining scope

The authoritative simulation needs separate persistent Town requests, disposable priority spending entries, demand/available budgets, current stocks, worker queue/position order and completed-world counts. Preserve the ordering from VM producer through budget refresh, prerequisites, placement, AI reservation, physical payment, construction and AI list attachment. Retry must regenerate from world/request state. A single boolean or one resource balance would lose the observed distinctions.

This closes the selected Terran Depot producer-to-completion example for R22/R23/R25/R26/R27. It does not certify all racial construction, resource-area generation, automatic expansion, mining-linked recovery, noncampaign strategy, every request/production type, multiple competing builders, placement obstacles, finite allocation failure or all original frame interleaving. Their existing ledger entries remain partial. No Lua implementation or complete campaign parity is certified by these native observations.

## Reproduction

After extracting the143 IDs from the shared image provenance with the unchanged image asset reader:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_ai_construction_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out /private/tmp/ai-construction-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_ai_construction_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-construction-assets \
  --asset-manifest docs/research/evidence/native-construction-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --out /private/tmp/ai-construction-probes.json \
  --execute-original-code
```

Without the execution flag, the probe prints its plan only. Private input hashes and pinned runtime prerequisites are recorded in [reproduction](reproduction.md). The repository publishes contracts, probes, instruction metadata and provenance, with original resource payloads kept private.
