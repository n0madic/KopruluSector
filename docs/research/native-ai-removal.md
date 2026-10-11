# Native 1.17 AI removal and town migration

The generic remover `0x48dbc0`, its killed-unit military branches, attack-roster removal, Medic reference replacement, and selected worker/building town lifecycles are recovered from StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at ImageBase `0x400000`. Evidence: [static witnesses](evidence/native-ai-removal-static.json), [113 isolated original-code checks](evidence/native-ai-removal-probes.json), and [inspector/probe](../../tools/research/inspect_native_ai_removal_117.py).

All **113 checks passed without helper stubs**. These are complete returns through the actual reviewed callees for the declared fixture paths, including spatial queries, list release, script cancellation, town deletion, and worker/Overlord reallocation. Original instructions execute in private Unicorn memory under an instruction allowlist. No WinMain, complete game, Windows API, or host-file/network operation executes. The checks close the applicable dependencies previously left open in [guard transfer](native-guard-transfer.md) and [worker town removal](native-workers.md); they do not establish every higher-level allocation or destruction caller's state invariants.

## Generic removal sequence

`0x48dbc0(ECX=unit, EDX=was_killed)` calls, in order:

1. `0x4b0770(unit,was_killed)`: clear regional references and remove military membership when AI type is4.
2. `0x486af0(unit,0)`: remove applicable worker/building membership and possibly delete or migrate its town.
3. `0x4b26f0(unit)`: clear an attached guard's parent and update its death counter.
4. `0x483220(unit)`: update the player's free-Medic reference.
5. Clear unit AI pointer at `unit+0x134`.

The earlier [guard-transfer report](native-guard-transfer.md) recovers the first helper's computer-player regional pointer cleanup, hostility-gated strength subtraction, and military record release. This batch executes the additional branches below. A null unit AI pointer does **not** skip the initial regional reference pass: one complete case begins with a null AI and still clears a matching regional target.

`was_killed` changes the military regional-state branch. It does not gate the state8 roster removal described next, and the enclosing remover always passes0 as the worker/building helper's second argument.

## Killed military unit and assigned-region state

For type4 MilitaryAI, `0x4b0770` reads the assigned region from AI+`0x10`. With region state8 it takes the roster branch immediately. Otherwise a region-state transition requires all of the following:

- `was_killed != 0`.
- Assigned-region state differs from0,1,2,8,9.
- Actual current-position region from `0x481ba0` differs from the assigned region's u16 ID at offset0.
- `0x4861a0(ECX=assigned_region.player, EDX=assigned_region.ID)` reports no owned building or worker in that region.

When these conditions hold, the helper resolves the region again through the assigned player's region array and calls actual `0x4ab900(region,3)`. For the nonzero old states reached here, this writes state3 and priority10, clears flag bits1 and`0x20`, and leaves the cached strength fields unchanged. Military list release then proceeds. Forty-four complete cases distinguish states0..10, killed0/1, and same/different position regions. They use matching region-record IDs and map cells, so the same-region result is not inferred from an inconsistent fixture.

`0x4861a0` obtains units in the region through actual `0x4222d0`, then accepts any candidate whose owner matches and whose unit DAT property low byte has a bit in mask`0x09`: Building1 or Worker8. This helper has no completed-status or inactive predicate. `0x4222d0` reads that region's rectangle from `map+0x44a14+region*64`, executes actual rectangle query `0x422330`, filters the result by each unit's actual position-region ID, and compacts the matching list. The caller invokes `0x4222b0` to restore spatial-query depth and scratch offset.

Six complete blocker cases use real sorted spatial arrays and the original rectangle-query chain. Owned properties1,8,9 in the assigned region prevent the transition. Property0, a different owner, or a different actual position region permits it. Query depth and scratch offset return to0. These are controlled static-plus-execution contracts; they do not claim that every synthetic status or ownership combination is a reachable campaign state.

## State8 attack-roster removal

`0x4839a0(ECX=unit_owner, EDX=current_unit_ID)` normalizes Tank siege ID30 to Tank ID5, then scans **63 u16 entries** at `PlayerAI+0x230`. It replaces only the first matching `unit_ID+1` with **229 (`0x00e5`)** and returns. It does not compact the array, move later entries, stop at a zero, or modify the following 64th word.

Ten complete cases cover duplicated Zergling entries, Tank normalization, a missing entry, a zero-filled roster, and a full nonmatching 63-entry roster, each with killed0 and killed1. For example `[31,6,6,0]` with removed ID30 becomes `[31,229,6,0]`: existing siege-form entry31 is not matched after the requested ID is normalized to5.

