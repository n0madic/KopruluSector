# Native 1.17 map bounds and unit collisions

**3,225 complete original cases pass, with12,285 complete entry returns.** They recover selected terrain/map-bound wrappers, pair eligibility, the actual swept spatial query and first-blocker consumer. Stock extents and explicitly sorted endpoint tables are supplied fixtures; unit creation, spatial insertion and full movement do not execute. Target: StarCraft.exe1.17.0.1, SHA-256`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase`0x400000`.

Evidence: [12 static blocks,1342 instructions](evidence/native-movement-collision-static.json), [complete cases](evidence/native-movement-collision-probes.json), [runner](../../tools/research/probe_native_movement_collision_117.py). Every reached instruction is allowlisted by exact address/size. No callable helper replacements, Windows imports, game process or guest host-file/network access execute.

## Terrain versus map-bound wrapper

`0x42d990(ECX=unit)` consumes signed destination WORDs at globals`0x543280`/`0x543284`. Its branch is **CUnit status`0x100000`**, rather than a test of airborne bit4:

| Branch | Complete behavior |
|---|---|
| Bit`0x100000` clear | Reject if destination x−left<0, y−up<0, x+right≥map_width or y+down≥map_height; otherwise return0 |
| Bit`0x100000` set | Subtract current signed Sprite coordinates from destination, call actual terrain-cache consumer`0x42da50`, return its collision result |

The first branch reads stock left/up/right/down extents as signed16-bit values and map dimensions as unsigned16-bit pixels.228 grouped stock cases each run nine edge/signed destinations under status0 and4, totaling4104 complete wrapper calls. All stock extents have the ordinary positive range; arbitrary malformed DAT extent overflow is not generalized from these tests. Lower-bound equality is admitted; right/bottom equality with map size is rejected.

Another108 cases compose the bit-set wrapper with genuine extent initialization, cache contraction and sweep on declared directional walls. They compare the returned result and all four stored safe-bound WORDs. This extends [terrain cache/sweep](native-terrain-sweep.md), while authenticated real-map arrays are covered by [the all-map composition](native-campaign-terrain-sweep.md). Those entries do not determine when a naturally created unit receives or changes the controlling status bit.

## Pair eligibility is separate from the spatial/flag filters

`0x42ebf0(ECX=blocker,EDX=mover)` supplies the pair policy.1152 cases vary both IDs0/35, eight blocker status profiles, three mover profiles, two blocker/mover orders and relationship values0/1/2. Nine additional cases validate neutral ownership conversion.

The complete policy in branch order is:

1. Blocker status`0x400000` allows passing when actual hostility helper`0x4290e0(mover,[blocker])` returns true. It indexes the mover-to-blocker relationship; table value0 is hostile. A blocker owned by11 uses its **Sprite owner byte+`0x0a`** for that relationship.
2. A mover of ID35, **Zerg Larva**, collides only when blocker status bit2 is set. For every other mover ID, blocker ID35 is excluded.
3. A mover with status`0x800000` passing a blocker without bit2 requires the blocker to have the same status bit. Even then, mover order84 (**ReturnGas**) rejects the pair; otherwise blocker order82 (**WaitForGas**) is required.
4. Other admitted combinations return1.

This direct helper does not itself reject the same unit, absent spatial membership or blocker status`0x200000`. The surrounding consumer/query supplies those filters. Combining all predicates in one function is possible, but its callers must preserve the original division of responsibility. The original complete paths agree with the equivalent pinned reconstruction's pair policy after accounting for filters outside this native entry. [OpenBW collision policy](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L1567-L1590).

The bit2/type/order combinations are controlled distinguishing states. They do not claim that every combination occurs naturally or that an arbitrary unit with a harvesting bit has executed its real harvest-order lifecycle.

## Swept candidate construction and ordering

`0x4217f0(ECX=mover,stack=[delta_x,delta_y])` pushes actual query-stack state at`0x5d6e48`, remembers the storage cursor and returns a null-terminated pointer list. CUnit stores endpoint indices at`+0x13c/+0x140` for x and`+0x144/+0x148` for y. The eight-byte sorted records hold one-based unit index and signed coordinate; actual resolver`0x4255f0` returns CUnit base`0x5da3a0`, with stride`0x150`.

The source Sprite flag`0x20` forces both deltas to0. Otherwise it scans the relevant x edge first, then the y edge, preserving direction-specific sorted/reverse order. Current and destination edge coordinates are **inclusive**. An x candidate must overlap the source's y interval translated by delta_y; a y candidate must overlap the x interval translated by delta_x. The source unit is excluded. Candidate eligibility status and pair rules are not applied in this constructor.

The same unit can appear more than once: both endpoints can be crossed, and both axes can contribute. No deduplication is performed in the observed query paths. The actual release`0x4222b0` restores stack depth and saved storage cursor; every tested composition checks both return to0.

## First-blocker consumer and controlling bit

`0x42d900(ECX=mover)` derives the prospective delta from the same destination globals, calls actual swept query and visits its pointer list. It returns the **first** candidate passing:

- Candidate pathing byte+`0x105` bit1 is present.
- Candidate status`0x200000` is absent.
- Candidate is different from the mover.
- Complete pair policy`0x42ebf0` accepts.
- Candidate status`0x100000` is present.

It releases the query even after finding a blocker, then returns the pointer or0. Wrapper`0x42da30` runs this consumer only when the **mover** has bit`0x100000`; otherwise it returns0 without query construction. These bit tests are independently executed rather than inferred from community names for the fields.

1728 complete cases compose direct query/release, the blocker consumer and its mover gate. Six stock mobile types, eight cardinal/diagonal directions, deltas1/20/100 and twelve profiles exercise missing membership, absent collision bits, Larva, hostile passing, harvesting orders, hidden source and reversed supplied ties. Native stock extents determine all tested endpoint boxes, including the Larva profile.

| Marine fixture | Actual candidate indices | Actual blocker |
|---|---|---:|
| Right100, coincident blockers2/3, supplied ties2 before3 | 2,3,2,3 | 2 |
| Same query with reversed endpoint ties | 3,2,3,2 | 3 |
| Diagonal20/20, blockers at430/430 | 2,3,2,3 | 2 |
| Diagonal100/100, same intermediate blockers | Empty | None |

The last row distinguishes this native edge query from an invented swept-box union or continuous segment test: its orthogonal filter uses the **translated final interval**. A large diagnostic jump can therefore skip an intermediate obstacle. These are supplied query deltas, not a claim that normal Flingy speeds permit that jump. The first-blocker rule is also not a nearest-distance search; real insertion/tie history remains a separate producer contract.

## Reproduction and evidence limits

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_movement_collision_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --units-dat /private/tmp/koprulu-campaign-data/arr/units.dat \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --static-out docs/research/evidence/native-movement-collision-static.json \
  --execute-original-code \
  --out docs/research/evidence/native-movement-collision-probes.json
```

Runtime: Python3.9.6 x86_64 Rosetta, Unicorn2.1.4, pefile2024.8.26, Capstone5.0.6. Unchanged units.dat SHA-256`da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba`; pinned PyMS`bfc5d3aad0b5614a5aff72c223f8efa00afddfa4`. Runner SHA-256`e2e64671450633a628cce3d5309856fda6c458a3e44d06517f6f04f13d482e19`. Maximum observed entry length489 instructions, bound100000. Static regeneration is byte-identical; an unknown EXE hash is rejected before emulation/output publication.

All228 stock-bound cases,1161 pair/ownership cases,108 terrain-wrapper cases and1728 query/consumer cases complete without unresolved calls. CUnits, sprites, relationship table and sorted endpoint records remain **explicit fixtures**. This does not close original endpoint insertion/reordering, all pair-state producers, cache invalidation, local paths, collision avoidance/slide response, full movement FSM or campaign behavior. The remake should preserve these algorithms and their query order in authoritative simulation; rendering interpolation is a later consumer.
