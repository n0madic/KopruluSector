# Native resource-area refresh, selection and Town worker allocation

**81 complete original cases** establish resource-area refresh, selected lookup rules, player-wide worker limits and a connected Terran Town resource-binding/allocation pass. They extend [Town startup](native-towns.md), [automatic requests](native-automatic-requests.md), [economy](native-economy.md) and [workers](native-workers.md). Resource-area geometry and resource tags remain declared inputs in this batch; the original generator is a separate producer.

The target is PE32 StarCraft **1.17.0.1**, preferred base `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs; subtract the base for RVAs. [The execution report](evidence/native-resource-areas-probes.json) records **2,062 complete original calls / 17,397,177 instructions**, zero prefixes and zero helper stubs, 238 visited range starts, 1,186 reviewed ranges and82 imported source hashes. The largest call uses71,827 instructions under an8,000,000 bound. Rosetta x86_64 Python3.9.6, Unicorn2.1.4 and pefile2024.8.26 execute private original code, with instruction/write guards and sentinel/stack-return checks. There is no process startup, Windows import, guest host-file/network access, renderer or complete gameplay execution.

The [probe](../../tools/research/probe_native_resource_areas_117.py) SHA256 is `8fe0ac0e7d0d2fc1206c8544039646f296550a5bf86ff091874dda7c8b0e7089`. The [inspector](../../tools/research/inspect_native_resource_areas_117.py) SHA256 is `731566c9014bc47532a4603d95c4f8990810b0368449a8d5620417ef42c05ba5`; [its report](evidence/native-resource-areas-static.json) has18 blocks /861 decoded instructions without undecoded tails. Private stock DAT/GRP/LO/IScript provenance reuses the unchanged [construction assets](evidence/native-construction-assets.json). Each case independently validates the original integer RNG transition against all256 source counters.

## Original objects and caller inputs

Actual pool initialization and unit allocation/completion create Command Center106, MineralField176/177, Geyser188, Refinery110, SCVs7 and selected Depots109. Actual AIScript pool creation and full VM startup run synthetic `start_campaign`, `start_town`, configuration/build instructions and `wait1`. No host-crafted CUnit, WorkerAi, BuildingAi or Town record substitutes those producers.

The resource-area array is at`6a2e88`, with250 records of48 bytes. Index0 is the sentinel. Global`6a5d68` is the used count; `6a5d6c` is the refresh countdown. Record fields used here are signed WORD x/y at+`00/+02`, BYTE mineral/gas counts at+`04/+05`, start marker at+`06`, flags at+`07`, and DWORD remaining mineral/gas sums at+`08/+0c`. The [pinned Teippi structure](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.h) corroborates the layout; the exact executable instructions establish its consumers.

Explicit inputs include two area coordinates, initial metadata, CUnit resource amounts at+`d0`, tags at+`d8`, player/controller/race/start positions, a flat visible/walkable terrain plane, a prepared two-region graph, empty contours, self-alliance and finite native pools. Native regional cached-strength inputs remain those of this fixture. Unit resource tags and array coordinates are not outputs of an executed original area generator. The host supplies elapsed tick0 for Town allocation, so positive stale-worker migration is outside this batch. No full object callback or outer accepted-frame caller advances the worker allocations here.

| Cases | Complete selected contract |
|---:|---|
| 7 | Refresh cadence, resource totals, occupied-building bit and actual resource Death exclusion |
| 6 | Campaign point versus melee start-position lookup, count and equal-distance behavior |
| 4 | First mineral in the actual active list, zero amount, Death and other-area tags |
| 7 | Zero through four gas candidates, sorting, zero amount and actual Death |
| 30 | Worker limits across mineral-total boundaries and zero/one/two native Towns |
| 4 | Mineral getter and limit bounds, including valid index249 |
| 4 | Free-area enumeration and flags/cursor behavior |
| 19 | Native Town binding, gas-stock thresholds, actual spending desire and WorkerAi source labels |

## Refresh cadence, amounts and occupation

Complete `496c80` returns unchanged when the used count is0. With a nonzero countdown it decrements once and returns, even when that decrement reaches0. A later invocation with countdown0 performs the refresh and writes450. The selected inputs0/1/450 distinguish these states; this is invocation cadence, not a measured wall-clock duration.

The refresh clears each used area's counts/totals and only flag bit1, preserving the start marker and other flag bits. It scans the actual active CUnit list through `425600`, ignores objects accepted by the real Death predicate`41fe40`, and uses stock resource-property/mineral classification plus the unit's nonzero area tag. Mineral and gas amounts accumulate separately; the number of qualifying objects is separate from their remaining amounts.

The baseline area1 contains two minerals750/250 and one gas5000; area2 contains a Refinery2000. The resulting counts/totals are2/1/1000/5000 and0/1/0/2000. Genuine `42a4d0` on the first mineral or Geyser changes the corresponding refreshed count/total without reclaiming the CUnit first. Zero tags suppress aggregation. No host Death-order write supplies those cases.

For nonresource stock buildings, original region/group acceptance`481d00`, approximate distance at most384 pixels and native connected distance`496930` at most12 determine occupied bit1. The nearby Command Center marks area1; the far area2 loses only bit1. The stock Refinery takes the resource branch and does not independently mark occupation through the building branch. Other distance/group/airborne/burn/destruction profiles remain separate.

## Lookup order is observable gameplay behavior

`496bf0(player,x,y)` uses the supplied point and used count when the player's actual campaign flag is set. Otherwise it replaces the point with the player's start-position WORDs and scans **eight** area records, independently of the supplied used count. It examines indices1 through the selected count inclusively. A strictly worse distance is skipped; **equal distance replaces the result with the later index**. A campaign count0 returns0. The tested symmetric midpoint chooses area2; melee cases prove that the input point/count are overridden. Actual game-profile initialization of those eight records remains a producer boundary.

`4964b0(area)` returns the first non-Death mineral with a matching tag in the actual active-unit linked-list order. It does not check remaining resource amount: a zero-amount mineral still wins. This is not nearest-mineral selection.

`496510(area,out1,out2,out3)` zeroes its outputs and scans non-Death stock resource objects that are not minerals. It keeps the first candidate in output1, the second in output2, and **overwrites output3 for every later candidate**. It then sorts only that retained triple by the original approximate distance from the area coordinate. Strict comparisons preserve equal-distance ordering. The four-Geyser fixture proves that an omitted candidate can be closer than a retained one. Zero amount does not exclude candidates; actual Death does.

`496500(cursor)` initializes the cursor0 then enters `496740`. Each advance increments the cursor, scans up to the used count inclusively and accepts only flag bytes with neither low bit set. Occupied1 and reserved2 are both excluded; unrelated flag`0x80` remains eligible. Failure returns0 with cursor at count+1. These flags are explicit inputs here, so original strategic reservation producers remain separate.

## Worker limits depend on player Towns

`486130(player)` sums mineral-count bytes for every nonzero area tag in the player's actual native Town list. It does not deduplicate tags. The two-Town fixture intentionally assigns the same area to both native Towns to discriminate this sum from a unique-area count; it does not certify an original producer of that duplicated configuration.

Let `M` be the selected area's unsigned mineral-total DWORD, `m/g` its mineral/gas object-count bytes, `T` the above player Town sum, and `q = floor(M/500)`. Complete `496690(area,player)` returns:

```text
0                              if area is 0 or >=250
m + 3*g                        if q < T
min(q, 2*m) + 3*g              otherwise
```

Complete `4966f0` uses the same decision without the gas term. The30 cases test totals0/499/500/999/1000/1499/1500/1999/2000/`0xffffffff` against zero, one and two native Towns. For example, an area with two minerals and one gas but total0 yields3 workers with no Town, and5 when an attached Town contributes its two mineral objects to `T`. This is the original low-reserve fallback, not a conventional saturation heuristic.

Mineral-count getter`496f20` similarly rejects0 and indices>=250. Gas-count getter`496f40` directly indexes the array and lacks those checks; only valid indices are used in the connected Town cases. The unchecked read is established statically, not tested as an arbitrary-memory API.

## Connected Town binding and spending-sensitive allocation

Complete `485020(Town)` consumes the genuine script-created Town and original units. With an initial area tag0 it calls the nearest-area helper, marks refresh needed, discovers/sorts gas pointers, assigns their regional state with actual `4b00a0`, finds the first mineral and updates WorkerAi limit through`496690`. The baseline two-mineral/three-gas area yields limit11. The host supplies no Town resource pointer, limit or WorkerAi source label.

The19 cases create2/4/5/10/15/20 actual SCVs and two completed Depots. Native `farms_notiming` suppresses incidental supply requests while supply remains nonzero. The first Town pass binds resources; complete `483400` then rebuilds player spending from actual script/automatic requests, and a subsequent complete Town pass assigns the native worker-list entries. Physical harvesting is outside this label-allocation composition.

The allocator starts with gas desire `floor(worker_count * gas_percentage /100)`, capped at four times the area's gas-object count. When current gas is below800 it also applies player-wide **completed** racial worker thresholds5/10/15/20, requesting at least1/2/3/4 gas labels. At stock800 this minimum is absent. The selected quantity is assigned to successive WorkerAi records, with labels2/3/4 in groups of four and wraparound; other workers receive mineral label1 in this profile. List order, categories and labels remain separate from successful downstream harvesting of the referenced slot. One selected slot is a raw Geyser; the label4 output does not certify a completed Refinery there.

Original `483030` derives desire from both current stocks and accumulated spending needs. A VM `build1 Ghost` gives needs25/75 and ratio75% when both stocks cover those needs. At15/20 workers, complete player processing also emits an automatic Refinery request because the bound area has an operating Refinery, a raw Geyser and enough minerals/workers. The combined needs125/75 produce **37%** gas desire. No host percentage write drives that change.

The `build30 Ghost` case at20 workers/gas5000 produces needs850/2250 after the same100-mineral automatic request, ratio72%, and12 gas labels through the area's cap. The observed assignment is four2, four3, four4, then eight1. The Ghost requests here are desire producers; actual Ghost training, prerequisite satisfaction and physical gas collection are not executed.

## Remake contract, reproduction and remaining producers

Keep area generation, periodic refresh, selected resource pointers, player spending desire and worker source labels as distinct simulation stages. Preserve active-list ordering, the gas third-slot overwrite, equal-distance replacement, low-reserve fallback and original automatic-request feedback into the next allocation. Replacing these with nearest-three selection or a fixed gas percentage would change the measured behavior.

Run the inspector with the authenticated executable and an output path. Reuse the [connected construction reproduction command](native-ai-construction.md#reproduction), substituting `probe_native_resource_areas_117.py`, `--case-group all` and the desired output filename. Groups `update`, `lookup` and `town` reproduce7/55/19 cases. Without `--execute-original-code`, the probe prints its plan.

R26 remains partial for original area generation/clustering and terrain producers, positive stale-worker migration/recovery, other races, all allocation categories and actual full-world scheduler interleaving. These81 cases establish the listed consumers under declared inputs; they do not certify campaign traversal or full gameplay parity.