Pinned [AISE attack-force removal](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/src/ai.rs#L318) corroborates the deleted-entry marker convention, but its amount loop and extended-data hook are modified semantics. The native per-call single replacement, 63-word limit and Tank normalization are established independently by the local executable.

## Free-Medic replacement

`0x483220` acts only when the current unit type is34 and `PlayerAI+0x4d4` equals this unit pointer. It clears that field, obtains the player's unit list through `0x4256f0`, follows `unit+0x6c`, and stores the **first unit of type34**. It has no sprite, primary-order, AI-pointer, or explicit removed-unit exclusion test.

Nine complete cases distinguish the reference/type gate and replacement-list contents. If the removed unit still occupies the player-list head as a Medic, the helper immediately selects **the same pointer**; the enclosing `0x48dbc0` subsequently clears its AI. If the fixture player list already excludes it, another Medic with null sprite and null AI can be selected. With no Medic in that list, the field remains0. The surrounding native destruction caller's list-update order determines which situation occurs in live gameplay; these checks do not label either fixture as a confirmed campaign defect.

Pinned [Teippi MedicRemove](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L41) adds an active-player check and requires a replacement's AI pointer to be nonnull. Those changes must not be imported into the original 1.17 contract.

## Worker and building release

`0x486af0(unit,0)` first executes actual `0x486000`, then classifies applicable removal using DAT properties and current unit ID, rather than validating the AI type byte:

- Building property1 with ID other than188 takes BuildingAI release.
- IDs35 Larva,36 Egg,42 Overlord take BuildingAI release even without that property.
- Otherwise Worker property8 takes WorkerAI release.
- Other units return without releasing these records.

The ID188 exception can leave a synthetic assigned building record and town references untouched while the enclosing generic remover clears unit.AI. This distinguishes the literal classification boundary; it is not proof that assigning such a record to ID188 is reachable in the original game. The standalone helper's nonzero second argument forces the building branch and suppresses town deletion; that argument is never used by `0x48dbc0`.

Actual WorkerAI release `0x483b70(ECX=town+8, stack=[node])` uses the pool pointer at town+8, head at town+`0x0c`, and free head at pool+`0x5dc0`. BuildingAI release `0x483b10(ECX=town+0x10, stack=[node])` uses town+`0x10`, town+`0x14`, and pool+`0xabe0`. Each unlinks the node, repairs reciprocal links, and prepends it to the free list without zeroing its payload. Worker removal additionally decrements town byte+`0x1a` with byte wrapping.

The remover clears town+`0x24` and+`0x28` when either equals the removed unit, attempts town cleanup, and clears unit.AI. Twelve complete cases cover worker, ordinary building, Larva, Egg, Overlord, and the ID188 boundary with a retained or empty town. Three additional count cases distinguish byte0→255,1→0,255→254. Town emptiness follows the **lists**, so removing the final listed worker can delete the town even when the resulting count is255.

## Empty-town test and complete migration

`0x485840(ECX=town)` returns false immediately while global migration lock `0x69b558` is nonzero. Otherwise:

- No workers and no buildings returns true.
- Any BuildingAI parent whose current unit ID differs from42 Overlord returns false.
- A worker/Overlord-only town searches for another town in the owner's list at `0x69b51c+player*8`.

The search uses signed16 town coordinates at+`0x20`/+`0x22` and actual approximate-distance helper `0x405cf0`. The initial minimum is99999999; replacement requires a strictly smaller distance, so ties keep the first other town in list order. No recipient returns false and retains the donor town.

With a recipient, the helper sets the migration lock to1, visits remaining Overlord BuildingAI records first and WorkerAI records second, saves each next pointer, executes actual `0x48dbc0(unit,0)`, and then actual `0x484cd0(recipient,unit)` assignment. Released records are reused through the original free pools. The lock prevents recursive deletion during these removals. After both donor lists empty it restores the lock to0 and returns true.

**Twenty-four complete cases** remove the donor's final ordinary building and then migrate combinations of one/two workers and one/two Overlords. Recipient arrangements distinguish none, one, nearer-second, and equal-distance first-list choice. The cases execute the actual WorkerAI allocator and the actual Overlord BuildingAI allocation path. They set campaign flag`0x20` and completed/flying Overlord status5, whose reviewed path bypasses construction-defense selection. They do not substitute successful assignment or execute an unreviewed placement helper.

With a recipient, all remaining units' AI town pointers identify the selected town, the donor's lists/count empty, and its town record reaches the free pool. Without one, donor membership remains. If the second recipient is nearer, the owner's list still starts at the first remaining town after donor deletion; list head and chosen migration destination are distinct results. [Training lifecycle](native-training-lifecycle.md) separately recovers the reused native assignment/allocation entry points and their other selected paths.

## Town destruction, scripts and resource markers

`0x4856a0` returns if the empty/migration predicate is false. Otherwise it performs these original effects:

1. `0x493860(town)` removes matching active AIScripts and returns their records to the native script pool.
2. Iterate the global active-unit list from `0x425600`. Units passing actual inactive predicate `0x41fe40` and DAT property`0x2000` ResourceContainer receive byte`unit+0xd9=0`.
3. For nonzero town resource-area byte+`0x1c`, call `0x497380(area)` to clear flag2 at `0x6a2e8f+area*48`. Area0 skips this mutation.
4. Unlink the town from its owner's list and prepend it to the town free head at `*(0x69b518+player*8)+0xb3b0`. Other town payload fields remain.

The resource marker reset applies to **all qualifying units in the global active list**, not only that town's resources. Four complete cases distinguish no/mixed-town scripts and area0/1/250 arithmetic. Area250 is a controlled byte-index boundary, not an assertion that the map contains250 authentic resource areas. With two fixture scripts, only the matching-town script is freed and the other remains active.

Pinned [Teippi DeleteTown](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L627) corroborates script cancellation, global resource-marker reset and resource-area flag2. Its C++ deletion/list machinery differs from the executable's native fixed-pool recycling.

## Reproduction and remaining dependencies

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_ai_removal_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-ai-removal-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-ai-removal-probes.json
```

Default mode only reads and disassembles the hash-gated EXE. Execution mode uses Python3.9.6, Unicorn2.1.4, Capstone5.0.6 and pefile2024.8.26 under x86_64 Rosetta. The JSON records target, probe and dependency script hashes, runtime architecture/versions, exact allowlisted VA ranges, actual call traces, instruction counts, and matched memory results for all113 complete cases. Synthetic DAT fields and object lists are explicit fixtures; original callees are never replaced by responses.

Remaining work concerns higher-level callers' destruction/list ordering and pool restoration, other construction eligibility variants during transplanted BuildingAI allocation, and the standalone `0x486af0` forced-building argument. The selected Overlord campaign/flying allocation and all declared generic removal paths return through actual native callees. This report supplies implementation contracts for those paths; complete game execution and campaign parity remain separate evidence requirements.
