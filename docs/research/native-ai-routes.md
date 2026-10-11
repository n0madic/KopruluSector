# Native 1.17 military reachability and strategic route policy

Two original AI route mechanisms have different acceptance rules. This batch recovers the cached military flood at`0x4abe90` and strategic intermediate-region filter`0x496ac0`, including their real callers. **48 complete original-code cases pass without helper stubs.** Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase`0x400000`.

Evidence: [two additional static witnesses,165 instructions](evidence/native-ai-routes-static.json), [48 complete native cases](evidence/native-ai-routes-probes.json), and [inspector/probe](../../tools/research/inspect_native_ai_routes_117.py). Imported reviewed original callees include native neighbor accessors, scalar/REP memset, air-weapon getter, long-path heap/search and the surplus donor selector. Fixtures supply private region graphs, target pointers, strength caches, geometry and DAT values. No Windows import, replacement helper, WinMain, full game, host file or network operation executes.

## Cached military flood

`0x4abe90(ECX=player, EDX=source_region)` writes current game time`0x5349d8` into player cache time`0x6b68c0 + player*4`, writes the source ID into`0x6b68e0 + player*4`, and clears2500 classification bytes at`0x6b1aa0 + player*2500`. It marks source byte1, then repeatedly scans **IDs1 through actual_count-1** in ascending order.

For a pending byte1, it marks the region2 and visits its **ground-neighbor prefix** through actual`0x481ab0`. An unmarked neighbor becomes1 when both its air-target+`0x1c` and ground-target+`0x20` pointers are null, or2 when either is nonnull. Marking a neighbor causes another scan pass. Consequently a threatened neighbor is counted as reached but **does not expand the flood**. The seeded source expands even when its own target pointers are nonnull. Region0 is excluded from scanning: explicitly seeding source0 leaves byte0=1 and every other byte0.

Ten blocked-chain cases distinguish no target, target at source, each interior region, and target at the endpoint, for both pointer fields. Two cyclic/backward-ID graphs prove repetition across scan passes and cycle termination. Four cases vary intermediate states3/6/8 and flags`0x20`/`0x40` without targets: those fields do not block this flood. Ground adjacency and target-pointer policy matter separately from later region-state policy.

Caller **`0x4ab330(ECX=player, EDX=source, stack=[target])`** returns1 immediately for player>=8 or equal source/target IDs. For different IDs it compares actual terrain connectivity groups and returns0 on mismatch. Equal groups allow a direct full-neighbor match, or a nonnull source air-target whose actual air-weapon getter is not sentinel130, to return1 without rebuilding. Otherwise it uses the cached classifications and returns whether target byte equals2.

Five early-out cases distinguish player8/11, equal IDs, group mismatch and direct adjacency, without calling the flood. Two source-target-weapon cases distinguish real weapon0 and sentinel130: the former bypasses an interior threat barrier. These helpers do not measure physical unit movement, detector visibility or actual combat progress.

The cache rebuild condition is **unsigned `now >= u32(saved_time +10)`**, or a changed source ID. It does not calculate an elapsed subtraction. Four composed cache calls distinguish time109/110 after a rebuild at100 and changed source1→2. With an unchanged source and newly blocked intermediate region, the target remains accepted at109 and becomes rejected at110; a new source rebuilds immediately. Three more cases distinguish backward time50 from saved100 and saved`0xfffffffa` plus10 wrapping to4. The JSON records preceding complete calls as well as the final call, so cache state is produced by actual instructions rather than an assumed fresh result.

## Strategic intermediate-region filter

`0x496ac0(ECX=player, EDX=source, stack=[target])` has a separate policy:

1. Equal IDs return1.
2. **Different connectivity groups also return1.**
3. With equal groups and different IDs, it gets actual centers and runs original`0x491300`.
4. A false path return, or a retained route of at most2 regions, returns1.
5. For a longer route, it checks only the retained interior region IDs, through actual`0x4aea70`.
6. An interior state3, nonnull air/ground target pointer, or flag`0x20` returns0. Other tested states/flags pass. Endpoints are excluded.

Eight chain cases distinguish state3, state8, flag`0x20`, flag`0x40`, each target-pointer field and equal IDs. Four more cases prove acceptance of differing groups, no-path input, direct adjacency and a threatened state3/flag`0x20` **endpoint**. A false search or accepted route therefore does not establish connectivity, arrival or safety in a general sense; preserve this caller's literal policy.

The original long path has a350-node arena and retains a source-side prefix of at most50 region IDs. These limits are independently recovered in [region paths](native-region-paths.md). Four complete calls request region351 on a351-node directed chain with8-pixel center spacing. The search reaches its arena bound and the filter checks only interior IDs2..49 of the retained prefix. A blocker at2 or49 rejects; blockers at50 or300 are ignored. Region50 is treated as the retained endpoint even though it is not the requested destination351. The filter does not require the stored route to end at the requested target or inspect the path's partial flag.

## Original donor consumer ignores the route return

One composed ground-donor case first executes`0x496ac0(7,4,[1])` on a chain whose intermediate region2 is state3, observing **return0**. It then executes the real surplus selector`0x4ac230` with a completed Zergling37 assigned to donor region4, actual current region4 and enough ground surplus. That selector calls the same original route filter on the same input, then continues distance ranking and returns the candidate's MilitaryAI pointer.

The instructions after the call do **not test EAX**. Thus adding a new eligibility condition that rejects a candidate whenever this helper returns0 would change original donor selection. The case returns from the selector; it does not substitute an allocation, prove the subsequent movement route, or claim the unit physically crosses that blocked region. Actual recruitment/assignment is separately composed in [initial recruitment](native-campaign-recruitment.md).

## Reproduction and boundaries

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_ai_routes_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-ai-routes-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-ai-routes-probes.json
```

Default mode only reads/disassembles the exact-hash EXE. No orders.dat or retail launch is required by these selected paths. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4, x86_64 Rosetta. Probe SHA-256: `82c8812cc85dfcfcb86f6a4d7b14d7217c9c9867077b7bf7a2e5da14de2ccb07`. JSON records script/dependency/runtime hashes, reviewed instruction ranges, actual arguments, complete returns and matched cache/output values. The bound is100,000 instructions; the longest complete case uses74,755.

Pinned [AISE AiRegion layout](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781) corroborates the consumed target/state/flag fields. Native instructions and calls establish policy here, separately from mod reconstructions. [Military/group behavior](native-military-ai.md), [radius recruitment](native-radius-recruitment.md), [strategic selectors](native-strategic-targets.md) and [region path search](native-region-paths.md) provide the neighboring contracts.

Remaining work includes multi-donor ranking and fallback priorities, a full blocked-route donor assignment/arrival composition, other regional state actions and requirement recipes, authentic generated campaign graphs consumed by these policies, and physical movement/combat. The supplied graphs and complete original returns are research evidence, not campaign walkthrough or full-game parity.
