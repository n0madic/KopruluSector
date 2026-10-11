# Native 1.17 guard, military cohesion and stim decisions

This report continues [native combat AI](native-combat-ai.md) and [unit AI](native-unit-ai.md) with concrete local military decisions. The target is StarCraft **1.17.0.1**, PE32 x86, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. All addresses are preferred VAs in that exact image; their RVA is VA minus `0x400000`. Older source addresses are not reused as local addresses.

The [static evidence](evidence/native-military-ai-static.json) contains 18 reviewed blocks and 660 decoded instructions, without undecoded tails. The [execution evidence](evidence/native-military-ai-probes.json) contains **76 passing cases**, comprising **65 complete original-function returns** and **11 prefixes stopped before order issuance, technology checking or stim application**. Artificial inputs are explicit units, AI nodes, region lists and terrain tables in private memory. All helpers executed before a boundary are original instructions, including pixel-region lookup, region connectivity, combat range, DAT technology offset initialization and the shipped technology requirements interpreter. There are no replacement helper stubs and no complete game or campaign execution.

## Evidence and reproduction

The static extractor is [inspect_native_military_ai_117.py](../../tools/research/inspect_native_military_ai_117.py), SHA-256 `eb18e4065af7936c836cf6cd3dd91c713df1d63d7d82227404d14f2119face0f`. The runner is [probe_native_military_ai_117.py](../../tools/research/probe_native_military_ai_117.py), SHA-256 `a9bb819b739971fd06c4388b1dda56140e034e521efa03073017ac38eb479f04`. Its evidence also records both dependency hashes: the reviewed unit-AI allowlist and the [DAT requirements inspector](../../tools/research/inspect_native_dat_requirements_117.py). No dependency function body replaces retail instructions.

```sh
python3 tools/research/inspect_native_military_ai_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-military-ai-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_military_ai_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-military-ai-probes.json \
  --execute-original-code
```

The engine is Unicorn 2.1.4, x86-32 guest, x86_64 host process under Rosetta. Each call has a 10,000-instruction bound and a reviewed code allowlist. Recorded outcomes include inputs, expected and actual outputs, visited function ranges, instruction counts, return/boundary address, ECX/EDX at the boundary and three stack arguments. A Windows game process, WinMain, DLL, Windows API, host-file operation or network call does not execute in the guest.

