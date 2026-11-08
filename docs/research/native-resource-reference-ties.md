# Native mineral-reference ties and observable cluster partitions

Two complete paired compositions validate the equal-distance reference rule in original resource-area generation. They extend [the 42 generator cases](native-resource-generation.md) without modifying their frozen sources or evidence. The target is StarCraft 1.17.0.1, PE32 x86 base `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.

[The probe](../../tools/research/probe_native_resource_reference_ties_117.py) executes genuine allocation/completion, complete generator `497a20`, complete reference helper `495e70` and their original callees. [The report](evidence/native-resource-reference-ties-probes.json) records 2 passing cases, 28 complete original calls and 352,092 original instructions; the largest call uses 71,827 instructions under the 8,000,000 bound. It pins 85 imported source hashes, retail DAT/IScript and [construction graphics provenance](evidence/native-construction-assets.json). Execution uses Rosetta x86_64 Python 3.9.6, Unicorn 2.1.4 and pefile 2024.8.26. Every instruction and guest write is guarded, return/stack restoration is checked, and the final LCG state matches all 256 native RNG source counters. There are no helper substitutions, prefixes, Windows imports, process startup or complete-game execution.

## Paired native outputs

The declared fixture has flat equal-height/group terrain, empty contours, finite pools, visible tiles and no start positions. Original functions create a Geyser at `(1000,1024)` and three minerals at x800, x1200 and x1710, all y1024. Remaining amounts are explicit inputs: gas5000 and mineral1500 each. The fixture intentionally supplies resource amounts directly; an original CHK loader does not produce them here.

| Mineral creation sequence after the Geyser | Native active-list mineral sequence | Original selected reference | Original areas |
|---|---|---|---|
| 800, 1200, 1710 | 1710, 1200, 800 | x800, score6 | `(1000,1024)`: M2/G1, amounts3000/5000; `(1710,1024)`: M1/G0, amounts1500/0 |
| 1200, 800, 1710 | 1710, 800, 1200 | x1200, score6 | `(1255,1024)`: M3/G1, amounts4500/5000 |

The native active head is the Geyser in both cases. The probe checks the exact original-produced list, and captures `495e70`'s chosen CUnit in EAX and its score at `4960a1`, before the original epilogue. Both chosen pointers are asserted against the genuine mineral objects. This is direct pointer/score observation, not a reference choice inferred only from the final bounding box.

The minerals at800 and1200 are equally distant from the gas seed: approximate distance200, same-region score `floor(200/32)=6`. Mineral1710 scores22 and cannot become the reference under the maximum20 rule. The [unchanged static witness](evidence/native-resource-generation-static.json) shows replacement rejects a score only when it is greater than the current best, then rejects values above20. Equal scores therefore replace the current reference: the later eligible **active-list** candidate wins.

In these creation scenes, the active-list sequence differs from creation sequence. Treating the later-created mineral as the tie winner would reverse the observed partitions. This result concerns the recorded original list; it does not assert one insertion rule for every entity lifecycle or accepted-frame mutation.

The chosen reference affects subsequent collection. With reference800, mineral1710 is outside its mineral collection bound and becomes a separate area. With reference1200, all resources are collected together, and the bounding-box midpoint is1255. No host reorder, type, position, order, classification, area tag or candidate-pointer write supplies either outcome.

## Reproduction and limits

Use the executable, stock data, private construction graphics and pinned PyMS paths from [generator reproduction](native-resource-generation.md#remake-contract-and-reproduction), substituting tool `probe_native_resource_reference_ties_117.py` and a separate output file. The executing invocation requires `--execute-original-code`; otherwise it prints an inert plan. The report pins its exact probe hash and dependencies.

Preserve active-list traversal, equal-score replacement, the separate20 reference cutoff and later collection bounds in the simulation. These two cases establish a selected original tie and its downstream partition effect. Authentic multi-region/elevation inputs, full loader startup, dynamic blockers, Town scheduling and campaign gameplay remain separate evidence.
