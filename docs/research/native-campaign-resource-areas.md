# Original resource-area generation on authentic campaign inputs

Complete original resource-area generator `497a20` returns on **all 64 supported-version campaign CHKs**, using genuine resource objects, their actual UNIT property records and authenticated campaign terrain. This connects previously separate [mission creation](native-mission-units.md), [terrain import](native-terrain-import.md), [original-generated campaign graphs](native-campaign-regions.md) and [resource-area algorithms](native-resource-generation.md). It does not execute a whole mission or rerun graph creation.

The target is StarCraft **1.17.0.1**, PE32 x86 base `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Preferred VAs below belong to this exact image. The local repack's file identity is fixed; unchanged retail provenance for every repack payload is not implied.

## Complete calls and declared preparation

[The probe](../../tools/research/probe_native_campaign_resource_areas_117.py), SHA256 `a7160b6e972289825800eb717decb5b5e2ddda6f4f120bf0028051061eb8f9d4`, records **64 complete passing compositions**, **11,308 complete original function calls**, and **336,946,805 original instructions**, with no prefixes or substituted helpers. The largest complete call takes **48,100,484 instructions**. Ordinary invocations are bounded at8,000,000; generator entry `497a20` has a separate80,000,000 bound. The earlier8,000,000 generator attempt exhausted its bound and was not published as a completed case.

The [published report](evidence/native-campaign-resource-areas-probes.json) pins89 imported source-module hashes,1,245 reviewed code ranges, executable/DAT/IScript/resource hashes, every CHK/MTXM/graph-snapshot identity, native outputs and original filter-branch observations. Execution uses x86_64 Python3.9.6 under Rosetta, Unicorn2.1.4 and pefile2024.8.26. Every invocation must reach its sentinel with the expected stack restoration; code and guest writes are guarded. The native LCG result is checked against all256 source counters. There are no Windows imports, process startup, renderer, guest host-file/network access or full-game execution.

For each map, the preparation and execution sequence is:

1. Initialize finite original CUnit/Sprite/Image/Thingy pools and execute complete acquisition/Bullet initialization. Private Sprite/Image pools are relocated to prevent overlap with larger authentic resource sets.
2. Reconstruct documented graph read fields from the hash-verified output of the earlier **original** graph builder: ordered regions/neighbors, fixed-point centers, groups, flags, cut ranks, pixel bounds, raster and split records. Temporary fields start at zero. The original graph builder is not rerun in this composition.
3. Load exact CV5/VF4/MTXM bytes into private memory. Execute the original `445562` **post-allocation megatile-flag fragment**, checking its whole output against an independent bit specification, then complete original terrain import `445720`, also checking the whole flag/megatile output. There are64 preparation fragments, separate from11,308 complete calls.
4. Supply visibility low WORDs`0xffff`, human runtime controllers2, and the raw start-location coordinates bound to the eight start-position globals. Full controller normalization and the original start-position producer are outside this batch. Start-marker order and the last record for a repeated owner are preserved.
5. For every selected authentic resource record, execute complete producer `469460`, then complete property application `46a420(record+12)`. No host resource amount or area tag assignment replaces those producers.
6. Execute complete `445690` at every resource pixel and check it against the independent exact MTXM/CV5/VF4 specification, then run complete `497a20` and record all generated areas/tags.

These preceding allocation results and graph buffers are explicit composition inputs. The fragment does not certify its excluded original heap allocation, and copying authenticated snapshot fields is not another run of graph creation. Full CHK section allocation, record relationships, mission UNIS/UNIx overrides, nonresource units, dynamic obstructions and the complete accepted-frame loop remain outside this scope.

## Physical source coverage and native observations

The supported sources use CHK versions59/205 and cover all eight tilesets. Four physical version57 extras are excluded explicitly; they remain in the [68-map source inventory](campaigns.md). This is supported-format coverage, not proof of campaign branch progression.

| Tileset | Supported CHKs |
|---|---:|
| Badlands | 10 |
| Platform | 9 |
| Installation | 4 |
| Ashworld | 10 |
| Jungle | 12 |
| Desert | 10 |
| Ice | 4 |
| Twilight | 5 |

All **3,684 resource-container UNIT records** in those maps are created and configured: minerals176/177/178 at751/1,350/1,268 records; Geyser188 at156; Refinery110 at56; Extractor149 at68; Assimilator157 at35. Preplaced gas buildings contribute to the original gas classification. Ignoring them would leave161 actual source records out of this generator composition.

The source also contains307 start markers. The generator produces554 total area records across the64 scenes, including empty start areas. Every live resource receives an original output tag between1 and the map's used count. For each tag, independently recomputed mineral/gas counts and remaining-amount sums match the native area record. Resource type, owner, pixel position and low16-bit amount match the original record after native creation/properties. Every tested map stays within the249 used-area capacity.

Pixel heights are independently checked for all resources:2,476 at height0,1,100 at height1 and108 at height2. The generator's real branch observations include:

| Gate | Start-area accepted/rejected | Cluster-candidate accepted/rejected |
|---|---:|---:|
| Region/group | 1,760 /16 | 4,859 /115 |
| Equal height | 1,690 /70 | 4,623 /236 |
| Connected-path bound | 1,601 /89 | 4,117 /76 |

These are branch execution counts, so one object may contribute many attempts. Gates run sequentially; pixel/rectangle filtering can stop a candidate before a later path check. The table is not a count of unique failed resources.

The tutorial Terran scene is a concrete result: nine resource records produce area1 at`(1728,240)` with M8/G1, amounts7,250/5,000, and empty start area2 at`(1728,1232)`. All nine original tags are1; all nine pixel heights are1. On expansion Terran05a the original scene includes89 resources and produces15 areas, with actual height and path rejections.

The complete partition is retained as an **original observation**. Independent record/height/conservation assertions verify inputs and accounting; this batch does not claim an independently predicted complete partition for every map. [Flat/boundary/weight cases](native-resource-generation.md) and [direct equal-score reference capture](native-resource-reference-ties.md) provide separate discriminating algorithm evidence. An authentic graph does not by itself certify local unit movement or dynamic contour changes.

## Compact evidence and reproduction

The original private raw report contains3,333,723 native function-entry records in128 trace arrays and is937,334,986 bytes. [The generated compactor](../../tools/research/compact_native_call_trace.py) publishes callee counts, first/last argument samples and canonical SHA256 for each complete trace. Case outputs/assertions, native instruction counts, scope and original probe/dependency hashes are preserved. The compactor executes **no original code**; it does not convert fragments or prefixes into complete returns. The raw report remains private with SHA256 `1a120302c3bf499136375fbeecff3b0576dc9b76d991f5756b04914c2f07b652`.

The [graphics manifest](evidence/native-campaign-resource-assets.json), SHA256 `7dbfd340a8a1ef889ee3d63e5c383d0ff4c22aa7ab703bc9a4a4c71e8d5dd871`, pins238 Image IDs/205 GRP paths and relevant LO files. It combines authenticated stock resources from frozen construction/gas/Zerg batches through the original provenance extractor. Resource bytes are private in `/private/tmp/koprulu-campaign-resource-assets`. This proves native image initialization/state, not rendered pixels.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_campaign_resource_areas_117.py \
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
  --map-indices 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,44,45,46,47,48,49,51,52,54,55,56,58,59,60,61,62,63,64,65,66,67 \
  --execute-original-code --out /private/tmp/campaign-resource-areas-raw.json

python3 tools/research/compact_native_call_trace.py \
  --input /private/tmp/campaign-resource-areas-raw.json \
  --out /private/tmp/campaign-resource-areas-published.json
```

Without the explicit execution flag the probe prints an inert plan. Regenerating the raw report and its compact derivative is separate from [freshness verification](reproduction.md), which does not rerun emulation. Town binding/scheduling, physical harvesting, mission object obstructions, runtime player initialization and complete mission-world startup remain open compositions in [the completion ledger](completion-ledger.md).
