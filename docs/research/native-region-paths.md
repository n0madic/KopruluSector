# Native 1.17 region paths, AI distances and defense anchors

This report recovers the original region search `0x491300`, the connected AI placement-distance caller `0x496930`, nearest hostile selector `0x48dff0`, and route-cut/anchor contracts in the reviewed **StarCraft 1.17.0.1** executable. SHA-256: `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`; preferred ImageBase: `0x400000`. [Static witnesses](evidence/native-region-paths-static.json) contain **20 instruction blocks and 1,412 instructions**. [Execution evidence](evidence/native-region-paths-probes.json) contains **505 passing complete original-function calls** from the [probe tool](../../tools/research/probe_native_region_paths_117.py).

Original instructions and reviewed callees execute in private Unicorn x86 memory. Execution is hash gated, instruction allowlisted and bounded at 2,000,000 instructions per call. The probe verifies the original return address and stack cleanup. No helper stubs, game loop, Windows API, imported allocator, or guest host file/network operation executes. Region graphs, raster lookup cells, centers, unit lists and DAT flags are supplied fixtures. Their production from authentic terrain remains separate. Positive repeated cuts require an imported allocator and are **static evidence**, whereas single-cut selection and complete zero-cut producer paths execute.

Two results affect the remake directly. Capacity exhaustion can return a path ending before the requested destination with the partial byte still0. The integer distance heuristic can also return a route longer than an available alternative. These are measured original outcomes on declared synthetic graphs; their occurrence in specific campaign terrain is not established.

[Region generation](native-region-generation.md) now executes the complete rectangle/adjacency/group/split/rank builder and connects 22 long-path calls to its generated synthetic terrain graphs. Authentic campaign import and collision contours remain separate; this extension does not change the supplied-graph scope of the 505 calls below.

## Region and search representation

Region records start at `*(0x69b584) + 0x449fc + region_id*64`. The declared search consumes:

| Relative offset | Native width | Meaning established here |
|---|---|---|
| `+0` | u16 | Literal flags/value tested by the hostile selector against `0x1ffd` |
| `+2` | u16 | Connectivity group |
| `+6` | u8 | Ground-neighbor prefix count |
| `+7` | u8 | Full neighbor count |
| `+8` | pointer | Temporary search-node backreference |
| `+0xc` | pointer | Ordered u16 neighbor IDs |
| `+0x10/+0x14` | dword | Fixed8 region center x/y |
| `+0x20` | u8 | Route-cut rank consumed by `0x481a90` |

The routine uses a stack arena of **350 nodes**, each24 bytes: closed byte at0, region ID at2, region pointer at4, parent at8, heuristic at`0xc`, accumulated cost at`0x10`, and priority at`0x14`. A one-based pointer heap begins at `0x69b588`; count is `0x69b780`, sentinel pointer `0x69b784`. The sentinel's priority is signed -1, written into its stack location. The initial node's parent/heuristic/cost/priority dwords come from actual constant `0x4efa00`: `[0,4194304,0,4194304]`.

The caller supplies signed16 source/target pixel coordinates at context`+4/+6/+8/+0xa`, and u16 source/target region IDs at`+0x160/+0x162`. The search writes partial byte`+0x1c`, used-node count u16`+0x164`, peak-open count u16`+0x168`, full parent-chain count **low byte**`+0x154`, stored-path count u16`+0x86`, cursor u16`+0x84`, and up to50 region words at`+0x20`.

Equal low16 region IDs return0 **before resetting any output**. Three complete calls distinguish ordinary equality and high input bits, preserving the entire context byte for byte. Callers must initialize fields according to their own contract; a false return is not a freshly empty context.

## Search, heap ties and costs

With equal connectivity groups, search proceeds source→target. Every popped non-goal node uses its record's ground-neighbor count when its group equals the original source group, otherwise the full neighbor count. Order is the stored neighbor-array order. The adjacency producer is independent of this consumer.

Source and target regions use the supplied pixel endpoints shifted left8 for node positions; other regions use their fixed8 centers. Actual approximate-distance helper `0x405cf0` supplies edge cost and heuristic. A neighbor outside the original source group receives the native cost expression `signed_truncate(u32(distance << 9)/256)`. In the ordinary nonoverflowing domain this doubles the cost. Preserve the original wrapping shift and signed truncation for artificial overflow boundaries. Accumulated cost and priority are dwords; their comparison branches are signed.

A newly encountered region allocates the next stack node, sets its parent/cost/heuristic, writes region`+8`, and inserts it into the heap. A strictly smaller accumulated cost updates an existing node unless it is the current node's immediate parent. An open node goes through actual priority helper `0x491a70`; a closed node is reopened and inserted again.

Heap insertion and upward priority repair move parents on **equal** priorities. Downward repair `0x491b20` chooses the left child when sibling priorities tie and stops when the current priority is less than or equal to the selected child. Equal-priority insertion therefore favors the later inserted candidate; it does not sort region IDs. Two symmetric routes with reversed neighbor arrays produce `[1,3,4]` and `[1,2,4]`. Six standalone original priority-helper calls cover increase, decrease, equality, unchanged priority and an absent node. The absent-node call still updates that node's priority before discovering it is outside the heap.

