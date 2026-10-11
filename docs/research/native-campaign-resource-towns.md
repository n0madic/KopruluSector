# Authentic resource producers connected to original Town binding

Two complete compositions connect authenticated campaign resources, original start-location extraction and area generation to genuine AIScript Town startup and original worker/resource binding. They extend [the64 supported-map generator scenes](native-campaign-resource-areas.md), [resource consumers](native-resource-areas.md) and [Town pools](native-towns.md), keeping their frozen sources unchanged.

The target is StarCraft **1.17.0.1**, PE32 x86 base`0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. [The probe](../../tools/research/probe_native_campaign_resource_towns_117.py) and [execution report](evidence/native-campaign-resource-towns-probes.json) pin the exact probe/dependency hashes. The run contains **2 complete passing compositions /154 complete original calls /3,667,679 original instructions**, plus2 explicitly separate post-allocation terrain preparation fragments. Maximum invocation length is1,327,873 instructions. There are90 imported source hashes and1,280 reviewed ranges.

Execution uses Rosetta x86_64 Python3.9.6, Unicorn2.1.4 and pefile2024.8.26. Original calls return to the sentinel with the expected stack, code/write guards apply, and all256 RNG source counters match the independent LCG. There are no prefixes, helper substitutions, Windows imports, renderer, process startup or complete-game execution.

## Original producers and explicit caller inputs

Both cases reuse authenticated MTXM/CV5/VF4 and hash-verified original-generated graph snapshots. Whole original terrain import and every selected resource's creation/properties/height checks run as in the64-map batch. Graph creation and excluded heap allocations are not rerun. Visibility, runtime controllers and initially empty regional AI records remain declared inputs; those AI records have authentic region IDs but do not constitute an original regional-AI bootstrap.

Unlike the earlier generator-only composition, each actual Start Location record runs **complete `46a280`**, directly producing the original start-position globals. No host coordinate copy replaces that helper. Its ordinary-mode/local-player inputs remain explicit, and the whole UNIT section allocator/loop is still excluded.

Actual depot and worker UNIT records supply type, owner, pixel position and property bytes. Complete `469460/46a420` genuinely create the selected Command Center106 and SCVs7. The harness then selects computer controller1 and **Terran race1** for the chosen owner as caller inputs. It does not claim that the tutorial's original human slot becomes a computer during campaign play.

Complete `4a98e0` initializes all stock type-strength caches before creation. The declared numeric context is x87 control word`0x037f` and MXCSR`0x1f80`, matching the separately tested [normal finite strength arithmetic](native-combat-ai.md). Native pure numeric callees run; no exceptional CRT/import path is substituted. This proves the selected cache producer under that context, not the original process's FPU bootstrap.

Native `486a20/4b2640/482ee0` initialize the relevant AI pools/player state. The synthetic supported program `[56,3,2,1,0,36]` invokes actual **start_campaign → start_town → wait1** through full `494d90/493a60`; it is not an authentic whole mission script. Its supplied rectangle surrounds the actual depot/worker scene. Original startup allocates the Town, WorkerAI and BuildingAI and links the genuine CUnits.

Complete `485020` then selects the generated area, discovers the actual resource objects, computes the worker limit, clears resource-refresh demand and assigns WorkerAI category1. No host Town area, main-building/resource pointer, worker count/limit, AI kind/list or worker-category write supplies these outputs. Physical harvesting and whole strategic scheduling are not executed.

## Two native bindings

| Source scene and declared computer owner | Genuine depot/workers | Selected original area | Mineral/gas counts and amounts | Original worker count/limit |
|---|---|---|---|---|
| Terran tutorial, owner1 | CC`(1824,208)`, SCV`(1692,241)` | 1, at`(1728,240)` | M8/G1;7,250/5,000 | 1 /17 |
| Zerg01, owner5 | CC`(1664,336)`, four actual SCVs | 2, at`(1664,336)` | M8/G1;12,000/7,000 | 4 /19 |

The original Town centers are`(1824,308)` and`(1664,436)`, derived from the supplied startup rectangles. They are distinct from the depot's position and generated area's center. Each main pointer matches the original-created Command Center. Every nonzero resource pointer belongs to an original-created object carrying the same generated area tag; the selected gas pointer is real, and the other two gas slots are zero.

All selected workers are genuinely linked through kind2 WorkerAI to that Town, their count matches the actual created SCVs, and their category bytes are1. The depot has kind3 BuildingAI linked to the same Town. This demonstrates resource-area producer → genuine Town relationships → binding, rather than supplying a Town-shaped memory fixture.

The independent worker-limit check uses the **area's remaining mineral total**, not the player's spendable mineral balance. Complete `486130` returns the player Town sum `T=8`; complete `496690` independently returns the same limit stored by `485020`. With `m=8`, `g=1` and `q=floor(area_minerals/500)`, the separately established rule is:

```text
limit = (m if q < T else min(q, 2*m)) + 3*g
```

Tutorial q14 gives17; Zerg01 q24 gives19. The source checks this arithmetic against both genuine native consumers and the original Town field. These two one-Town scenes do not extend the rule to arbitrary malformed area IDs; its broader boundaries are in [resource-area consumers](native-resource-areas.md#worker-limits-depend-on-player-towns).

## Evidence, reproduction and remaining scope

The [generated trace compactor](../../tools/research/compact_native_call_trace.py) reduces21,910 function-entry records across4 trace arrays while preserving native outputs/assertions and exact source hashes. It executes no original code; the unchanged raw report remains private. Graphics reuse [campaign-resource provenance](evidence/native-campaign-resource-assets.json); no retail bytes are added to the project.

Run [campaign-resource reproduction](native-campaign-resource-areas.md#compact-evidence-and-reproduction), replacing the probe with `probe_native_campaign_resource_towns_117.py`, selecting `--map-indices 56,58` and using a separate raw/compact output. Original code executes only with `--execute-original-code`.

Remaining compositions include other racial Town startup, genuine regional-AI/runtime-player/bootstrap producers, full actual map object/override/relationship loading, dynamic obstructions, stale-worker recovery/migration, physical mining/gas travel and whole campaign scheduler interleaving. These two selected complete bindings do not certify either mission's playability or its original AI script execution.
