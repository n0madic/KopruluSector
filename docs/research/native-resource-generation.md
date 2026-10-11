# Native resource-area generation and clustering

**42 complete original cases** execute resource-area generator`497a20`, its genuine clustering/query/path callees and selected capacity boundaries. Three cases connect its original area coordinates/tags to full AIScript Town creation and original Town resource binding. This supplies selected producers for [resource-area consumers](native-resource-areas.md), while authentic terrain/world initialization remains a separate composition.

The target is StarCraft **1.17.0.1**, PE32 preferred base`0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs; subtract the base for RVAs. [The execution report](evidence/native-resource-generation-probes.json) contains42 complete passing cases, **450 complete original calls /6,145,550 original instructions**, zero prefixes and zero helper stubs. It records207 visited range starts,1,192 reviewed ranges and84 imported source hashes. The largest invocation uses71,827 instructions under the8,000,000 bound. Instruction/write guards and sentinel/stack checks apply to every call; an independent integer LCG check covers all256 RNG source counters.

The [probe](../../tools/research/probe_native_resource_generation_117.py) SHA256 is `be1fa6839cdedea80d22dd50c71a9ed581958f6f99c608efe0e29e1bfac9b6cc`. The [inspector](../../tools/research/inspect_native_resource_generation_117.py) SHA256 is `55ac3a28bdd4d0da67637819f3ff3518e70713879a5025451d20d9b8ff376b84`; [its report](evidence/native-resource-generation-static.json) contains7 blocks /1,003 decoded instructions without undecoded tails. Private hash-checked resources reuse [construction provenance](evidence/native-construction-assets.json). Execution uses x86_64 Python3.9.6 under Rosetta, Unicorn2.1.4 and pefile2024.8.26. There is no helper replacement, Windows import, process startup, renderer, guest host-file/network access or complete gameplay execution.

## Declared inputs and case matrix

Actual original allocation/completion creates the resource objects and selected Command Center/SCVs. Start-position/controller fields, remaining resource WORD amounts, a prepared flat two-region terrain graph, equal terrain height, empty contours, visible/walkable tile arrays and finite pools are caller inputs. Native scalar CRT clearing and stack-page probing execute as original code; their private writable stack is enlarged to accommodate the cluster helper's`0x6a64`-byte locals. These are actual returned pure CRT calls, not substituted allocation services.

The generator overwrites the area records and live resource tags itself. Initial authored tags in resource fixtures are deliberately stale; none is treated as the resulting original tag. The three connected cases do not write a Town area, resource pointer, worker limit or WorkerAi label. Resource amounts still come from explicit caller inputs rather than a complete original CHK loader.

| Cases | Complete contract |
|---:|---|
| 9 | Empty/one/eight/zero-axis/sparse starts and near/far/no-start resource groups |
| 3 | Generated areas/tags → actual AIScript Town startup → original resource binding/worker limit |
| 6 | Mineral640 and gas896 inclusive attachment boundaries |
| 8 | Same-type clusters, boundary separation, resource weight and bounding-box midpoint |
| 2 | Actual resource Death exclusion and retained old tag |
| 1 | All eight controller-byte classes and initial occupied flags |
| 9 | Mixed mineral/gas reference selection and20-cell cutoff |
| 4 | Direct complete cluster-helper capacity at used counts248/249 |

The four capacity cases supply the helper's classification vector and existing used-count/array state explicitly; they are complete helper calls, not an original producer of248 preexisting areas. All other cases enter the complete generator with original-created units.

## Start records and existing-area attachment

`497a20` clears`0x2ee8` bytes at`6a2e88`: all250 records plus the used count and refresh countdown. It scans player start-position pairs0–7 in order. A pair is skipped only when both WORDs are zero; x0/y512 and x512/y0 both produce an area. Nonzero pairs are compacted into successive indices beginning at1, with start marker1. Controller bytes1/2 set occupied flag1; the tested0/3/4/5/6/7 do not. The controller meaning/production is outside this input matrix.

The generator then scans the actual active CUnit list. It classifies live stock resources into mineral1 or gas2, clears their existing area tag, and tries existing start areas through complete`4955b0`. Death objects are classified0 and skipped before that tag clear: an actual dying mineral/Geyser retains its old`0xaa` tag in the selected cases, while contributing no area.

Start matching uses stock mineral classification to choose approximate pixel limit640 for minerals or896 for gas. It also calls actual region/group acceptance`481d00`, checks terrain-height equality through`445690`, and bounds connected distance`496930` at20/28 respectively. The first qualifying area wins. The six positive/negative probes place resources at639/640/641 or895/896/897 pixels on the prepared flat graph. They establish the inclusive pixel boundary under that graph, not all disconnected/split/diagonal path behavior.

Matched objects update that area's counts, unsigned remaining sums and CUnit tag. The baseline nearby start at896,512 absorbs both minerals750/250 and gas5000/2000, yielding mineral/gas counts2/2, totals1000/7000 and all four tags1. A far start3500,3500 remains an empty marked area; the resource cluster becomes area2. With no start, the same cluster becomes area1.

## Unassigned cluster order, weight and reference

Complete`495890` runs **gas category2 first, then mineral category1**. It collects matching unassigned candidates in active-list order, gives each a ±960-pixel rectangle and obtains its weight through complete spatial query`497480`. Eligible resources contribute `min(floor(amount/500)+1,4)` to an initial weight1, subject to the original rectangle/group/height/pixel/path gates. The static witnesses establish the arithmetic and filters; the far two-mineral fixtures show that a5000-amount candidate is processed before a zero-amount candidate even when created later. A zero-amount resource still receives a count/tag/area. Equal candidate weights preserve the earlier candidate because replacement requires a strictly greater weight.

Before collecting a cluster, complete`495e70` may replace the selected seed with an unassigned mineral. It compares native connected-distance scores, accepts at most20 cells and replaces on an equal score; otherwise it retains the original seed. The path helper can use same-region approximate distance or a real long-region path, and its fallback remains original code. This batch exercises the flat/same-region branch; [authentic region graphs](native-campaign-regions.md) are not loaded into these scenes.

Candidate consumer`495750` checks stock resource properties, unassigned tag, the prepared rectangle, region/group acceptance, terrain-height equality and the resource-specific640/896 pixel plus20/28 path limits. The actual collector writes classification3/4, count, sum and the newly allocated area tag. The final area coordinate is the integer midpoint of the collected bounding-box extrema, **not a quantity-weighted centroid**.

Two minerals at800/1439 produce center1119; at800/1440 they produce1120. At641 pixels apart they form two areas. Two gas objects join through896 pixels and separate at897. The baseline four-resource cluster spans896–1152 in x and768–1024 in y, producing1024,896 with counts2/2 and totals1000/7000.

## Mixed groups expose a separate reference cutoff

With a1500 mineral atx800 and5000 Geyser atx1200/1440/1441/1471 (all y1024), the gas-first pass selects the mineral as reference and groups both objects. Centers are the integer bounding-box midpoints. On this flat graph the reference score is `floor(distance/32)`, so671 pixels still scores20.

At gasx1472, the reference score becomes21; the mineral is rejected as reference. The gas retains itself as seed, and the mineral's640-pixel collector bound also rejects the672-pixel separation. They become gas area1 and mineral area2. Positions1696/1697/2800 and the reversed800/2800 case retain that gas-first ordering. Thus a single generic “cluster radius” cannot reproduce both same-type and mixed-group results.

The671/672 result depends on the declared same-region flat geometry; it is not a universal straight-line equivalent of the connected-path predicate. Multi-region costs, heights and disconnected terrain need separate probes.

## Capacity and connected Town consumers

The unassigned-cluster helper stops creating areas when used count reaches249, leaving index0 reserved and the250-record allocation bounded. At input248 it creates exactly area249, assigns the first selected resource and stops before the next; at249 it assigns neither resource. Its supplied refresh countdown123 stays unchanged. Both mineral and gas category paths are tested. This does not certify arbitrary malformed used-count inputs or larger custom-map behavior.

Three complete compositions run actual `start_campaign/start_town/wait1` after generation, with four original SCVs and a Command Center. Complete`485020` picks generated area1 for near/no-start profiles or2 for the far-start profile, discovers the actual mineral and two gas pointers, clears its refresh-needed byte, and computes worker limit8. All WorkerAi labels become mineral1 with no gas demand. No host resource/tag/area/limit replacement is used between generator and Town consumer; physical harvesting remains separate.

## Remake contract and reproduction

Preserve compacted start ordering, first qualifying start match, gas-before-mineral passes, resource-amount weights, distinct tie rules, mineral-reference selection, pixel and connected-path gates, midpoint coordinates, original tags and249-area capacity. Refresh occupancy and strategic reservations have their own later producers; generation's initial flags do not replace them. Keep terrain/import/graph evidence and area/Town evidence separate until a connected authentic-world scenario executes them together.

Run the inspector with the authenticated executable and an output path. Reuse [connected construction reproduction](native-ai-construction.md#reproduction) with tool`probe_native_resource_generation_117.py`, `--case-group all` and a new output path. Without`--execute-original-code` the tool prints its plan. All private resources remain outside the project.

R26 remains partial for authentic campaign terrain/resource-record initialization, differing heights/groups/splits/obstructions, other racial Town allocation/recovery and full scheduler interleaving. These42 cases establish the selected generator and connected consumers; they are not a campaign walkthrough or complete remake parity proof.
