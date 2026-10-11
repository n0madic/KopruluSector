# Native 1.17 regional states zero through seven

**183 complete original-code cases pass**, recovering selected actions of all eight regional states through the real dispatcher and full reached callees. This adds reserve mobilization, neighbor-support selection, combat departure and support arrival to the earlier state8/9 campaign-wave work. Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`.

Evidence: [eight new static witnesses, 434 instructions](evidence/native-region-states-static.json), [183 complete cases](evidence/native-region-states-probes.json), and [inspector/probe](../../tools/research/inspect_native_region_states_117.py). The longest call executes3780 instructions. Actual target/strength queries, spatial selection/release, state changes, route calls, military release/allocation, RNG and primary-order activation execute without callable helper replacements. Fixtures supply explicit units, cached strengths, DAT entries, terrain records/neighbor arrays and free pools; unchanged stock orders.dat supplies189 order fields. No complete game, WinMain, Windows API or guest host file/network operation executes.

## Dispatch and shared strength decay

The native dispatcher `0x4b0ce0(ECX=player,EDX=region_index)` clears region flag4, decrements nonzero retry byte+7, then dispatches by state. A handler therefore sees **the decremented retry**, independently of its other counters. The unchanged mapping is:

| State | Handler | Selected behavior recovered here |
|---:|---|---|
| 0 | `0x4ac9b0` | Strength update, then requirement decay |
| 1,2 | `0x4ac7c0` | Empty reserve reset, state1 promotion at20 ground members, retain local target or select/transfer toward remote enemy |
| 3 | `0x4aca00` | Strength/target update, conditional decay, choose neighboring support group |
| 4,5 | `0x4ac740` | Strength/target update, noncritter threat switches to6, then decay |
| 6 | `0x4ac690` | Strength/target update, retry gate, noncampaign minimum, no-target departure or insufficient-strength dispersion |
| 7 | `0x4acbc0` | Strength/target update, possible dispersion, priority adjustment, local threat reaction, support readiness/timeout transfer |
| 8,9 | `0x4ac870` | Independently recovered in [campaign-wave launch](native-campaign-wave.md) |

Actual `0x4adbd0` recomputes local/all military sums and raises/caps needed strength as defined in [regional strength](native-campaign-wave.md). The present tests intentionally supply zero enemy-strength accumulators and no max-force cap when isolating decay. Needed-ground is region u16+`0x0c`, needed-air u16+`0x0e`; these stored requirements differ from target pointers and cached unit strength.

`0x4ac080(ECX=word_pointer,EDX=noncampaign)` performs this exact decay:

| Input | Campaign result | Noncampaign result |
|---|---|---|
| value>1000 | value−13 | max(0,value−300) |
| 200<value≤1000 | value−6 | max(0,value−300) |
| 2<value≤200 | value−2 | max(0,value−300) |
| value≤2 | 0 | 0 |

Twenty-eight direct calls cover0,1,2,3,199,200,201,299,300,301,999,1000,1001,65535 in both modes. Thirty-six dispatcher compositions cover states0/4/5, both modes and flag`0x40`, checking needed words, recomputed local/all strengths and no RNG. Flag`0x40` suppresses decay. State0 has no target update; states4/5 do. State3 decays only when both updated target pointers are null and flag`0x40` is absent.

## Reserve states one and two

`0x4ac7c0` updates targets first. An empty military head then changes the region to state0. With a nonempty head, a state1 ground-member count≥20 changes to state2 **and returns before global selection**. Six cases distinguish19/20/21 and both modes. The fixture deliberately supplies a count independent of its one list node to isolate the numeric gate; it is not evidence that every fixture count occurs naturally. State2 receives priority70 in campaign mode,160 otherwise, as established by actual state initialization.

Otherwise an empty cached slowest pointer invokes actual `0x4ab870`. That selector changes the pointer only in campaign mode; the present ordinary campaign member becomes the cached slowest, while the noncampaign pointer stays0.

If local targets exist, the handler evaluates `0x4ab490(region,region)`. Its real same-region weapon/path branch returns0 for the armed-member fixtures, preserving reserve state1/2 and avoiding global retargeting. The preceding target update can issue ComputerAI156. Four complete positive-target cases retain state, target pointers and assignment and record zero global-selector calls.

With no local target, the real global selector `0x4aba70` runs. A null result resets the source to0 without transferring its retained military list. Four positive remote-building compositions instead locate the actual enemy's terrain region, put that destination in the **source's state1 or2**, transfer military with arguments`[0,0,1]`, then change source to0. The released node is reused in the destination. Ordinary arrival issues158 for destination state1 and157 for state2, with three actual RNG calls in both cases. These numbers are distinct orders; issuing them does not execute physical movement.

## State three chooses a support neighbor

`0x4aca00` updates strength and targets, optionally decays requirements, then chooses a neighbor list. Literal terrain marker`0x1ffd` uses all neighbors via`0x481ad0`; other records use the ground prefix via`0x481ab0`. A prior state7 neighbor whose destination equals this region's ID causes an immediate return: no second support group is created in that invocation.

Candidates must have null air/ground targets and state0,4 or5. Campaign mode additionally excludes state0. The first-town helper `0x4872a0(player,out_x,[out_y])` reads town-list head `0x69b51c+player*8`; absent head returns0. A present head supplies **signed** position words+`0x20`/+`0x22`. The state3 caller then invokes actual `0x496ac0` from that town's region to the candidate and rejects a zero result. This caller tests the return, unlike the donor selector in [blocked-route adoption](native-donor-ranking.md). Three complete fixtures compare absent/present town and a targeted, preexcluded neighbor, including two or three direct original route calls. More complex town routes reuse [strategic route filtering](native-ai-routes.md); this batch's positive town routes are direct neighbors.

Ranking uses the candidate terrain record's **u16+4**, commonly named tile count. It maintains two slots initialized with score0:

```text
if score > best_score:
    best = candidate; best_score = score