One hundred independently generated positive-weight graphs are compared with Dijkstra, using the established fixed8 distance arithmetic. Every returned route in that set has valid edges and the independently calculated minimum cost. Four of those original calls reach open-node priority decrease. These cases do not establish universal shortest-path behavior.

Four further fixtures reach **closed-node reopening**, followed by open-node priority decrease. Their points, before scale, are source `(4,19)`, alternative `(4,20)`, previously closed region `(53,32)`, detour `(70,90)`, and goal `(63,40)`. Scales1/8/16/64 execute the same transition, ending with path `[1,2,3,4,5]`.

Removing that detour exposes early goal exit: original path `[1,3,5]` is longer than `[1,2,3,5]`, at all four scales. At scale1 the fixed8 returned cost is16003 and the alternative is15952. The routine exits when the goal is popped; the approximate heuristic's ordering permits the longer route to win. Both routes and every fixed8 coordinate are recorded. These supplied graphs are algorithm probes, not certified generated campaign region graphs.

## Exhaustion, group reversal and output truncation

The search stops when the open heap reaches125 or the next free stack-node pointer reaches the end of the350-node arena. If there is still an open node, it pops the best one as the result. **It does not set partial byte1 for this capacity branch.** A chain of351 regions returns a chain ending at350, and a255-neighbor root stops after adding the first125 neighbors. The requested goal is absent from those results while partial byte remains0. Seventeen chain-length and five heap-capacity cases distinguish these boundaries.

If the open set empties, the routine sets partial byte1 and selects the visited node with the strictly smallest heuristic. Equal values retain the first node in **allocation order**. The initial node's heuristic is recomputed from its **region center**, rather than the supplied source point. A differing-source-point fixture distinguishes those two geometries. Seven additional cases distinguish nearest visited nodes and ties.

For differing source/target groups, the first search runs target→source. If it reaches the source, the output follows parent pointers from source and stops at the first node outside the original source group. If it ends elsewhere, it clears all temporary backreferences and runs a forward search from the source toward the selected region. That restart bypasses the initial partial-byte reset: a failed reverse search leaves partial1 even when the forward search reaches its selected goal. Two neighbor-count fixtures distinguish ground-prefix traversal from full-neighbor traversal in the reverse phase.

Ordinary output counts the entire parent chain, drops target-side nodes until at most50 remain, and stores the **source-side prefix**. The byte at`+0x154` stores only the low8 bits of the full count: a350-node chain stores94 there and50 at`+0x86`. It writes cursor65535 and clears the temporary backreference of every node allocated in the final search. Group-reversed output keeps only the initial same-group segment, also capped at50. All declared completed path cases verify backreference cleanup; group restart clears the first phase separately.

## AI placement distance

`0x496930(ECX=source_x, EDX=source_y, stack=[target_x,target_y])` resolves both pixel positions through actual `0x481b10`:

1. Equal region IDs return approximate pixel distance divided by32, with signed truncation toward zero.
2. Different connectivity groups return9999.
3. Equal groups with different region IDs call the complete original long-path routine.
4. A false path return gives9999. A returned route of at most2 regions uses direct approximate pixel distance/32.
5. A longer route sums source point→second region center, intermediate center segments, and the last retained intermediate center→the original target point, then shifts the dword total right5.

This caller **does not check the partial byte or require that the retained path ends at the target region**. Twenty complete cases distinguish same-region, direct-edge, detour, disconnected and differing-group inputs at pixel offsets0/1/15/31. Same-group disconnected input can return direct distance6; differing-group input returns9999. Six longer zigzag paths distinguish truncation at50 and the final straight segment from the retained prefix to the original target. This closes the connected-distance consumer previously left open in [AI placement](native-ai-placement.md), for supplied graph/raster state.

Radius filter `0x4976d0` contains its own copy of this distance calculation and invokes `0x491300` directly. Sixty-four complete calls cover its connected detour, same-region, differing-group and unreachable inputs, equality at the unsigned limit and limit `0xffffffff`. The filter preserves a nonzero byte on acceptance and clears it on rejection. Its candidate pixel point is **original signed16 seed plus32*(column-32,row-32)**, retaining seed sub-tile offsets; it does not quantize that seed before measuring distance. The detour fixtures vary offsets0/1/15/31 and record the resulting13/14 tile distances. Other grid-filter stages keep their separately recovered geometry.

## Nearest hostile selection

`0x48dff0(ECX=current_player, EDX=list_player, stack=[x,y])` follows that list player's original unit head at `0x665b08 + player*4`, then unit`+0x6c`. It rejects:

