# Native 1.17 positive attack-order and wave-retarget consumers

This batch composes positive target selection with its original consumers: regional target update `0x4adcf0 → 0x4ada90 → 0x42a2c0` and state8 dispatch `0x4b0ce0 → 0x4ac870 → 0x4aba70 → 0x4b15d0 → 0x4ae760 → 0x4a9b20`. Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`.

Evidence: [additional transport witness and dependencies](evidence/native-attack-consumers-static.json), [20 complete original-code cases](evidence/native-attack-consumers-probes.json), and [inspector/probe](../../tools/research/inspect_native_attack_consumers_117.py). **All20 cases pass without helper stubs.** Original spatial queries, positive scores, attack/detection predicates, order pool/list/activation, terrain groups, AI release/allocation, arrival and RNG execute. No WinMain, full game, Windows/host API or guest file/network operation executes. Objects, cached strengths, terrain, DAT fields and private pools remain declared fixtures.

These are composed native function paths, extending [positive strategic targets](native-strategic-targets.md), [wave launch](native-campaign-wave.md), [military arrival](native-military-arrival.md) and [primary orders](native-primary-orders.md). They do not execute movement, future ComputerAI order handling, attacks or damage.

## A regional target requests ComputerAI, separately from the target pointer

Ten cases begin at actual `0x4adcf0` with one indexed enemy and one MilitaryAI member. Both regional target pointers+`0x1c/+0x20` become the enemy through the actual spatial-query/positive-score chain. The member can then receive **order156, ComputerAI**, through the real primary-order issuer. This operation does not directly issue AttackUnit14 or write the enemy into Unit+`0x5c`.

The actual member gates suppress this request when its current order is14,157,158; status has any bit`0x3000`; the selected enemy already equals its current target; or the real attack-capability checks fail. Ground weapon sentinel130 prevents attacking the fixture's ground enemy even though both regional target fields can still select that enemy. A nonmatching **nonnull** current target can allow the selection/gates to reach `0x4ada90`, but that helper preserves the current order because its ordinary branch requests156 only when Unit+`0x5c` is null.

Order-pool failure is also concrete: with no free order node, the positive selector and original issuer return completely, while the unit retains order1. This composition must not be reduced to a boolean “target found means attack started.” Query depth and scratch offsets restore to0 in every case; the military AI pointer remains intact.

Four more cases discriminate non-MilitaryAI, UnitAI parent mismatch, Building DAT property, and nonzero transport-capacity byte. They preserve order1 while the regional target fields update positively. The transport fixture intentionally changes only the capacity byte; it does not claim that a type37 unit is an ordinary transport. Original predicate `0x4381f0` rejects hallucinated transports, requires Ventral Sacs for Overlord42, and otherwise returns whether its DAT cargo-capacity byte is nonzero. The selected positive capacity case executes this original predicate; its unexercised Overlord branch is statically recorded and uses the independently recovered upgrade accessor.

Two detection cases compose the entire path: invisible status`0x300` without player7 detection leaves both region targets0 and order1; detection mask128 admits the enemy through the regional selector and the actual `0x41da50 → 0x40a180` attack check, then activates order156. Detection is checked at both relevant layers rather than substituted by a fixture helper result.

## A positive global fallback creates state9 and transfers the army

Four cases start at the original region dispatcher with source state8, a ground MilitaryAI member, source terrain marker`0x1ffd`, and a Building-status enemy in terrain region2. The source-region enemy queries are empty because that enemy lies outside their spatial rectangle, while the actual global active-unit list contains it. This explicitly distinguishes the regional and global selectors.

For matching connectivity groups, the original special-marker branch resets the source to state0, the global selector chooses the enemy through actual completed transport/upgrade/reachability helpers, and its actual position yields destination region2. The destination changes to **state9**, with priority50 in campaign mode or160 otherwise. Actual `0x4b15d0` releases the source military record, actual `0x4ae760` reallocates it to the destination, and arrival activates order158 through the original primary-order implementation. The three actual RNG calls advance the destination state/counters according to [military arrival](native-military-arrival.md).

For differing connectivity groups, the global selector returns no target. Actual `0x4b1500` instead disperses the member into its current region1. It receives ordinary order157; destination region2 remains state0. Both outcomes retain grouping-plus-one2 and last-attack100. This is distinct from the ordinary roster/timeout launch, which creates destination **state1** and can clear the timeout field.

The terrain marker, groups, cached strengths, object positions and enemy list are explicit inputs; this batch does not claim that its complete synthetic arrangement arose during a campaign. It establishes the original conditional paths and resulting records/orders. Authentic terrain input/region generation and caller scheduling remain separate evidence domains.

## Reproduction and next dependencies

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_attack_consumers_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --out docs/research/evidence/native-attack-consumers-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-attack-consumers-probes.json
```

Default mode only reads/disassembles the hash-gated PE. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6 and Unicorn2.1.4 under x86_64 Rosetta. Stock orders.dat SHA-256 is `0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9`; its interruptibility/highlight/subunit-inheritance fields remain unchanged. JSON records script/dependency hashes, original allowlisted instruction ranges, complete returns, actual helper arguments, expected effects and observations.

Remaining work includes initial group recruitment from periodic strategic scheduling, positive radius recruitment of guards, upstream dynamic enemy-strength production, actual transport orders, remaining regional states and the later movement/ComputerAI/combat consumers. The selected target→order and global target→state9 transfer dependencies are now directly composed, while those later effects remain explicit.
