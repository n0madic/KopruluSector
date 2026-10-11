# Native 1.17 default production and needed guards

The original default producer chooser `0x4849c0`, capability/limit helper `0x4878d0`, region accessor `0x4b0960`, request insertion `0x482990`, morph-parent emission `0x482130`, and needed-guard helpers are recovered from StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. All addresses are preferred VAs at ImageBase `0x400000`. Evidence: [static code and complete producer switch](evidence/native-default-production-static.json), [116 isolated original-code checks](evidence/native-default-production-probes.json), [inspector/probe](../../tools/research/inspect_native_default_production_117.py).

All **116 checks passed without helper stubs**, executing complete reviewed functions and their actual callees for the selected fixture paths. The probe executes original instructions in private Unicorn memory under an instruction allowlist. It executes no WinMain, full game, Windows API, or host file/network operation. This closes default selection and the declared guard predicates; it does not prove a complete production lifecycle or campaign parity.

## Producer gate and fixed alternatives

`0x4849c0(ECX=player, EDX=producer, stack=[priority])` first rejects producer-incomplete status or primary orders `0x6a`/`0x96` through `0x4b14d0`. It requires secondary order `0x17`. These gates precede the explicit-request path, so an explicit request does not bypass them.

Explicit accessor `0x482be0` consumes nonzero word PlayerAi+`0x220`, returning value minus one and clearing the word. Without an explicit value, signed current stocks must be at least **600 minerals and 300 gas**, and PlayerAi flag `0x4` must be clear. The [spending dispatcher](native-spending-dispatch.md) separately tests its opportunistic thresholds of stocks greater than1500 and calls the chooser with priority160, or priority50 for an empty request heap.

The switch accepts current producer IDs35..167 and uses a 133-byte index table at `0x484c3c` into eight targets at `0x484c1c`. Exactly seven producer IDs have choices. They are ordered alternatives, with different checks at individual alternatives:

| Producer | First choice | Second choice | Checks at choice |
|---|---|---|---|
| 35 Larva | 38 Hydralisk | 43 Mutalisk | Requirements exactly1, then unit limit, for both. |
| 111 Barracks | 0 Marine | 1 Ghost | Marine: unit limit only. Ghost: requirements exactly1, then limit. |
| 113 Factory | 3 Goliath | 5 Siege Tank, tank mode | Requirements exactly1, then limit, for both. |
| 114 Starport | 8 Wraith | 12 Battlecruiser | Wraith: limit only. Battlecruiser: requirements exactly1, then limit. |
| 155 Robotics Facility | 83 Reaver | None | Requirements exactly1, then limit. |
| 160 Gateway | 66 Dragoon | 65 Zealot | Dragoon: requirements exactly1, then limit. Zealot: limit only. |
| 167 Stargate | 70 Scout | 72 Carrier | Scout: limit only. Carrier: requirements exactly1, then limit. |

All other types return0 without adding a request. Twenty complete chooser cases select every alternative and reject each exhausted list. Seven additional cases cover unsupported types inside and outside the table range. Native Marine cases with its availability byte both0 and1 still select Marine: its first-choice branch does not call the specialized requirement gate. Admission later evaluates the actual recipe, so selection itself does not guarantee training success.

`0x4878d0(ECX=producer, EDX=requested_ID)` obtains the producer owner and calls [specialized unit requirements](native-requirements.md) `0x4b32a0`. A result other than exactly1 returns1 immediately. Otherwise it tail-calls native unit-limit helper `0x4835e0`. This wrapper returns a **rejection predicate**, not the unit gate's three-valued result. A valid second-choice Tank fixture needs an attached Machine Shop120, and a Battlecruiser fixture needs Control Tower115; the probe supplies those real recipe prerequisites rather than stubbing the gate.

## Region context and carried stack arguments

`0x4b0960(ECX=producer)` is a region accessor. It reads owner byte+`0x4c`, obtains the producer's current position region through `0x481ba0`, and returns:

```text
player_region_array[owner] + u16(region_ID) * 0x34
```

The array-pointer table is `0x6b1a74`. `0x481ba0` obtains signed sprite coordinates at+`0x14`/+`0x16` and tail-calls position-region helper `0x481b10`. This batch observes an ordinary grid-cell region lookup; the underlying split-cell and pathing contracts belong to [placement](native-placement.md).

The chooser pushes four request arguments **before** calling this accessor. The accessor has a plain `ret`, consuming none of them. The subsequent `0x482990` receives `ECX=player`, `EDX=returned_region`, and stack `[requested_ID, priority, arg3=1, explicit_flag]`. It consumes16 stack bytes. Treating `0x4b0960` as the request generator would lose this ABI and its actual role.

## Request insertion, costs and morph parents

`0x482990` suppresses insertion when its fourth stack argument is0 and PlayerAi flag`0x200` is set. A nonzero fourth argument bypasses that suppression. If the third argument is0 it clears PlayerAi flag1; otherwise it retains it. After suppression checks, it sets flag`0x40`, adds requested-unit mineral/gas/supply cost to AI demand, and calls the real [spending heap](native-economy.md) push `0x482040` with kind1, requested ID, byte priority and region context.

Eight direct insertion cases distinguish both arguments and flag`0x200`, including input priority300 becoming stored byte44. The third argument is subsequently reused as a local cost-output slot; its initial flag-control use must be recovered before that overwrite.

