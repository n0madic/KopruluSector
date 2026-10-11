# Campaign Towns connected to natural WorkerAI and physical mining

Four complete compositions connect actual campaign resource/start/depot/worker records to original Town allocation and **naturally scheduled WorkerAI, movement, mining and repeated deposits**. After native Town/resource binding, the published run calls only complete original `445070` object updates. It issues no worker command and makes no direct `487580` call. Every genuinely created worker deposits at least twice; resource conservation and physical index geometry are checked after every callback.

Target: StarCraft **1.17.0.1**, preferred base `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Preferred VAs below apply only to this exact image. This connects [campaign resource generation](native-campaign-resource-areas.md), [Terran Town binding](native-campaign-resource-towns.md), [Protoss Town binding](native-campaign-protoss-towns.md), [worker decisions](native-workers.md), [physical harvesting](native-harvesting.md), [primary orders](native-primary-orders.md) and [physical movement](native-movement-fsm.md). Earlier frozen sources remain unchanged.

## Evidence and boundaries

[Execution evidence](evidence/native-campaign-ai-harvesting-probes.json) records **4 passing complete cases, 3,407 complete original calls, including 3,025 complete object callbacks**, and zero prefix cases or helper stubs. The complete calls execute **87,032,074 instructions**. Four separately identified original terrain preparation fragments execute another **4,303,448 instructions**, making **91,335,522 total original instructions**. The largest measured invocation, including preparation, is 1,327,873 instructions. A returned terrain epilogue after an unexecuted allocator is explicitly a preparation fragment, not a complete function.

The [probe](../../tools/research/probe_native_campaign_ai_harvesting_117.py) SHA256 is `b46cecbdc32def8c92f2084df6775eadd5292bd4e2d975391547256f24279cae`. The [inspector](../../tools/research/inspect_native_campaign_ai_harvesting_117.py) SHA256 is `e56ea8ff709c3ce5432c3cbc68cac9d1cb3d7e71b5738c9b8d7aa4f1e95ddb92`; its [static report](evidence/native-campaign-ai-harvesting-static.json) contains 11 complete bodies and 706 instructions. Execution uses 1,288 reviewed ranges and checks 92 imported source hashes. Runtime is x86_64 Python3.9.6 under Rosetta, Unicorn2.1.4 and pefile2024.8.26, with declared x87 control word `037f` and MXCSR `1f80`.

Code and resource inputs are authenticated before execution. Memory guards reject code writes and guest writes outside declared private mappings. No WinMain, Windows import, guest host-file/network operation, renderer, full accepted-frame caller or entire mission executes. These are selected complete algorithm compositions, not a campaign playthrough or measured real-time throughput.

## Authentic producers and declared startup

The four scenes load **94 actual resource-container UNIT records**, four actual depots, 12 actual workers and 12 actual start-marker records. Unchanged `469460` creates resource/depot/worker objects; unchanged `46a420` applies each actual UNIT record. Unchanged `46a280` produces the start-position array from actual markers. Stock unit-strength initialization `4a98e0` executes before creation.

Complete `445720` imports each actual MTXM/CV5/VF4 terrain, with independent flag/megatile checks. Complete `445690` matches independently decoded pixel heights. Original-generated region/split snapshots are hash-checked and their read fields reconstructed in private memory; graph generation is not rerun in this batch. Dynamic contours start empty and other mission units are omitted. The low visibility word is explicitly **0**, supplying explored/visible terrain before creation; the campaign FOW bootstrap is not executed.

Runtime controllers initially use Human2 for resource creation. The selected owner becomes Computer1 with racial byte1 or2 **before its depot/workers are created**. These are declared valid runtime inputs, not an executed normalization of editor OWNR or lobby settings. Native `486a20`, `4b2640` and `482ee0` initialize Town/WorkerAI/BuildingAI/Guard/PlayerAI pools. Regional-AI records contain declared original region IDs/player IDs and otherwise zero input fields; full regional strategic initialization remains separate. Self-alliance diagonals are declared before creation.

Original script allocation `494d90` and VM `493a60` execute declared bytecode `[56,3,2,1,0,36]`, producing campaign/Town startup, wait and termination themselves. The script rectangle is derived from the selected actual depot, not copied from an authentic mission AI trigger. Full `485020` binds the Town to native `497a20` resource areas, sets native resource pointers/worker limits and mineral labels. Neither CUnit AI pointers nor Town fields are supplied by the host.

## Natural service and repeated deposits

The workers initially have original order **Guard2**. Complete object updates naturally reach **ComputerAI156 → WorkerAI `487580`**, which selects resources and emits Harvest79 through real `485f20 → 42a380`. Read-only entry observations capture the parent top-level function, logical frame, current order, genuine AI pointer and wait byte. Every observed `487580` entry in the published natural run belongs to top-level **`445070`**; no direct worker-service or order-issuer entry is called by the host.

First WorkerAI entry uses callback12 for the tutorial SCV; callback3/6/13/14 for the four Zerg01 SCVs; callback5/13/21 for Protoss02 Probes; and callback5/13/15/21 for Protoss04 Probes. These are the measured scheduling observations for declared inputs, rather than a uniform worker-update interval. Frame input is zero-based; the table below uses callback counts starting at1.

| Actual input scene | Owner / depot / workers | Complete object callbacks | Deposits / current and cumulative mineral increase | Final carried minerals |
| --- | --- | ---: | ---: | ---: |
| Terran tutorial, map56 | 1 / CommandCenter106 / one SCV7 | 498 | 2 /16 | 0 |
| Zerg01 Terran base, map58 | 5 / CommandCenter106 / four SCV7 | 1,091 | 8 /64 | 8 |
| Protoss02, map34 | 7 / Nexus154 / three Probe64 | 748 | 8 /64 | 8 |
| Protoss04, map36 | 1 / Nexus154 / four Probe64 | 688 | 8 /64 | 8 |

The stop condition requires at least two actual deposits **per worker**, not a fixed number of cycles globally. In Protoss02 one worker deposits four times while each other worker deposits twice. At the stop boundaries some workers still carry eight minerals, and the others can already be mining or returning. They are not artificially made idle or synchronized.

Native progression includes Harvest79, MoveToMinerals85, WaitForMinerals86, Mining87, ReturnMinerals90, ResetHarvestCollision151 and ComputerAI156. Original queues, source occupancy, path storage, positions, headings, animation signals, mining-flash/projectile/image work, cargo and deposits progress without host substitutions. ComputerAI156 is distinct from ResetHarvestCollision151.

## Mineral selection beyond the Town seed

The tutorial uses its unmarked Town mineral directly. The multiworker scenes additionally execute complete fallback search, candidate predicates, distance estimation, alternative search and movement retry. Reached call counts distinguish executed branches from static witnesses: map58/34/36 reach `432420` 1,779/470/893 times, `432510` 93/108/98 times, `48e9b0` 640/158/331 times, and state15 retry `42bfd0` 1,218/330/645 times. The bodies are genuine; these totals do not establish every internal branch.

| Complete body | Recovered contract |
| --- | --- |
| `431fe0..432058` | Neutral Mineral176/177/178 existence, packed position, and search radius256 or512 according to the supplied flag; original nearest search `442cc0` |
| `432420..43250c` | Mineral eligibility, surrounding terrain rejection, ground reachability, equal original pixel height, owner-specific explored-bit gate and distance at most twice the search radius |
| `432510..432556` | Above predicate, conditional Computer1 resource-mark `+d9` rejection, and actual busy-byte `+d3` exclusion |
| `412c90..412e1a` | Stock unit extents and surrounding tile-edge tests against terrain mask `08000000`; no substituted line-of-sight query |
| `48e9b0..48eb86` | Original flag-dependent approximate/region-route distance, native failure sentinels and route-center accumulation |
| `48ec20..48ec43` | Actual short-region-path endpoint distance |
| `431f70..431fd6` | Moving-worker alternative mineral search with radius384 and the base candidate predicate |
| `42bfd0..42c022` | Actual state15 wait/retry counter, native retry/reachability consumers and subsequent movement-state selection |

The unoccupied/unmarked predicate and the moving-worker alternative predicate are distinct. A faithful port must preserve their caller flag, search radius, resource marks, busy state, visibility/elevation, path status and tie traversal. Replacing both with a nearest-distance query would lose reached original decisions.

## Independent conservation and geometry checks

After each callback, unchanged native deposit-entry observations supply worker identity, owner, cargo type and amount. Every reached deposit belongs to a genuine worker of the selected owner and carries eight minerals. Current mineral increase equals cumulative increase and the sum of actual deposit-entry quantities. Separately:

```text
initial resource amounts - current resource amounts
    = deposited minerals + minerals still carried by genuine workers
