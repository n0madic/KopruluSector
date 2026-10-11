# Native region graphs on authentic campaign terrain, StarCraft 1.17

The original graph builder has now executed on **all68 audited CHKs**, using their original MTXM and hash-verified CV5/VF4 inputs. This extends [terrain import](native-terrain-import.md), [synthetic region generation](native-region-generation.md), and [long paths](native-region-paths.md). It produces the terrain graph before dynamic doodad/building/world mutations; it is not a played campaign or complete native mission initialization.

Target: numerical version **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. Evidence: [probe](../../tools/research/probe_native_campaign_regions_117.py), [instruction witnesses](evidence/native-campaign-regions-static.json), and [native execution record](evidence/native-campaign-regions-probes.json).

## Connected input and execution chain

Each map follows this exact original sequence in private memory:

```text
445562 fragment: generate DWORD megatile flags from original VF4
445720 complete: import original MTXM through original CV5
48ad9f fragment: convert VF4 walkability to byte records
48b20c fragment: copy real megatile IDs into the fixed-stride walk grid
48b2b8 fragment: enforce bottom boundaries and create cardinal direction bits
490e9f fragment: classify the imported tile flags into the fixed-stride raster
490f50 complete: create reserved bottom region0
48f060 complete: rectangles, refreshes, merging, reindexing and split cells
48f0e0 complete: produce region cut ranks
491300 complete: four selected long paths per map
```

Allocation/free boundaries in the input stages remain explicit fragments. No original imported allocator is replaced by a callable fixture. The complete graph builder and its original callees actually return; original scalar CRT memset is selected by an explicit CPU-feature fixture. The complete game, Windows APIs and guest host-file/network operations are outside execution.

Every converted VF4 byte and every final walk-grid byte is compared with the independent bit/boundary definition. The earlier terrain-import batch separately compares every imported tile flag and class on the same68 authenticated maps. Here the graph's original output is recorded and checked for structural invariants; there is **no independent complete graph-builder implementation used as an oracle**.

The graph object, input buffers and stack are private. Authentic CV5/VF4 files, CHKs, and full original-derived graph snapshots stay in scratch outside the project. Evidence in the project records map identities, output hashes and aggregate observations. Snapshot hashes include ordered region records, raster cells and split records; they are not hashes of unordered connectivity alone.

## Corpus and observed geometry

There are64 supported catalog map resources plus four legacy version-57 CHKs. All eight tilesets are represented. The68 graph calls produce:

| Observation | Value |
|---|---:|
| Total region records across maps | 35,840 |
| Minimum / maximum regions in a map | 111 / 1,474 |
| Total split records across maps | 146,742 |
| Maximum splits in one map | 6,910 |
| Maximum neighbor count in one region | 44 |
| Complete graph cases | 68 |
| Connected long-path cases | 272 |

None of these maps reaches the5000-region error branch or the2500-live-region threshold that increases the merge threshold. This supplies positive original evidence for the actual authored MTXM geometry, without closing larger arbitrary-map limit behavior.

Assertions establish that all raster references resolve to a live ordinary region or an allocated split record; neighbor IDs are valid and unique; ground-neighbor prefixes contain the appropriate nonzero/non-blocked region types; and ordinary cells retain the class selected from native imported flags. The final split stage does not rebuild region counts/bounds, as already shown by the smaller distinguishing fixtures.

## Bottom-region split records

Synthetic flat-map tests left every final-row raster cell literally0. Authentic input reveals an additional normal output: the split producer can replace those zero cells with split IDs even though region0 remains reserved with group `0x4000`.

Across the68 native snapshots, **990 final-row cells** contain split IDs. Every such record is exactly `[0xffff,0,0]`: its selected region is0 for every minitile. Consequently, literal equality `bottom_raster[x] == 0` is too strict for authentic final output. A remake must preserve the original raster/split allocation sequence if its graph state, IDs and consumers are intended to match. Resolving the cell to region0 and discarding the split entry can change later split IDs and state hashes.

This is an observation on every supplied map, not a universal claim about all malformed or custom maps. Bottom walk-grid reservation and bottom region allocation remain the separately specified native rules.

