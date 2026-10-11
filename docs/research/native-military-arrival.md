# Native 1.17 military assignment and arrival

This report closes selected successful MilitaryAI allocation and arrival paths through `0x4ae760 → 0x4a9b20`, including completed-child assignment `0x48d320 → 0x487990`, original destination randomization, primary-order activation and the regional slowest-unit reaction. Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`.

Evidence: [static witnesses](evidence/native-military-arrival-static.json), [115 complete original-code cases](evidence/native-military-arrival-probes.json), and [inspector/probe](../../tools/research/inspect_native_military_arrival_117.py). **All115 cases pass without helper stubs.** Actual native pool/list/primary-order and spatial-query callees execute in private Unicorn memory, under reviewed instruction ranges. No game loop, WinMain, Windows API or guest host-file/network operation executes. This completes the two military-prefix dependencies in [training lifecycle](native-training-lifecycle.md) for the declared ordinary completed-unit paths.

## Allocation and completed-unit composition

`0x4ae760(ECX=requested_region, EDX=unit, stack=[force_requested_region])` first checks requested region state3. With force0 it replaces that region by the unit's **actual current-position AI region**, selected using the unit owner; force1 preserves the requested region. Two complete state3 cases distinguish both outcomes, including their resulting destination.

It pops a MilitaryAI record from `*(region+0x2c)+0x4e20`, repairs the remaining free head's previous pointer, and prepends the record to region military head+`0x30`. It writes type4 at record+8, parent at+`0x0c`, assigned region at+`0x10`, and unit.AI at unit+`0x134`. Ground-unit count u16+`0x0a` increments unless unit status bit4 is set. An empty MilitaryAI pool returns without assigning AI, movement or consuming random values; four complete cases distinguish forced/current-region and ground/flying paths.

The allocator reads the selected region center through actual `0x481a30`, arithmetic-shifting the terrain center's fixed-point coordinates by8. It calls arrival with order**157**, except Medic ID34 receives**177**. After arrival returns, states1,2,8,9 invoke actual slowest-unit selector `0x4ab870`.

Forty-four complete allocator-to-order cases cover states0..10, ordinary unit37/Medic34, and ground/flying status. Ten additional completed-child cases start at `0x48d320`, using every producer metadata slot0..4 and ground/flying child status. They execute the entire original tag1 chain through `0x487990`, allocation, arrival and real primary-order activation. Producer metadata tag1 remains1, the child receives the selected region's MilitaryAI, and order158 becomes active for the state8 fixture. This extends the previous prefix evidence without executing new-unit creation or visibility/placement functions.

## Arrival gates and order selection

`0x4a9b20(ECX=unit, EDX=input_order, stack=[x,y])` requires a nonnull AI of type4, matching AI parent, nonbuilding DAT property, and completed status bit1. It returns early for current orders106 or150. Without InBuilding status`0x20`, current orders98 or6 also return early. With InBuilding it attempts actual helper `0x437e00` instead; this batch executes that helper's complete **null-connected-unit return**, not its nonnull placement/show branch. That inconsistent connected-unit fixture is a literal boundary discriminator, not a claim about reachable garrison state.

For ordinary units, input157 changes to **158** when assigned-region state is1,8,9. The exact unit-ID switch at `0x4abc90` preserves the input order for IDs**9,45,46,67**; Medic remains177. All59 mapping bytes for IDs9..67 are retained as static data, separately from code disassembly. Six complete caster/control cases distinguish this exception from the ordinary state8 behavior. Numerical IDs are the contract; historical source order names differ.

Loaded status`0x40` without InBuilding takes `0x4ada90(unit,156)` instead of random-position movement. A completed loaded unit with no target issues ComputerAI156 through the real primary-order chain, consuming no random values. Six additional controlled burrowed/loaded cases execute `0x43c560`'s actual Unburrow118 request and `0x429d00` unlock/advance path, then append ComputerAI156. The primary issuer may already advance to156 and leave another156 queued. Burrowed status remains set: requesting an order does not execute the Unburrow state machine or prove that a loaded/burrowed combination is authentic gameplay state.

Siege Tank ID30 already on order99 returns early. Otherwise the Tank-specific branch searches within512 pixels through actual `0x4317a0 → 0x442b20 → 0x422330`. Four complete cases use an empty original spatial index, so no candidate callback is invoked. No enemy causes a request for TankMode99 followed by queued arrival157 at the randomized destination. With a real four-node order pool,99 remains active under the stock noninterruptibility flag and157 remains queued. With an exhausted pool, neither request activates and the initial order remains1, despite three random calls. Positive nearby-enemy targeting through `0x41f6c0/0x431c40/0x4202c0` remains a separate explicit dependency.

## Exact randomized destination

The routine resolves the requested point and current unit position to terrain region IDs, then calls actual `0x4ab330(player,current_region,requested_region)`. The current selected paths use identical regions, an actual neighbor match, or different-group rejection. [Military cohesion](native-military-ai.md) covers this helper's other selected predicates; arbitrary nonadjacent reachability is not replaced by a fixture response. If reachability rejects, arrival uses the requested point and consumes **no random values**.

If it accepts, use the **current terrain-region center**, rather than the unit's exact position, as source `S`, and requested point `T`. Execute these steps:

1. Compute integer direction `a=0x40ba10(S,T)`. Its slope helper `0x40b940` lower-bounds the unchanged table at`0x502eb0`; axis and quadrant handling are native integer operations.
2. Raw RNG call1, source index**`0x38`**: rotate `a` by64 when `result & 8` is nonzero (mask value 8, bit 3; `test al,8` at `0x4a9cf1`), otherwise by192, modulo256. Wording corrected after the function atlas ([P37 `0x4a9b20`](atlas/p37.md)).
3. Raw RNG call2, same source: radius=`result &127`. Use original signed direction vector at`0x5026b0+angle*8`, multiply by radius, arithmetic-shift each component by8, and add to`T` to obtain intermediate point`U`.
4. Compute integer direction from`S` to`U` and actual approximate distance `d=0x405cf0(S,U)`.
5. Raw RNG call3, same source: radius=`max(0,d-64+(result &127))`. Apply the vector table again and add to`S` for candidate`C`.
6. Accept `C` only when `0<=x<map_pixel_width`, `0<=y<map_pixel_height`, and its actual terrain-group word equals the requested point's terrain-group word. Otherwise use unchanged`T`.

The approximate-distance helper's floor points matter. For major absolute delta`M` and minor`m`, return`M` if `m <= M>>2`; otherwise set `q=(3*m)>>3` and return `M-(M>>4)-(M>>6)+q+(q>>5)`. Replacing this with rounded floating coefficients changes some destinations. Vector multiplication similarly preserves arithmetic right shifts.

Twelve complete enabled/disabled seed cases compare actual original execution against an independent integer transcription using the unchanged EXE tables. Enabled arrival advances the shared LCG three times and increments total/source56 counters by3. Disabled RNG returns zero three times and leaves state/counters unchanged, but still runs destination geometry. The table bytes are hash-anchored in static evidence; no trigonometric approximation substitutes for them.

Five complete destination cases distinguish x equal to width, y equal to height, negative candidate, equal terrain group, and different candidate group. An accepted example is seed42, source`[128,128]`, requested`[512,512]` → candidate`[627,427]`. Its seed becomes1961185873. A candidate in a different group falls back to`[512,512]` after the same three RNG calls. A separate different source/target-group case rejects reachability earlier and retains seed1 with zero calls. Preserve this distinction in deterministic simulation.

## Slowest military reaction

`0x4ab870(ECX=region)` returns immediately unless player campaign flag`0x20` is set. It initially considers the existing cached slowest pointer at region+`0x24`, then traverses the MilitaryAI list in native order. It compares **signed dword movement speed at unit+`0x34`**, separately for ground and flying units, keeping strictly smaller values. Equal speeds retain the earlier cached/list choice.

If any ground candidate exists, the final choice is the slowest ground candidate, even when a flyer is slower. Otherwise use the slowest flyer. The existing cached pointer participates even if an inconsistent fixture does not include it in the list; surrounding membership maintenance is a separate invariant. Twelve complete composed cases vary campaign flag, flying status, speed10/30/40, and an existing ground member speed30. They also verify both native active-list link repair and remaining free-list linkage after allocation.

Pinned [AISE AiRegion layout](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781) corroborates the military/slowest/count offsets. Pinned [Teippi AddMilitaryAi](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L575) corroborates the arrival/reaction structure, but allocates C++ records and uses its modified enum/implementation. Local 1.17 instructions establish the fixed pools, order IDs, integer geometry and RNG consumption.

## Reproduction and limits

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_military_arrival_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --out docs/research/evidence/native-military-arrival-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-military-arrival-probes.json
```

Default mode is static. Execution requires effective stock orders.dat SHA-256`0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9`, and loads its189 interruptibility/highlight/subunit-inheritance entries unchanged. The guest's main image animation is0; actual idle-reset instructions execute. Subunit pointer is null. Two complete exhaustion cases establish that membership/RNG success does not prove primary-order activation.

JSON records target/data/probe/dependency hashes, Python3.9.6, Unicorn2.1.4, Capstone5.0.6, pefile2024.8.26, x86_64 Darwin architecture, exact code allowlist, instruction counts, actual arguments/call traces and complete-return status. Fixtures are synthetic object/DAT/map state with real native callees. Remaining dependencies are positive Tank combat selection, nonnull InBuilding release/placement, general connectivity cache/BFS, and subsequent military order execution and strategic attack-wave planning. Those are not hidden by these complete selected assignment returns.