else if score > other_score:
    other = candidate; other_score = score
selected = other if present else best
```

**A new maximum does not move the previous best into the other slot.** This is the exact native update, not a conventional second-largest selection. Twenty complete cases vary score/list order and campaign mode. Scores100,150,200 select the final200 candidate because the other slot stays empty;200,150,100 select150;100,200,150 also select150. Equal scores200,200,200 select the second list candidate: the first fills best, the second fills other, and the third fails both strict comparisons. All-zero supplied scores select no candidate. These boundary fixtures do not claim that ordinary generated regions have zero area.

The chosen region receives destination=current region ID and changes to state7. The current region stays3. Actual state initialization sets priority100 in the no-owned-worker/building fixtures and clears the chosen region's state7 counter. Six cases isolate state0/4/5 and campaign gates; two isolate full/ground-prefix neighbors; two distinguish a preexisting support destination that does or does not match this region.

## States four and five react, then decay

When actual updated air or ground target is a **noncritter**, `0x4ac740` changes the region to6. It then continues its existing call and applies decay unless flag`0x40` is present. Eight full positive-enemy cases check state6, priority100, both original target pointers, ComputerAI156, needed words and restored spatial-query state. The new state's later handler does not execute during this call.

Four critter-only cases preserve state4/5. Noncampaign mode retains the critter target; campaign regional target selection excludes it, leaving null pointers. This separates target selection's campaign policy from the subsequent noncritter transition gate.

## Combat state six

`0x4ac690` updates strength and targets **before** checking retry+7. A still-nonzero retry returns. Thus an initial retry2 becomes1 and postpones the transition/minimum work; an initial retry1 becomes0 and permits it immediately.

After that gate, noncampaign mode enforces a minimum1000 requirement: ground for literal terrain marker`0x1ffd`, air otherwise. This happens after the earlier strength/cap helper returns. Twenty-four no-target combinations cover campaign mode, marker, retries0/1/2 and a real spatially indexed owned worker. With no targets, campaign mode and no owned worker/building change to0; campaign with an owned worker or noncampaign mode change to5. The selected real query accepts the worker property, restores query depth/offset and does not fabricate a boolean result.

With targets and no owned worker/building, the handler invokes `0x4aa9b0`. Its insufficient-strength branch changes to3 and releases/reallocates military into actual current regions. Two nonexempt Mutalisk43 compositions execute that chain, reuse the node, retain its current-region assignment and issue ordinary157 with three RNG calls. Separate Zergling37 cases exercise the original **noncampaign first-member exception**, which suppresses dispersion despite the newly imposed air requirement1000. Campaign sufficient-strength fixtures remain6. This exception is a literal native branch, not a rule that all Zerglings globally ignore defense needs.

## Support state seven

The handler first updates strength/targets. With flag`0x20`, insufficient-strength dispersion can end processing before destination handling. Two complete first-Zergling cases distinguish campaign dispersion to3/order157 from the noncampaign exemption that continues waiting.

Otherwise it resolves the stored destination. If both source and destination have null targets and source priority is not50, it writes50 directly. Two destination-target fixtures preserve the old77 priority; the target's separate presence does not manufacture a local enemy encounter.

An actual local noncritter target changes source to6 and returns **before** counter/transfer handling. Two positive-enemy cases retain destination state3 and source MilitaryAI, issue the target update's156, and preserve the initial counter9 without movement RNG.

Without that interruption, it reads counter+9. If the old counter is below10, it increments it and compares **local** air/ground strength against destination requirements; equality passes both. If the old counter is already≥10, it bypasses these strength comparisons. Crucially, old9→new10 still checks strength and can wait; timeout occurs on the next visit. Twenty cases distinguish counters0/8/9/10/255, sufficient/one-short local strength and campaign mode. The byte is not incremented when its initial value is10 or255.

On readiness/timeout it changes destination to6, calls actual military transfer with arguments **`[1,1,1]`**, changes source to5, and sets destination retry+7 to5. The selected ordinary member is actually assigned to the destination and receives157 with three RNG calls. The extra-radius query runs on an explicitly empty index; its positive guard callback is independently recovered in [radius recruitment](native-radius-recruitment.md). Counter and cached source fields are not invented resets beyond the actual state initializer's writes.

## Reproduction and remaining compositions

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_region_states_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --out docs/research/evidence/native-region-states-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-region-states-probes.json
```

Without the execution flag the tool only validates/disassembles. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4, x86_64 Rosetta. Orders.dat SHA-256: `0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9`. Probe script SHA-256: `c825249747b940a99e80f6c7c0596529c011b55be7ff40b3208388f3ee3d4752`. JSON preserves current dependency hashes, exact original instruction allowlists, complete returns, native arguments and matched state. Each entry is bounded by100000 instructions.

Pinned [AISE region layout](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781), previously read during the earlier recruitment work, corroborates field names only. Original local1.17 instructions determine the actions above. These selected complete paths do not establish every composition: non-null garrison unloading, mixed positive radius recruitment within a support timeout, every transport/weapon route alternative, persistent physical movement and actual campaign execution remain separate. General strength-recipe production and the cached unit-strength producer are also separate dependencies; dispatching a state handler does not close those algorithms.