- Actual inactive predicate `0x41fe40`: null sprite or primary Death order0.
- DAT Subunit property`0x10`.
- Actual hostility predicate `0x429110` returning false.
- Actual position-region predicate `0x481bb0` matching literal word`0x1ffd`.

Hostility uses the unit owner, except owner11 uses the sprite owner byte`+0xa`; the corresponding alliance byte must equal0. It is an alliance predicate. The body reads no fog/detection/visibility eligibility field. Completed status, hidden status, disabled status and hallucination status do not independently reject a candidate in this selector. Their surrounding list-membership producers remain separate.

For accepted candidates the routine uses sprite signed16 x/y and the requested point, maintaining unsigned distance strictly below99999999. Equal distances keep the first accepted unit in player-list order. Thirteen gate cases, three distance/tie cases and an empty-list case execute the entire actual chain, including pixel→region lookup.

## Route cuts and anchor production

Single-cut helper `0x40e4e0(ECX=context, EDX=maximum_index, stack=[cut_out,minimum_index])` runs the actual long path. False return or partial byte1 gives status2 and preserves cut output. It then limits the scan to `minimum_index..min(path_count-2,maximum_index)`, inclusive. Insufficient path/range gives4. It reads each region's u8 rank through `0x481a90`, skips rank0, and accepts a rank greater than **or equal to** the current best. Thus equal nonzero ranks select the **last** scanned region. No eligible rank gives3; selection gives0 and writes the u16 region ID. The helper itself does not clamp the supplied minimum to2. The 210 range/tie cases plus six rank/no-route cases establish these distinctions with complete original calls.

Repeated-cut entry `0x40e190(ECX=source_region, EDX=target_region, stack=[maximum_index,cut_array,status_out,minimum_index])` clamps minimum to unsigned max(2,input). Equal region IDs or a smaller maximum give status4/count0; different groups give2/count0. It constructs endpoint coordinates from actual region centers and calls the single-cut helper. Twenty-four complete cases establish its **no-allocation** outcomes, including zero rank, short paths and exhaustion.

The positive tail is recovered statically. After a cut, `0x40e2f0` backs up the neighboring regions' adjacency arrays and removes their references to that cut region, decrementing both neighbor counts. It then searches again. Up to3 cuts are retained; a fourth successful search is temporary. With a fourth route, a new route length exceeding the initial length by more than10 returns count3/status1; otherwise it returns count0/status3. A later no-eligible-rank result also returns0. Other failed alternative searches return the cuts accumulated so far and status0. `0x40e480` restores backed-up counts/arrays and releases bookkeeping nodes.

Allocation at `0x4d001c` dispatches through IAT `0x4eb55c`, original **Storm.dll ordinal401**. It is identified from this PE's actual import table. Successful adjacency backup, restoration and positive repeated-cut execution remain separate from the completed single-cut/no-allocation calls; no replacement allocation is used.

Anchor producer `0x496e10(ECX=player, EDX=source_region, stack=[maximum_cut_count])` clears count`0x6a2e84`, obtains the source center, and visits players0..7 with alliance byte0. For each it selects the nearest hostile unit, resolves its region, and tries maximum indices2..6 through the repeated-cut entry. It accepts the first nonzero cut count at most the supplied limit, appends cut IDs at`0x6a5d70`, and suppresses duplicates in stored order. Four complete allied/empty/zero-rank/different-group cases return count0 and leave the stale ID-array payload unchanged.

The reviewed repeated-cut body returns at most3, and the producer visits8 players, establishing a **per-call upper bound of24 distinct anchors** before deduplication. This is a static producer bound, not an assertion that24 is a common campaign result. The instruction scan records count reads and the producer's reset/update writes; it is a linear `.text` scan with stated alignment/data limits. [Final anchor choice](native-ai-placement.md) consumes the count and those IDs; positive generated anchors still need the allocator-backed cut path and authentic terrain state.

## Reproduction and remaining boundaries

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_region_paths_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --static-out docs/research/evidence/native-region-paths-static.json \
  --execute-original-code \
  --out docs/research/evidence/native-region-paths-probes.json
```

Default mode performs static inspection only. The execution report records its script hash, runtime versions, allowlist, case inputs, actual call/branch counts, memory outputs and expected results. The 100 generated graphs record their concrete adjacency and points as well as seeds. The complete original calls establish the declared consumer behavior on supplied inputs; they do not establish every terrain-produced graph or campaign scenario.

Pinned [OpenBW long-path implementation](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L9191) corroborates the350/125 bounds, reversal, heap structure and source-side50-region prefix. Its C++ fields/container types do not prove the native u8 full-count storage or persistent partial byte on restart. The local instruction and execution evidence defines those contracts.

For Defold, preserve the original neighbor-array order, equal-priority heap rules, integer cost widths, bounded search outputs and the placement caller's treatment of partial paths. Region generation/splitting, native local paths, terrain contours, dynamic collision, movement/Flingy state transitions and allocator-backed repeated cuts still need their own specifications and original evidence. [Completion ledger](completion-ledger.md) keeps those obligations open.
