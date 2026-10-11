# Native 1.17 donor ranking and blocked-route adoption

**72 complete original-code cases pass**, closing multi-donor ties, selector precedence and the full selected grounded adoption path after a rejected strategic route. These calls execute the original donor selectors, military release/allocation, strength recomputation, arrival, RNG and primary-order issuer. Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`.

Evidence: [15 static witnesses, 918 instructions](evidence/native-donor-ranking-static.json), [72 complete cases](evidence/native-donor-ranking-probes.json), and [inspector/probe](../../tools/research/inspect_native_donor_ranking_117.py). The longest call executes3718 instructions. Private fixtures provide two or three units, linked player/military lists, cached strengths, region geometry/graphs, synthetic unit DAT entries and free pools. Stock orders.dat provides189 unchanged order fields. Every reached helper executes reviewed original instructions; no callable replacements, full game, WinMain, guest Windows API or host file/network operation executes.

## Ranking across several donors

Both `0x4ac230` (surplus MilitaryAI) and `0x4ac0d0` (guard/lower-priority fallback) walk the **player-unit list**, following Unit+`0x6c`. They rank eligible candidates by native approximate distance `0x405cf0` from sprite position to the supplied staging-center coordinates. Selection compares unsigned distance **strictly below** the current best; equal scores preserve the first eligible player-list candidate. The initial best is9999999. The tests stay within ordinary positive map coordinates; malformed-coordinate behavior and that distant initial cutoff are static observations only.

The positive-domain distance used by these cases is:

```text
small = min(abs(dx), abs(dy))
big   = max(abs(dx), abs(dy))
if small <= (big >> 2): return big
term = (small * 3) >> 3
return big - (big >> 4) - (big >> 6) + term + (term >> 5)
```

Twenty-eight direct calls cover both selectors, candidate-list permutations, two/three candidates, same/different assigned donor regions, axial/diagonal distances and exact ties. Two further calls follow equal-distance military selection through actual release, node reuse, destination-list insertion and order158. The first player-list node is adopted even when the tied sprite positions are reversed. This proves list-order dependence through the selected assignment path, rather than inferring it from a comparator alone.

Fallback source priority is a **strict eligibility threshold**, not an additional ranking score. With threshold60, a closer MilitaryAI donor of priority60 or160 loses to a farther priority59 donor. When both sources qualify, swapping their priorities0/59 leaves the closer donor selected. A guard does not use this source-region priority comparison.

Twenty calls place an ineligible or protected donor before a farther eligible donor. The surplus selector excludes source states7/8/9 in both campaign modes, while the fallback protects source states1/2 only outside campaign mode. Six calls also protect the receiver's destination region: for receiver state7,8 or9, a candidate assigned to destination region2 is skipped before a farther donor in region3. These are eligibility decisions before distance ranking. The existing [initial recruitment report](native-campaign-recruitment.md) defines the other unit, health and surplus gates.

## Selector precedence is observable

`0x4ae960(ECX=receiver,EDX=requested_ID,[priority])` tries the surplus selector first. Only a null result triggers the fallback. Six complete two-unit compositions mix guards and military candidates, vary their player-list order and enable/disable sufficient donor surplus.

A qualifying surplus MilitaryAI at distance128 is adopted before a fallback guard at distance32. When surplus is insufficient, fallback chooses the eligible closer candidate. Two guards always enter fallback and preserve its own distance ranking. The trace records whether `0x4ac0d0` actually executes; successful surplus adoption has zero fallback calls. These results do not imply that a guard is less valuable in a shared score: the original uses two sequential searches.

Successful guard adoption allocates the supplied free MilitaryAI node and clears the old guard's parent. Successful military adoption releases/reuses its own node. Only the selected unit gets destination assignment and order158; other candidates retain their old assigned region/order. Each selected ordinary arrival consumes three actual RNG calls. Free-pool exhaustion and the guard path's non-propagated allocation failure remain the independently recovered contract in [initial recruitment](native-campaign-recruitment.md).

## A rejected route does not prevent this adoption

Five full grounded compositions use the graph `1 ↔ 2 ↔ 3 ↔ 4`, a Zergling37 physically and administratively in region4, staging region1 in state8, and intermediate region2 with state0, state3, state8, flag`0x20` or flag`0x40`. The donor has cached ground contribution30 and total ground31, satisfying the selector's strict surplus gate. The staging destination is region2. Fixtures contain no enemy-target pointers.

Actual strategic route helper `0x496ac0(player,source,[target])` returns0 for intermediate state3 and flag`0x20`; state8 and flag`0x40` return1. The full adoption call invokes that same original helper from `0x4ac230` and records its returned EAX. **The following native selector instructions do not test that return**, so all five cases select the donor and complete adoption without fallback.

The original chain then executes:

```text
4ae960 → 4ac230 → 496ac0
       → 4a9a30 old military release
       → 4ae760 destination allocation / assignment
       → 4a9b20 arrival and original primary-order issuance
       → old-region strength recomputation
```

In both rejected-route cases, the unit's MilitaryAI is assigned to region1, the old region's ground-member count becomes0, the new count becomes1, the old military head clears, and the destination head becomes the released/reused node. Active ordinary order158 is issued with three RNG calls. `0x4ae960` returns1 and fallback is never called.

This is **assignment and order issuance**, not proof that subsequent physical movement reaches the requested destination. The arrival/reachability chain has a different blocker policy from the strategic route filter: its ground-neighbor flood uses enemy target pointers and ignores these state/flag combinations. Actual [military arrival](native-military-arrival.md), [military reachability/strategic route filtering](native-ai-routes.md), and [primary-order issuance](native-primary-orders.md) define those separate stages. No path check should be invented at this donor callsite merely because `0x496ac0` looks like a boolean predicate.

## Reproduction and boundaries

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_donor_ranking_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --out docs/research/evidence/native-donor-ranking-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-donor-ranking-probes.json
```

Without the explicit execution flag the tool only validates and disassembles the EXE/DAT. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4, x86_64 Rosetta. Orders.dat SHA-256: `0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9`. Probe script SHA-256: `7eeb8b6b928f15ee0c605a2d4e9deaa45d8dc820ced7707492cbc10b6e3cefc6`. JSON records current dependency hashes, allowlisted ranges, complete returns, call arguments/returns and checked state. Calls are bounded by100000 instructions.

The local binary is the oracle. Pinned [Teippi recruitment](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L835) is modified corroborating context, previously inspected in the initial-recruitment work. Its guard-list movement/allocation policies do not override these native observations. This batch does not establish non-null enemy-target route outcomes, garrison unloading, every health exemption, physical movement execution, general recipe recruitment, remaining region-state handlers or full campaign behavior.