Insertion then invokes `0x482130` **even if the main heap push was rejected**. This adds costs and kind1 parent requests for these requested IDs:

| Requested unit | Additional requests |
|---|---|
| 44 Guardian or62 Devourer | One Mutalisk43. |
| 68 Archon | Two High Templars67. |
| 63 Dark Archon | Two Dark Templars61. |
| 103 Lurker | One Hydralisk38. |
| Other reviewed valid IDs | None. |

Each parent uses the same region context and priority `(LOW8(priority)-1) mod256`. Priority0 therefore creates parent priority255. Costs accumulate **before** each push; unit limits may reject the main request, the parents, or both while leaving their newly added demand. Three native Guardian cases independently block44,43, or both, observing that demand remains for both attempted requests. This is actual execution order, not an inferred accounting correction.

The cost fixtures deliberately use10 minerals/5 gas/2 supply for every unit, making each added amount observable. They test control flow and width, not authentic unit prices. Original cost-table behavior is separately recovered in [production](native-production.md) and [training](native-training.md).

After insertion, the chooser returns whether native top-kind getter `0x483550` is nonzero. It does not return the success of the particular push. A blocked Guardian with an accepted Mutalisk parent returns1; both blocked return0 despite increased demand. Nonempty prior state must likewise be considered before interpreting this return as request acceptance.

## Explicit request redirection through needed guards

An explicit ID is first checked by `0x4b25f0(ECX=player, EDX=desired_ID)`. It scans the needed-guard list head at `0x6be614 + player*8`, following node+0. A candidate must have parent pointer+`0x0c` null and stored u16 type+`0x10` equal to the **full** requested EDX value. This query does not normalize Siege Tank30 to5.

The candidate's signed coordinates+`0x16`/+`0x18` select a region through `0x481b10`. Native eligibility `0x4aea70` requires all of:

- Region state byte+5 differs from3.
- Region dwords+`0x1c` and+`0x20` are both0.
- Region flags byte+8 has bit`0x20` clear.

The query returns1 on the first eligible candidate and0 after exhaustion. Forty-four cases vary states0..10, assignment, the first target field and flags. Four further cases distinguish the second target field, full requested-ID width and exact Tank comparisons.

If this query finds a matching guard, the chooser calls `0x4b2750`, replaces its explicit ID with-1, and enters the automatic producer switch. It bypasses the stock600/300 and flag4 gates on this redirected path, but uses explicit_flag0, so flag`0x200` can still suppress insertion. If no suitable guard exists, it retains the explicit ID and inserts with explicit_flag1. Explicit ID228 is consumed but returns0 without insertion.

Four complete native cases use stocks0/0 to distinguish eligible/blocked/assigned guard candidates and flag`0x200`. An eligible Ghost1 needed guard causes Barracks to request default Marine0 even with default-disable flag4 set. The assigned or state3 candidate retains the explicit Ghost1 choice. This mechanism redirects production; it does not itself assign the new request to the guard.

## Guard retry marker and source differences

`0x4b2750(ECX=player, DX=desired_ID)` scans all needed guards. For each parentless candidate with nonzero dword+`0x1c`, it normalizes stored type30 to5, compares against **LOW16(requested_ID)**, and writes1 to that dword on a match. It does not normalize the requested ID itself. Requested5 marks both stored5 and30; requested30 marks neither; zero timestamps and assigned guards remain unchanged. Seven native cases observe these list writes and requested-ID truncation. This is a marker update, not a countdown decrement in this helper.

Pinned [Teippi guard query/refresh](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L874) corroborates the retry-field label `previous_update` and stored Tank normalization. Its reconstructed query omits the original parent-null test and region flag`0x20` check; its refresh also omits the parent-null test. These differences preclude substituting that modified source for this local binary. [AISE region rebuild predicate](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/src/ai.rs#L800) includes state, target and flag checks, but its broader guard scheduler is modified and has separate policy. [Teippi PlayerData layout](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.h#L66) corroborates field positions, without establishing local addresses across versions.

## Complete selected consumer path and reproduction

One complete original-callee case starts with an empty request heap, Barracks111, stocks600/300, valid original Marine recipe and supplied race/supply/account fixtures. Building entry`0x487110` generates priority50 Marine0, admits it through the original queue consumer, pays10/5 current stocks, consumes10/5/2 AI available budget, removes its demand and pops the request. The final queue is `[0,228,228,228,228]`, stocks590/295, demand0/0/0 and heap empty. This proves the selected chooser-to-queue path; the fixture prices are synthetic.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_default_production_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-default-production-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-default-production-probes.json
```

Without the explicit execution flag, inspection only reads/disassembles the exact hash-gated EXE. Probe JSON records target/script/dependency hashes, runtime versions/architecture, reviewed ranges, instruction counts and actual call trace. The subsequent [guard lifecycle report](native-guard-lifecycle.md) covers pool/list creation, selected cleanup/rebuild paths, retry timing and the explicit word+`0x220` writer; [guard transfer](native-guard-transfer.md) closes selected successful adoption. [Train lifecycle](native-training-lifecycle.md) recovers retry/progress/rally and selected completed-unit assignments, while successful creation, normal hidden completion and full morph progression remain dependencies. The separately recovered [primary-order queue and activation](native-primary-orders.md) contract explains why an issuer's normal return alone cannot prove the requested order became active.
