# Native 1.17 radius recruitment and regional sweep

This batch closes positive nearby-guard recruitment through the actual spatial callback and composes the regional cursor scheduler with its real threat rebuild and selected state dispatcher. **28 complete original-code cases pass without helper stubs.** Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`.

Evidence: [two additional static witnesses,69 instructions](evidence/native-radius-recruitment-static.json), [28 complete original-code cases](evidence/native-radius-recruitment-probes.json), and [inspector/probe](../../tools/research/inspect_native_radius_recruitment_117.py). Imported reviewed original callees supply the real radius/rectangle query, generic AI detachment, guard counter update, MilitaryAI allocation, random movement destination, primary-order activation, enemy-strength rebuild and state8/9 dispatcher. Fixtures provide private pools, a sorted spatial index, units, region geometry and DAT values. No complete game, WinMain, Windows API or host-file/network call executes.

## Nearby guard callback and full radius consumer

`0x4ada40(ECX=unit, EDX=destination_region)` requires nonnull UnitAI, AI type byte1, no InBuilding status`0x20`, matching owner byte, and an actual sprite-position region equal to the destination's ID. It then calls **`0x48dbc0(unit,was_killed=0)`**, followed by **`0x4ae760(destination,unit,force=1)`**. It returns0, continuing callback iteration. This callback does not select by requested unit type, health, completion or the guard's stored home; it recruits qualifying guards already in the destination's actual region.

Eight direct positive cases vary MilitaryAI pool availability and guard counter0/99/100/255. The actual generic remover clears matching regional target/slowest/detector references and the old guard parent, increments its counter below100 and leaves values100/255 unchanged. It clears UnitAI before allocation. Successful allocation creates MilitaryAI in the destination, reinstalls the unit as the destination's slowest pointer through real arrival handling, issues ordinary order158 and consumes three native RNG values. With an empty pool, AI stays0, the guard remains detached, no order or RNG call occurs, and callback iteration continues. There is no rollback or failure return.

This differs from roster adoption in [initial recruitment](native-campaign-recruitment.md): that path does not call generic guard removal and does not increment the guard counter. The same visible guard→MilitaryAI conversion therefore has distinct cleanup effects according to its caller.

Seven direct gate cases distinguish null AI, another AI class, InBuilding status, another owner, another current terrain region, completion and current order0. The first five preserve the guard and supplied region references. Two deliberately supplied boundary fixtures show that callback filtering itself admits incomplete or order0 guards; they are not claimed as naturally reachable campaign states. An incomplete fixture gets MilitaryAI but skips arrival order/RNG work. With order0, actual arrival consumes three RNG values while original primary-order issuance leaves order0 active. This is consistent with [primary-order permission/activation](native-primary-orders.md): a normal issuer return does not prove the requested order became active.

Two further complete cases begin at **`0x4b15d0(source,destination,[extra_radius=1,include_casters=1,restrict_in_building=1])`** with an empty source military list. It reads the destination center, executes actual radius256 query`0x442b20`, finds the indexed guard and runs the full callback→generic detach→allocation→arrival chain. The cases distinguish successful allocation and empty-pool detachment. Spatial scratch depth and offset restore to0. These are positive original spatial-consumer calls, separately from the earlier [campaign-wave report](native-campaign-wave.md)'s empty-radius fixture.

Three more complete calls provide **two indexed guards** at128/128 and144/144, with0/1/2 MilitaryAI nodes free. Actual query order processes the first guard before the second. With one node, the first gains MilitaryAI/order158 and three RNG calls; the second still loses its guard assignment, increments its counter and remains AI0/order1. Both nodes available produce two assignments/orders and six RNG calls, with the second military record inserted at the head. Zero nodes detach both. Later exhaustion therefore preserves earlier successful recruitment and does not terminate the callback pass; the query scratch state still restores to0.

## Exact cursor sweep and threat timing

`0x4b0d70` reads actual terrain count once and performs exactly **333 slot visits**. The probes count visits at original instruction`0x4b0d84`, including skipped slots. Persistent globals are player`0x6b1a94` and region`0x6b1a98`. A slot calls the state dispatcher only when its region is below actual count and its player-record type is1. It then increments the region cursor. At **2500**, it resets the cursor to1, increments player and wraps8 to0. A boundary call invokes `0x4abf90` for every newly selected player; that function performs its rebuild only when the new player cursor is0.

Five complete sweeps begin at player/region0/1,0/2300,3/2498 and7/2498, with player types0 explicitly skipping regional actions and the noncampaign planner. They prove333 visits, exact final cursors, all boundary producer calls and the difference between an early-return producer call and an actual rebuild. Seeded enemy-strength fields remain123 when the new player is1 or4; wrapping7→0 actually clears all eight seeded arrays, even though all player types are0. The rebuild's contributions and filters are separately recovered in [enemy strength](native-enemy-strength.md).

Three composed sweeps supply one admitted region at index1, actual terrain count2, player7/type1 and states8,9,10. The real dispatcher clears flag4 and reduces retry byte2→1. States8/9 execute actual strength/target/readiness helpers and retain a missing roster entry; state10 returns after the common dispatcher mutations. The remaining332 visits skip absent region slots, leaving final cursor334. These fixtures do not replace state helpers or count a direct dispatcher call as scheduler execution.

The outer accepted-object-step caller is independently recovered in [frame scheduling](native-frame.md). Under steady cursors, a sweep cycle has **8×2499 =19992** visits after the initial index0 phase, so preserving the skipped slots matters for revisit timing. This batch executes selected full scheduler calls, not a continuous game loop or every state's action.

## Reproduction and remaining regional work

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_radius_recruitment_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --out docs/research/evidence/native-radius-recruitment-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-radius-recruitment-probes.json
```

Default mode only reads/disassembles the exact EXE and validates stock orders.dat. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4, x86_64 Rosetta. Orders.dat SHA-256: `0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9`. Script SHA-256: `3e8207e67a6c572dda8f817a2f91dbd889fdf8d83cca114b91b16954adab41b5`. JSON records runtime/script/dependency hashes, reviewed ranges, actual call arguments,333-slot observations, complete returns and matched memory effects. Each entry is bounded by100,000 original instructions; the longest uses4089.

Pinned [AISE AiRegion layout](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781) corroborates the fields. Original local instructions determine policy; modified guard/army logic is not substituted. Remaining compositions include the boundary noncampaign melee planner`0x4aa8f0`, other state0..7 actions, general donor route/ranking, and physical movement/production/combat after these decisions. Selected complete returns and fixtures do not establish a campaign walkthrough or full-game parity.
