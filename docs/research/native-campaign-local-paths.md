# Original local searches on all campaign maps

**816 complete search cases on all68 original maps pass the declared contracts.** The original initializer returns1 in813 cases and0 in3 cases. Passing means authentic input provenance, complete unchanged execution, bounded output and agreement between independent and native footprint queries. It does not mean every search finds a complete walkable route.

Target: StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred base `0x400000`. [Static evidence](evidence/native-campaign-local-paths-static.json) contains87 reviewed ranges /7,919 decoded witness instructions; some ranges overlap. [Probe evidence](evidence/native-campaign-local-paths-probes.json) records816 search cases,6,471 complete original entry returns and388,854,860 instructions, including272 native contour sorts. Maximum observed invocation:1,151,398 instructions, below the10,000,000 limit. No helper replacements, imports, full game/world initialization or physical movement execute.

The [probe](../../tools/research/probe_native_campaign_local_paths_117.py) SHA256 is `6dc3c13011d999d484dab045a68a4caf1447fd13ea3b4344b831b5d204c5346d`. Static-only regeneration is byte-identical. Exact source dependencies, unchanged UnitsDAT hash, original graph snapshot hashes and input-report hashes are recorded.

## Authentic inputs and declared unit positions

Every original CHK is hash-checked against the [terrain corpus](terrain-assets.md). Effective CV5/VF4 inputs reconstruct megatile IDs, flag arrays and walk geometry; the resulting hashes must match the earlier [native import](native-terrain-import.md) and [contour producer](native-contours.md). Native qsort executes on all four contour orientations of every map. Its resulting concatenated bytes must match the recorded native contour hash.

Each graph comes from the private, hash-checked output of [original campaign region generation](native-campaign-regions.md). The fixture restores raster, splits, IDs, region classes/groups, tile counts, native adjacency order, fixed-point centers, bounds and cut ranks. This is a serialization of those producer outputs, not a complete raw-memory world snapshot. Neighbor pointers are relocated into declared private buffers; temporary fields start clear.

Twelve search inputs per map use six unchanged stock footprint profiles: Marine0, SCV7, Tank30, Zergling37, Drone41 and Zealot65, each at two diagnostic positions near authentic contours. The positions are selected by the earlier [all-map movement-query diagnostic](native-campaign-terrain-sweep.md). A deterministic authored selector tries same-group, independently fitting destinations at distances512/256/128/64/32/16/8/1. This selection procedure is not original AI behavior.

Both source and requested destination must pass complete original `42ed50` footprint validation. CUnit and Sprite positions, status and empty other-unit spatial indexes are explicit fixtures. The native local initializer `491210` then performs genuine long-region search when needed, local target adjustment, local search, visited-area updates, heap operations and reverse reconstruction. This batch stops after the first local segment; [selected continuation](native-local-paths.md) separately executes finite packed-record reuse and repeated segment consumers.

## Returned points, failed searches and the lower boundary

The813 positive searches return3,751 points. Every point is checked by complete native `42ed50` and an independent walkability/contour-footprint calculation. The two checks agree for all points; eight points fail both checks.

All eight failures occur in the same Tank case on **BW Zerg05**, starting at`(2544,6112)` and requesting`(3056,6112)`. Both source and destination are in region0 of the native graph's special bottom row, although the independent terrain placement query accepts them. The initializer returns1, adjusts its segment goal to`(3040,6096)`, and emits nine points. Its first eight points fail footprint validation; only the adjusted final point passes. These are actual outputs under the declared bottom-row inputs, not a claimed live campaign command sequence. Region0 handling and physical response must not be inferred from footprint permission alone.

Independent exact-rational segment tests intersect each returned straight segment with integer forbidden rectangles derived from all native contours and the stock footprint. All3,742 segments outside that one bottom-row case avoid those rectangles. All nine segments in the bottom-row case have reported intersections. This is a geometry diagnostic, not execution of the Flingy/collision response or a proof of optimality.

Three source/destination pairs pass footprint permission and share a connectivity group, yet return0 with no local points:

| Map / unit | Source → requested destination | Native local node / peak open count |
|---|---|---|
| BW Terran06 / Drone41 | `(2299,2715) → (2811,2203)` |28 /5 |
| SC Protoss05 / Drone41 | `(459,2291) → (971,2291)` |6 /0 |
| SC Zerg08 / SCV7 | `(3755,1619) → (3243,2131)` |6 /0 |

Same-group connectivity and independently fitting endpoints therefore do not guarantee successful local routing for every stock shape. The reports retain the exact native output flags, region counts and search state. No guessed semantic label is assigned to every context flag.

## Real capacity and fallback consumers

The largest observed local node count is **250**, while the largest peak open count is71. The stock Tank search on **BW Terran07**, source`(3584,3472)` toward`(4096,2960)`, reaches the physical arena end. It returns a27-point intermediate segment, region count9 and raw flags`[0,1,0,0]`. The trace executes `4925dc` with allocation cursor at250 nodes and heap count27. This is a genuine capacity case, rather than a synthetic array-size inference.

The trace also records actual comparisons at `492170` (150-node early criterion) and `49219b` (200-node criterion). These run with allocation counts through156 and221 respectively. Their application depends on native target/neighbor-region counters and existing search flags; they are not unconditional global limits. The static witness and exact comparison-state histogram preserve those inputs for implementation. [Heap identity and signed priority contracts](native-local-heaps.md) independently prove helper tie rules through a declared count150.

This batch reaches the250-node arena boundary, but does not fill the open heap to150. It does not force every fallback composition, every unreachable-target adjustment or every possible endpoint selection on each map.

## Reproduction and remaining movement work

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_campaign_local_paths_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --terrain-report docs/research/evidence/terrain-assets.json \
  --terrain-import-report docs/research/evidence/native-terrain-import-probes.json \
  --contours-report docs/research/evidence/native-contours-probes.json \
  --campaign-regions-report docs/research/evidence/native-campaign-regions-probes.json \
  --terrain-data /private/tmp/koprulu-terrain-data \
  --campaign-data /private/tmp/koprulu-campaign-data \
  --graph-scratch /private/tmp/koprulu-original-campaign-regions \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --static-out docs/research/evidence/native-campaign-local-paths-static.json \
  --out docs/research/evidence/native-campaign-local-paths-probes.json \
  --execute-original-code
```

Runtime: x86_64 Python3.9.6 / Rosetta, Unicorn2.1.4, Capstone5.0.6 and pefile2024.8.26. `--map-limit` produces a diagnostic subset whose report explicitly does not include all audited maps. Omitting the execution flag validates the exact EXE and disassembles only. Retail payload and graph snapshots stay in private scratch.

Remaining contracts include positive other-unit local geometry and policy, full physical movement-state progression, endpoint/status producers, repath/collision response, cache invalidation, dynamic terrain, allocator-backed contour lifecycle, complete native world initialization and naturally issued campaign routes. All-map first-segment evidence closes a specific composition, not the broader R13 domain or full campaigns.