Pinned primary comparison sources are [Teippi's guard return-home code](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit_ai.cpp#L101), its [AI region layout](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.h#L133), and AISE's [pathing region](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L631) and [military AI region](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781) layouts. Layout names corroborate native offsets; their source implementation is not a substitute for local execution evidence.

## Shared military eligibility and terrain fields

The group functions below begin with the same eligibility checks: unit `+0x134` points to AI, AI type byte `+8` is `4`, AI parent pointer `+0x0c` equals the unit, and DAT building-property bit `1` is clear. The AI node's region pointer is `+0x10`. These checks use DAT properties, not the superficially similar unit status bits.

The AI region has a slowest-military pointer at `+0x24` and the first military-list node at `+0x30`; node `+0` is next and `+0x0c` is the associated unit. Group functions inspect the linked list in its existing order.

Terrain data is reached through global pointer `0x69b584`. Local accessors and source-layout correspondence establish:

| Field | Native address relative to terrain base |
|---|---|
| Tile-to-region table | `0x0c + (tile_y * 256 + tile_x) * 2` |
| Region record start | `0x449fc + region_id * 0x40` |
| Region group | Record `+2` |
| Region tile count | Record `+4` |
| All-neighbor count | Record `+7` |
| Neighbor IDs pointer | Record `+0x0c` |
| Fixed-point center x/y | Record `+0x10/+0x14` |
| Ignored-neighbor count | Record `+0x21`, read as signed byte |

Native pixel lookup is `0x481b10`; unit lookup `0x481ba0` obtains sprite coordinates then tail-calls it. Split-tile resolution remains original code, although this batch uses ordinary tile IDs. Center accessor `0x481a30` arithmetic-shifts stored fixed-point coordinates by eight. Group accessor is `0x481c90`; neighbor accessor `0x481ad0` returns the neighbor pointer and `all_neighbors - signed(ignored_neighbors)` count.

`0x4ab330(player, source_region, target_region)` returns true immediately for player `>=8` or identical region IDs, rejects different terrain group IDs, then accepts a target appearing in the source's reviewed neighbor prefix. Further paths inspect a regional air target and a cached search before consulting reachability bytes; those paths are visible statically but not exercised here. One complete case executes distinct same-group adjacent regions and the real neighbor traversal. Cohesion cases execute identical-region acceptance and different-group rejection. This proves their invoked connectivity branches, not arbitrary terrain BFS behavior.

## Guard return-home decision

`0x4b2110`, ECX = unit, EDX = `dont_issue`, returns whether a guard should return home. It returns false unless every following gate passes:

1. AI exists and has type `1`.
2. If unit race helper `0x428d60` returns Terran (`1`) and DAT mechanical-property bit `0x40000000` is set, current **raw** HP exactly equals DAT raw maximum HP.
3. Unit pointer field `+0x7c` is null. Teippi calls this a previous-attacker field; the GPTP layout names it auto-target. The native requirement is pointer-null, independent of that naming difference.
4. The guard-home position from AI `+0x12/+0x14` lies in a different region from the current sprite position.
5. The home position is outside the 192-pixel point-area test `0x417aa0`.
6. There is no current target in native weapon range.
7. The first queued order is absent or is not Patrol `152`, as read by `0x429d10`.

If eligible and `dont_issue != 0`, return true without issuing an order. If eligible and zero, issue **ComputerReturn `163`** at the home position through `0x42a340`, then return true.

The point-area helper compares against approximate distance using unit exact fixed-point coordinates `+0x2c/+0x30`, then shifts the unsigned distance by eight before testing the inclusive radius. Sprite positions serve region lookup; they do not replace those exact coordinates. At exact integer positions, distance `192` blocks return, distance `193` permits it. A home at `193` and exact current x of `1/256` yields a truncated distance of `192` and blocks return. A one-fixed-point HP loss blocks return for Terran mechanical guards even though rounded HP still equals maximum.

Eighteen complete cases cover absent/wrong AI, same/different region, the pointer `+0x7c` gate, both distance boundaries and fractional position, Terran mechanical full/whole/fractional damage, Zerg/Protoss mechanical damage, Patrol/Move queue heads, and a current target inside/outside range. One prefix records the real order-issuance call with `[order=163, x=320, y=0]`; queue allocation and subsequent movement are not executed in that prefix.

## Regional slowest unit and transport pickup

`0x4afaf0` applies common military eligibility, then returns region `+0x24` without another selection scan. It is a **read of an existing slowest-unit choice**, not the routine that computes that choice. Five complete cases establish no AI, wrong type, wrong parent, building property and valid military outcomes.

`0x4afec0` returns whether this military group has another unit waiting for transport pickup. After common eligibility, region state `2` rejects. Traverse the region military list; return true for another unit whose order byte is **Pickup4 `96`** and whose loaded status bit `0x40` is clear. Skip self and all other orders or loaded candidates. Return false after exhaustion.

Eight complete cases distinguish order `6` versus `96`, loaded versus unloaded, and another unit versus the unit's own list node. In HarassMove `0x41e890`, a true result leads to movement stop through `0x43a4e0`. Generic group distance must not replace this transport-specific wait condition.

## Follower readiness

`0x4b09c0` returns **true** for ineligible units and for cases where no regrouping restriction applies. For eligible military units, the following gates must all pass before it can return false:

- Region slowest unit exists, is another unit, and has loaded status bit `0x40` clear.
- Assigned AI region state is not `2`.
- The slowest unit's current terrain region has tile count **at least 60**.
- The follower and slowest unit pass original regional connectivity `0x4ab420`, which calls `0x4ab330`.
- Approximate center distance from follower to slowest unit is **greater than 128** pixels.

When all gates pass, return whether the follower is within **384** pixels of its assigned AI region's terrain center. Thus a distant follower outside that center radius returns false; an equally distant follower inside the radius returns true. These are approximate center distances, distinct from the bounding-box weapon distances in [native-unit-ai.md](native-unit-ai.md).

Eleven complete cases distinguish 128/129 distance, exact 384 center-distance equality and one pixel outside, tile count 59/60, region state `2`, absent/self/loaded slowest unit, and different terrain groups. The caller uses a false result to allow movement toward the slowest unit; true suppresses that particular following action.

## When the leader waits for a member

`0x4aff20` applies common eligibility and rejects assigned region state `2`. It scans military-list members other than self. For each member it computes approximate center distance and skips distances **greater than 384**. A member within that radius causes the leader to wait only when all three conditions hold:

```text
regions_reachable(leader, member) == true
follower_ready(member) == false
distance(leader, member) > 256
```

Therefore the distance band is **(256,384]**, with an additional connectivity and follower-readiness requirement. A member at 256 does not trigger waiting, 257 can, 384 can, and 385 is skipped. The first qualifying member returns true; no qualifying member returns false. Eight complete cases cover those four distances with region centers at zero and 1,000, executing the real follower predicate and its region/center helpers.

In HarassMove, the leader is identified when `0x4afaf0(unit) == unit`; a true wait result stops movement. This does not mean every distant member causes a wait, or that all members must be collected before movement resumes.

## Stopped units rejoin their assigned region

`0x4b0e10` requires common military eligibility and an exact match between movement-target words `+0x10/+0x12` and current sprite position. Its status-`0x80000` expression produces either one or two, so the following zero branch cannot reject an exact position match. If the current terrain region equals the assigned AI region ID, return without issuing an order.

Otherwise read the assigned terrain-region center and issue **HarassMove `158`** toward it. (Order 158 was labeled ComputerAttackMove in this document; the pinned Teippi name is HarassMove. Label corrected after the function atlas, [P07 `0x41e890`](atlas/p07.md).) Medic ID `34` issues **HealMove `177`** instead. Four cases cover an ordinary unit and Medic, each already in the assigned region or in another region. Two complete returns and two original prefixes verify the actual order IDs and center coordinates `[320,0]`. They do not execute order queue mutations after the issuance entry.

## AI stim decision

AttackUnit's AI support `0x4b1100` implements the stim decision. It only considers ordinary **Marine `0`** and **Firebat `32`**. Hero classes accepted by the general technology requirements table do not automatically enter this AI action.

The decision requires:

1. A current target.
2. Native speed modifier `0x428af0` is `<=0`.
3. Rounded own HP is at least half maximum whole HP, with the maximum-HP fallback specified in [native-unit-ai.md](native-unit-ai.md).
4. The target can attack this unit using the original `0x41da50` capability test with detection enabled.
5. This unit is currently in weapon range of its target via `0x421060`.
6. Original `CanUseTech(Stim=0, unit, player)` at `0x407f90` returns exactly `1`.
7. Target rounded HP plus enabled whole shields from `0x41faa0` is **at least 100**.

If all gates pass, tail-transfer to stim application `0x438af0`. That application is an explicit boundary in this batch; no sound, HP-cost or timer-effect helper is replaced or claimed as executed here.

The speed modifier is `nonzero(stim_timer +0x115) + status_bit(0x10000000) - nonzero(ensnare_timer +0x116)`. Only a **positive** result rejects the decision. Eight complete/prefix cases cover all combinations; an active stim timer combined with Ensnare can therefore reach the technology check because the modifier is zero. This observed predicate should be retained rather than simplified to “already stimulated means reject.”

Four complete early-rejection cases cover a non-Marine/Firebat, own HP below half, target out of range, and a target disabled from attacking. Eight further cases execute the real technology availability/research accessors, shipped offset initializer, technology-use wrapper and requirements interpreter. They cover Marine/Firebat acceptance, fractional own HP rounding to half, target life 99/100 and fractional rounding to 100, unavailable/unresearched Stim, and a hero excluded by the AI class gate. Four stop at the actual stim-application entry after every decision gate has passed; the other four return normally.

Technology-use execution uses the original shipped Stim recipe and explicit availability/research bytes; it does not replace the recipe with an unconditional true predicate. Its broader specification is in [native-dat-requirements.md](native-dat-requirements.md).

## Remaining native work

These predicates define guard recall, local group waits, follower cohesion, region rejoining and stim eligibility. Their execution does not prove full regional battle planning or movement through arbitrary terrain. The remaining concrete dependencies are slowest-unit **selection**, nonadjacent cached/BFS reachability `0x4abe90`, military-region transfers and group-list lifecycle, order issuance/queue activation, movement stop/approach, and stim application effects. The complete campaign regression should vary guard damage, transport pickup, large/small regions, minimum-range weapons, disconnected terrain and mixed Marine/Firebat/Medic groups. Keep frame timing and shared random-call ordering from [native-frame.md](native-frame.md) and [native-prng.md](native-prng.md) when implementing these decisions in Defold.