```

This equality is checked after every callback. Initial resources decrease by16/72/72/72 in the four scenes; deposits account for16/64/64/64 and final cargo accounts for0/8/8/8. Current/cumulative baselines are zero input stocks; campaign trigger-based starting resources are not run. All selected completed-worker populations stay below the separately verified computer-harvest bonus threshold.

Every worker's original CUnit integer/fixed-point positions, Sprite position and four spatial borders agree with stock extents. Both original index arrays remain sorted after every callback. Ambient unit/Flingy/Bullet contexts return to zero. Main PRNG source totals and state match the independent integer LCG from seed12345; this validates reached RNG composition, not all possible campaign interleavings.

The logical frame counter is supplied before each whole-object callback. Elapsed game tick stays at declared0; selected slow-service countdowns start at10,000. Native resource-area refresh does execute, with final countdowns404/262/154/214 recorded in the report. Whole accepted-frame time, trigger ordering and all strategic schedules remain separate. Initial and final area snapshots are retained; cached resource-area amounts are not assumed to refresh after every extraction.

## Reproduction and Defold contract

Use the unchanged [campaign image/LO manifest](evidence/native-campaign-resource-assets.json), SHA256 `7dbfd340a8a1ef889ee3d63e5c383d0ff4c22aa7ab703bc9a4a4c71e8d5dd871`, with its 238 image IDs/205 distinct GRPs, and the pinned resources in [reproduction](reproduction.md). The published probe defaults to natural worker service. Its optional `--initial-worker-service selected` is a different explicitly identified profile; selected-entry pilots are not included in the published four-case run.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_campaign_ai_harvesting_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-campaign-resource-assets \
  --asset-manifest docs/research/evidence/native-campaign-resource-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --terrain-report docs/research/evidence/terrain-assets.json \
  --terrain-data /private/tmp/koprulu-terrain-data \
  --regions-report docs/research/evidence/native-campaign-regions-probes.json \
  --snapshots /private/tmp/koprulu-original-campaign-regions \
  --out /private/tmp/campaign-ai-harvesting-raw.json --execute-original-code

python3 tools/research/compact_native_call_trace.py \
  --input /private/tmp/campaign-ai-harvesting-raw.json \
  --out docs/research/evidence/native-campaign-ai-harvesting-probes.json
```

The generated derivative summarizes 2,381,645 entries in eight call-trace arrays, preserving outputs/assertions, complete call counts and per-callee counts. Its metadata hashes the 714,152,119-byte raw report; the raw report remains private. Compaction executes no original instruction and is not another native run.

For Defold, preserve separate Town/resource-area state, WorkerAI labels/waits, original unit-order service cadence, resource marks/busy queues, physical movement and cargo/deposit accounting. Initial script attachment must lead into normal unit service, rather than an engine-side shortcut that simply credits income. Spatial consistency and per-tick conservation are useful implementation gates, alongside exact original decision/order traces.

Remaining scope includes Zerg Town startup/physical AI harvesting, gas allocation/entry/exit and failures under Town scheduling, depleted-resource death/retargeting, damaged or migrating workers, depot destruction/ownership, moving blockers/dynamic contours, real campaign visibility/controllers/overrides, complete mission triggers and the accepted outer-frame schedule. The four selected algorithm compositions do not close six-campaign parity.