## Paths on generated graphs

Four deterministic source/target pairs per map use ordinary nonzero regions in the same connectivity group. Source and target pixels are centers of actual directly owned raster tiles, rather than hand-authored region centers. The original long-path helper reads the real generated ordered neighbor arrays.

All272 calls return1, retain partial byte0, use legal ground-prefix edges and clear all temporary region backreferences. **264 stored paths end at the requested target. Eight do not.** Seven of those eight contain exactly50 stored regions. The remaining path contains22 regions. These outputs are recorded rather than treated as failed parity checks: the helper's recovered capacity and storage contracts already allow successful/partial-zero outputs that do not end at the goal.

An additional [eight-case diagnostic](evidence/native-campaign-path-limits-probes.json), [reviewed witnesses](evidence/native-campaign-path-limits-static.json), and [replay tool](../../tools/research/probe_native_campaign_path_limits_117.py) now attribute these results. It reconstructs only the path helper's read fields from hash-verified original-generated graph snapshots, then reruns the complete original helper and compares each exact stored path/return to this batch. Pointer addresses and temporary fields are private fixtures; graph creation is not rerun by the diagnostic.

The seven50-region outputs have full parent-chain counts55,51,99,105,52,56,52. Their native capacity-stop branch is not taken: the storage cap drops the goal-side suffix. The22-region result occurs on the `expzerg/zerg10` graph for source473→target129. It allocates all350 search nodes, records peak open count34, and actually takes `0x491803`, the capacity stop, before selecting the best remaining open node. Its partial byte remains0. Thus this authored geometry supplies a concrete positive instance of the node-capacity behavior previously established with synthetic graphs. These results do not demonstrate exhaustion of the125-open-node heap limit on an authentic map.

The earlier [capacity specification](native-region-paths.md) separately establishes350 search nodes,125 open nodes, source-side50-region storage, and the absence of a partial-byte update on capacity exhaustion. Both the helper and its downstream callers must retain their treatment of a non-reaching stored path.

Paths are not compared with shortest-path output. Native cost approximations, ordering and capacities are part of the original behavior; the shortest available route is not the acceptance oracle.

## Evidence scale and reproduction

The batch contains **340 cases**, **544 complete original-function returns** and **340 explicit fragment completions**. A case can contain multiple invocations. The maximum observed complete builder invocation is42,140,114 instructions; the explicit per-invocation limit is100,000,000. Basic-block hooks verify every executed instruction boundary against the reviewed original witness addresses/sizes, excluding embedded jump tables and imported allocator destinations.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_campaign_regions_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --terrain-report docs/research/evidence/terrain-assets.json \
  --terrain-data /private/tmp/koprulu-terrain-data \
  --campaign-data /private/tmp/koprulu-campaign-data \
  --scratch /private/tmp/koprulu-original-campaign-regions \
  --static-out docs/research/evidence/native-campaign-regions-static.json \
  --execute-original-code \
  --out docs/research/evidence/native-campaign-regions-probes.json
```

`--map-limit` supports a narrow diagnosis; limited output explicitly records that not all audited maps are included. Default mode is static only. The tool rejects unknown executable, tileset or CHK identities before executing the associated guest input, and rejects a snapshot scratch directory inside the project.

Reproduce the separate complete path diagnostic after generating the snapshots:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_campaign_path_limits_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --regions-report docs/research/evidence/native-campaign-regions-probes.json \
  --snapshots /private/tmp/koprulu-original-campaign-regions \
  --static-out docs/research/evidence/native-campaign-path-limits-static.json \
  --execute-original-code \
  --out docs/research/evidence/native-campaign-path-limits-probes.json
```

## Remaining consumers

Native collision-contour construction/sorting, local paths and collision response, repathing, dynamic doodad/tile changes, building/occupancy updates and full movement remain separate algorithms. This batch supplies authentic graph state for subsequent AI reachability/path checks. It does not certify world initialization, full unit traversal or complete campaigns, and the [completion ledger](completion-ledger.md) retains those boundaries.
