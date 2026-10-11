# Authentic Protoss depot/worker records connected to Town binding

Two complete original compositions extend [authentic Terran Town binding](native-campaign-resource-towns.md) to **Nexus154 / Probe64 / runtime Protoss race2**. Actual source records are from supported original Protoss02 and Protoss04. [The probe](../../tools/research/probe_native_campaign_protoss_towns_117.py), SHA256 `db1f4e9e673d534fb4bd7b0c0dff10785e2bf03bb01af66873ce97d2d2cd5278`, and [the report](evidence/native-campaign-protoss-towns-probes.json) preserve their source/record hashes and native output relationships.

The executable is the same StarCraft1.17.0.1 PE32 x86 image, preferred base`0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. The run records **2 passing complete compositions /228 complete original calls /5,044,301 original instructions**, plus2 explicitly separate post-allocation terrain fragments. Maximum invocation length is1,327,873 instructions;91 imported source hashes and1,280 reviewed ranges are pinned. Runtime is Rosetta x86_64 Python3.9.6, Unicorn2.1.4 and pefile2024.8.26. Native return/stack, code/write and256-source LCG checks apply. No helpers, imports, prefixes, renderer or complete game are substituted/executed.

## Selected native relationships

| Actual source scene | Declared computer owner | Original-created depot/workers | Native Town center | Generated area | Counts/remaining amounts | Workers/limit |
|---|---:|---|---|---|---|---|
| Protoss02 | 7 | Nexus`(320,720)`;3 actual Probes | `(320,820)` | 1 at`(320,720)` | M10/G1;6,238/6,453 | 3 /15 |
| Protoss04 | 1 | Nexus`(736,496)`;4 actual Probes | `(736,596)` | 2 at`(224,368)` | M12/G1;13,500/6,500 | 4 /27 |

Complete `469460/46a420` create/configure each actual Nexus/Probe and all authentic resource records. Complete `46a280` produces the start coordinates, and complete original terrain/resource-area generators produce the height/area/tag inputs. The loaded terrain graph is reconstructed from a hash-verified original-generated snapshot; graph creation is not rerun.

Actual AIScript startup `[56,3,2,1,0,36]` creates the Town and genuine WorkerAI/BuildingAI records through `494d90/493a60`. This is a supplied **synthetic supported script**, not an authentic whole mission program. `485020` then binds original resource pointers, clears refresh demand and sets all selected worker categories1. Every nonzero pointer belongs to an actual created resource with the selected original tag. Main-building pointers match the genuine Nexus objects; workers and counts match the original-created Probes. No host Town/AI-list/pointer/category/limit assignment supplies these results.

The same independent remaining-reserve formula and complete `486130/496690` checks apply. Protoss02 has q12/T10, producing `min(12,20)+3=15`; Protoss04 has q27/T12, producing `min(27,24)+3=27`. Here q derives from the selected area's mineral amount, and T from genuine player Town membership. Neither is the player's spendable mineral balance.

Runtime controllers, Protoss race2, visibility and initially empty authentic-ID regional AI arrays remain caller inputs. Complete stock type-strength initialization uses declared x87`0x037f`/MXCSR`0x1f80`. Whole runtime-player/regional-AI/bootstrap producers, mission overrides/nonselected units, power-mask bootstrap and physical harvesting are not executed. No Pylon is added or psi-mask behavior certified by these Nexus/Probe scenes.

## Evidence and reproduction

Resources reuse [campaign-resource graphics provenance](evidence/native-campaign-resource-assets.json). The [generated compactor](../../tools/research/compact_native_call_trace.py) summarizes41,879 function-entry records in4 arrays, preserving output/assertion/probe identities and canonical full-trace hashes. It runs no original instructions; the unchanged raw report remains private.

Use [campaign-resource reproduction](native-campaign-resource-areas.md#compact-evidence-and-reproduction), substituting tool `probe_native_campaign_protoss_towns_117.py`, `--map-indices 34,36` and separate raw/compact outputs. Original execution requires `--execute-original-code`.

The cases establish this selected racial startup/resource-binding composition. Other racial/native runtime bootstrap, physical mining, demand-driven gas allocation, stale-worker migration, full mission loading, dynamic obstructions and campaign scheduler interleaving remain separate closure work.
