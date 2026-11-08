# Native 1.17 unit strength, combat geometry and order transitions

This report specifies recovered behavior of the local **StarCraft 1.17.0.1** executable for the six-campaign remake. It extends [native combat AI](native-combat-ai.md), which covers target ranks, spell decisions and regional AI. Native addresses below are preferred virtual addresses in the exact PE32 x86 image with SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`; subtract `0x400000` to obtain RVAs. They are independently identified local functions, not transferred 1.16.1 addresses.

The [static witnesses](evidence/native-unit-ai-static.json) contain 47 code blocks, 2,106 decoded instructions and four native switch tables. No requested block has an undecoded tail. Partial caller fragments are explicitly named as branches or tails. The [execution evidence](evidence/native-unit-ai-probes.json) contains **330 passing cases**: **158 complete returns** and **172 prefixes stopped before a selected unreviewed handler**. Prefixes prove the decisions and writes preceding that boundary; they do not prove the called handler's effects.

These are isolated original instructions in Unicorn 2.1.4, with an x86-32 guest and an x86_64 host process under Rosetta. Inputs are explicit artificial CUnit, sprite, DAT and cargo fixtures in private memory. Helpers executed inside the allowlist are original instructions; there are no replacement eligibility or movement stubs. No Windows game process, WinMain, DLL, Windows API, host-file operation or network call runs in the guest. This is function behavior evidence, not a campaign playthrough or proof that every artificial state is reachable in an original mission.

## Evidence and reproduction

The extractor is [inspect_native_unit_ai_117.py](../../tools/research/inspect_native_unit_ai_117.py), SHA-256 `c61df5591107cd390ee679af03f6d09ae89c9aef95b531db8f887b53313759c4`. The execution runner is [probe_native_unit_ai_117.py](../../tools/research/probe_native_unit_ai_117.py), SHA-256 `a0af930eab6cb4a776689d07549289d58e2685d9ef28ad0012fc3bf2273d401c`. Its default mode only prints the plan; original instructions require the explicit execution flag.

```sh
python3 tools/research/inspect_native_unit_ai_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --order-names /private/tmp/koprulu-teippi/src/constants/order.h \
  --out docs/research/evidence/native-unit-ai-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_unit_ai_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-unit-ai-probes.json \
  --execute-original-code
```

Each case records its input, independently calculated expectation, actual output, instruction count, visited allowlisted functions and stop address. A complete return reaches a private sentinel. A prefix reaches its documented original handler entry before executing that handler. All calls have a 10,000-instruction bound and fail if an unexpected code boundary is encountered.

Pinned secondary implementations provide names and comparison points: [Teippi's strength calculation](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L1645), [OpenBW's dynamic strength](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L849), [GPTP's attack orders](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/base_orders/attack_orders.cpp#L773), and [Teippi's numeric order labels](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/order.h). Their behavior is checked against local instructions where specified below; their older addresses are not local anchors.

## Current strength and cached strength

`0x4a96b0`, ECX = unit and EDX = direction (`0` air, `1` ground), computes current strength. It immediately returns zero for unit IDs `35`, `36`, `59`, `97`: Larva, Egg, Cocoon and Lurker Egg. If status bit `0x40000000` is set, it compares rounded current life with maximum life and returns zero when current life is lower.

For ordinary nonnegative game values, the remaining calculation is:

```text
life = ceil(hitpoints_raw / 256)
     + (shields_enabled ? floor(shields_raw / 256) : 0)

maximum_hp = floor(dat_hitpoints_raw / 256)
if maximum_hp == 0:
    maximum_hp = ceil(hitpoints_raw / 256)
    if maximum_hp == 0: maximum_hp = 1
maximum_life = maximum_hp + (shields_enabled ? dat_max_shields : 0)
if maximum_life == 0: maximum_life = 1

multiplier = strength_multiplier(unit, direction)
strength = uint32_low(life * multiplier) / maximum_life
```

The final division is unsigned integer division. Original HP and shield shifts are arithmetic; HP rounding adds `255` before shifting by eight. These signed machine details matter for malformed or unusually large fixtures, even though ordinary live-unit HP is nonnegative.

The native helpers are rounded life `0x41faa0`, maximum HP `0x409c90`, maximum life `0x4200e0`, and multiplier `0x4a9630`. The multiplier starts from `0x6ac530[direction * 228 + unit_id]`; the ground table starts at `0x6ac8c0`. Base type strengths and their initialization are specified in [native-combat-ai.md](native-combat-ai.md).

Two multiplier exceptions are established:

- Bunker ID `125`: replace its own type strength with `occupied_transport_space * Marine_type_strength(direction)`.
- DAT property bit `0x200000`: add `energy_raw >> 9`, unless hallucination status `0x40000000` is set. This is half of whole energy, rounded down. Fractional energy below a full point does not affect the bonus.

Disabled status is not a blanket zero-strength condition here. The isolated disabled fixture retains its ordinary strength. The function has no direct weapon-upgrade or attack-disable adjustment; such inputs may affect decisions elsewhere.

The 46 direct strength cases cover both directions, full/half/fractional/zero/above-maximum HP, enabled and disabled shields, missing DAT HP, odd/even caster energy, noncasters, hallucinations, all four excluded forms, disabled status and 32-bit product wrap. A separate complete call to `0x4a8f30` confirms cache writes: air to unit `+0x138`, ground to `+0x13a`, both **16-bit**, independently of the 32-bit function result.

One fixed-point HP damage is insufficient to zero a hallucination whose rounded life remains at maximum. Losing one whole HP zeros it. Losing one fixed-point shield value from full shields can already reduce `floor(shields_raw / 256)` and zero its strength. The remake must preserve this asymmetry instead of using raw health fractions for hallucination validity.

The cache is distinct from an on-demand calculation. Regional strength accumulation reads cached words; other target-selection callers invoke current-strength calculation directly. Static caller witnesses show refresh after the completed-unit branch of HP assignment (`0x41bbb5`), shield damage (`0x41e140`), surviving HP damage (`0x41e246`), a type-change tail (`0x424f31`), HP rescaling (`0x426835`), initialization (`0x4309e0`), and a Hallucination conversion branch (`0x430a8f` inside `0x430a00`). These fragments establish refresh sites, not a complete reachability or cadence specification for their containing routines. Recomputing all caches every tick could change decisions between those original refreshes. The earlier timer-update label for `0x430a8f` was incorrect: the containing routine sets Hallucination status `0x40000000`, caps timed life at `+0x110`, refreshes strength and issues secondary order `0x95`. This site does not establish periodic refresh cadence.

OpenBW's formula agrees on the exclusions, life rounding, caster bonus and bunker weighting. Teippi's `UpdateStrength()` returns early for Larva/eggs, whereas the local refresh writes zero through the local calculation. For initialized valid units the difference may be invisible; for stale-cache/type-transition fixtures it is observable. Teippi's linked cargo list also does not reproduce the local packed-handle validation described next.

## Packed cargo handles and bunker weighting

`0x428b50`, ECX = packed handle, returns the native cargo unit pointer or zero. A zero handle is empty. Otherwise:

```text
index = handle & 0x7ff
unit = 0x5da3a0 + (index - 1) * 0x150
reject if removed_or_dying(unit)
reject if unit.byte[0xa5] != arithmetic_shift_right(handle, 11)
return unit
```

`0x4255f0` returns the native array base `0x5da3a0`. `0x41fe20` rejects a null sprite, or order `0` with order-state `1`. Order `0` with state `0` or `2` is not rejected by this helper. The packed representation supplies 11 index bits and, for the 16-bit cargo fields, five generation bits. The helper contains no explicit array-bound check: a nonzero handle with index zero addresses the preceding array slot. This is a recovered implementation detail, not authorization to accept invalid handles in external remake input.

`0x438350` reads **all eight** 16-bit slots beginning at unit `+0xb0`, validates every handle with the real resolver, and sums the DAT `space_required` byte at `0x523ef8[occupant.unit_id]`. `0x438140` instead finds the first valid loaded unit among the number of slots allowed by DAT capacity `0x523fe0[container.unit_id]`. Capacity and occupied-space scans are different algorithms.

Seven complete handle cases cover empty, valid, generation mismatch, removed, and three death-order states. Twenty complete space/first-unit/multiplier cases cover empty cargo, one occupant, a two-space occupant, only slot eight, and all eight slots. The slot-eight fixture has declared capacity four: the occupied-space calculation and bunker multiplier still count it, while first-loaded returns zero. This distinguishes the actual loop bounds; it does not claim that ordinary bunker loading permits such a state.

## Visibility and combat distance

`0x422c80`, ECX = attacker and stack = target, tests visibility. For a nonnull target, cloaked/burrowed status bits `0x300` require the attacker's player bit in target detection dword `+0xe4`. It then ANDs the target sprite visibility byte `+0x0c` with the **low byte** of `1 << attacker.player`. The nonzero result is a bitmask, not necessarily `1`. Owner eight therefore yields zero in this sprite-mask path even when the sprite byte is `255`. Eight complete cases verify owners `0`, `1`, `7`, `8` with absent and full visibility. The null-target branch samples the visibility tile at the order-target position using map width `0x5264b4` and tile pointer `0x666454`; it is statically recovered, not executed in this batch.

`0x4229a0`, ECX = unit and stack = radius, target, computes separation between bounding rectangles from DAT extents at `0x522c38 + id * 8`. A subunit with DAT property `0x10` uses its parent pointer `+0x70` for the attacker's rectangle. Bounds are calculated with 16-bit operations; right and bottom bounds include `+1`. Each axis gap is the nonnegative distance between rectangles, then the [native approximate distance](native-combat-ai.md) is applied. The return condition is **unsigned `distance <= radius`**.

For ordinary map coordinates, rectangle A is `[x-left, x+right+1] × [y-up, y+down+1]`. If centers are 100 pixels apart and both horizontal half extents are eight, the horizontal gap used here is `83`, not `84` or `100`. Complete probes confirm exact maximum-range equality and its adjacent failure, bounding-box reduction, and the inclusive rectangle test.

`0x421060` returns whether the unit is in weapon range. A null explicit target falls back to its order-target pointer; if both are null it returns `1`. Otherwise it requires `0x422c80` visibility, selects a valid subunit for weapon choice, picks air or ground weapon according to target status `4`, and disables unburrowed Lurker ID `103` ground weapon. Weapon ID `>=130` returns false. A nonzero minimum range at `0x5252b0[weapon]` rejects targets **at or below** that radius. The maximum check is inclusive. Seven complete range cases distinguish equality at maximum, one pixel outside, bounding-box effects, equality at minimum, one pixel above minimum, and zero minimum.

`0x420f00`, ECX = unit and first stack argument = weapon ID, provides maximum range from `0x5254b8[weapon]`. Unit status bit `0x20` adds 64 pixels. Native additions from its switch table are:

| Unit IDs | Condition | Additional pixels |
|---|---|---:|
| Marine `0` | Upgrade `16` present | 32 |
| Hydralisk `38` | Upgrade `30` present | 32 |
| Dragoon `66` | Upgrade `33` present | 64 |
| Goliath `3`, turret `4` | Weapon `8` and upgrade `54` present | 96 |
| Alan Schezar `17`, turret `18` | Weapon `10` and expansion flag `0x536720` | 96 |
| Fenix Dragoon `78` | Expansion flag | 64 |

Other unit IDs receive the base range plus the status-bit addition. The 18 direct range cases execute the real upgrade-level helper `0x49a470` and test the additions both absent and present, including a Goliath weapon mismatch.

`0x421120` adds a movement-dependent tolerance before the rectangle test. If attacker movement flag byte `+0x20` has bit `2`, it adds `halt_distance >> 8`, except when the target is also moving and the circular heading difference is `<=32`. Heading difference `0x40b990` is `min((target_heading-attacker_heading)&255, 256-difference)`. At difference `33`, the tolerance applies. Five complete cases prove the exact boundary and a stationary target.

The native halt-distance helper `0x40bf00` returns zero if current speed `+0x3c` is zero or byte `+0x27` is nonzero. If speed and acceleration match DAT defaults it returns precomputed halt distance `0x511178[flingy_id]`; otherwise it divides signed low-32-bit `speed * speed` by twice the 16-bit acceleration. The cases use a nondefault speed of 2,560 fixed-point units and acceleration 128, producing 25,600 fixed-point halt distance, or 100 pixels. This tolerance does not replace minimum-range rejection.

## Three stages of main-order dispatch

The local dispatcher is `0x439420`, ECX = unit. Its phases are visible in the complete switch tables in the static evidence, including numeric input, ordinal, case address and first transfer. Order labels come from pinned Teippi; table entries come from the local PE.

1. A special-order switch runs before the normal disable checks. Its mapping at `0x4399d4` covers numeric orders `0..174`; table `0x4399c0` has five entries. Selected special orders tail-transfer directly.
2. Normal orders reject status `0x400`, nonzero Lockdown `+0x117`, Stasis `+0x119` or Maelstrom `+0x124` by tail-transferring to `0x4af270`. A unit that lacks status `0x20000` (the mobility bit seeded from units.dat flag `0x08000000`) but has status `0x8000` (under Disruption Web) also calls `0x4af270` (`0x43948c`–`0x43949a`), then continues normally instead of returning. Corrected after the function atlas ([P12 `0x439420`](atlas/p12.md)): this second call was missing. Otherwise the every-visit switch maps `4..183` using `0x439b18`, with 37 entries at `0x439a84`. These handlers are called, then control continues to the throttled stage. The handler can change the current order before the third stage reads it.
3. Unit throttle byte `+0x85` is decremented. If its **old** value was nonzero, return. If its old value was zero, overwrite it with `8`, call help-reaction `0x4af2c0`, clear spell reservation byte `+0x92`, then dispatch the current order `1..187` through mapping `0x439d5c` and 100-entry table `0x439bcc`.

This is an eight-count delay followed by a dispatch on the ninth visit, unless another path resets the throttle. Reservation flags are not cleared on every dispatcher visit. The disabled reaction helper has its own timer check and may scan nearby units; it is a separate path from ordinary order execution.

All 188 order IDs `0..187` have executed prefix cases. Their fixtures have zero disable timers, no acid spores, no queued order and an old throttle of zero. The run stops before the first external selected handler, or returns when the selected case has no work. A reviewed no-op branch of `0x4af2c0` executes normally. Four additional complete calls with old throttles `1`, `2`, `8`, `255` confirm decrement-and-return and reservation preservation. This does not execute every order body or the second-to-third-stage continuation after every-visit handlers.

Selected recovered combat routes are:

| Order | Local handler | Dispatch phase |
|---|---|---|
| Guard `2` | `0x41e700` | Throttled |
| PlayerGuard `3` | `0x41ee10` | Throttled |
| Attack `8` | Default return; no selected handler | Throttled default |
| AttackObscured `9` | `0x427c90` | Throttled |
| AttackUnit `10` | `0x41e5c0` | Throttled |
| AttackFixedRange `11` | `0x41e400` | Throttled |
| AttackTile `12` | `0x4be950` | Throttled |
| AttackMove `14` | `0x41e4a0` | Throttled |
| StayInRange `21` | `0x4288f0` | Throttled |
| ComputerAI `156` | `0x48cd70` | Throttled |
| AiAttackMove `157` | `0x41e510` | Throttled |
| HarassMove `158` | `0x41e890` | Throttled |

Orders 157 and 158 were labeled ComputerAttack/ComputerAttackMove here; the pinned Teippi names are AiAttackMove (BWAPI `AtkMoveEP`) and HarassMove. Labels only; handlers and behavior are unchanged. Corrected after the function atlas ([P07 `0x41e510`/`0x41e890`](atlas/p07.md)).
| AIGuard `160` | `0x41e7f0` | Throttled |
| ComputerReturn `163` | `0x41e370` | Throttled |

The complete mappings also locate spell, transport, siege, carrier/reaver, worker and construction orders. A resolved handler address is an entry point, not by itself an implemented behavior contract.

## Guard transition and its RNG consumption

`0x41e700` performs the generic Guard order. It first calls the real ranged PRNG `0x478270` with range `0..15`, source ID `29`, and stores the result in main-order timer `+0x54`. It then chooses:

```text
if player_type != 1:
    order = PlayerGuard (3)
else:
    order = ComputerAI (156)
    if unit.ai != null:
        if unit.ai.type == 1: order = AIGuard (160)
    else if DAT_properties & 0x40:
        order = PlayerGuard (3)
```

Thus player type, AI presence/type and the no-AI flyer property are independent branches. Fourteen complete cases cover human/computer, no AI, guard/worker/building/military AI, and an AI-less computer flyer, each with RNG enabled and disabled. The enabled call advances the shared seed and both global and source-29 counters exactly once. Disabled RNG returns timer zero and leaves counters and seed unchanged. Use the integer-safe PRNG contract in [native-prng.md](native-prng.md); this transition consumes randomness even when the eventual order becomes PlayerGuard.

## Attack orders and the target-approach FSM

The following body decisions are statically recovered from local instructions. Where a branch was executed, its exact stop boundary is stated.

**AttackFixedRange `0x41e400`:** an AI-associated unit calls best acquisition `0x431c40` and replaces its target; a unit without AI calls target-maintenance `0x41de10`. A nonnull target updates the order-target position. Failed native capability `0x41da50` ends the order through `0x42a5a0`. If capable but outside weapon range, an AI unit issues AttackMove `14` toward the target position via `0x42a340`; a unit without AI ends the order. If in range, update attack waypoint `0x40cbd0`, then begin weapon animation through `0x41f630` with animation `5`.

**AttackMove `0x41e4a0`:** call attack-reaction `0x420600` with the current order. Continue only if that helper returns true and the main-order timer is zero; set the timer to `15`. If movement target equals current sprite position, end the order. Otherwise call target acquisition/queueing `0x4206e0`. The equality path computes `(status & 0x80000 ? 1 : 0) + 1` then branches on zero, which can never occur: the status bit does not prevent exact-arrival completion. Two complete nonzero-timer cases confirm no timer write inside this handler; one original prefix confirms exact-arrival completion with status `0x80000`, stopping before `0x42a5a0` after storing timer `15`. Timer decrement is outside this handler and is not inferred from these calls.

**Attack-reaction `0x420600`:** state zero first attempts a movement target change through `0x43a070` and sets state one on success. It validates auto-target pointer `+0x7c` through enemy predicate `0x4290e0`, rejects hidden, invulnerable and undetected targets, and returns true when there is no valid auto-target. An AI unit delegates to attack-target update `0x41f6c0(0,0,0)` and inverts its result. A non-AI unit stops movement, queues its existing order and target position, queues the DAT attack order against the auto-target, clears auto-target, then activates the queued order and performs `0x41cf90`. Those queue effects are static, not executed in this batch.

**AttackUnit `0x41e5c0`:** for a computer player whose target is absent or lacks DAT building property `1`, first call military return-home decision `0x4b2110(unit,0)`. A true result completes this invocation. Otherwise call target-maintenance `0x41de10` and attack main `0x420770`. A true attack-main result calls AI attack support `0x4b1100` when AI exists, then starts animation `5` through `0x41f630`.

**Attack main `0x420770`:** failed native attack capability ends the order. For an existing invalid target, if status `0x08000000` is clear and AI is absent, queue Move `6` to the target's current sprite position before ending; a null target or set status bit first stops movement. A valid target updates both order-target position and attack waypoint. State zero checks weapon range: when outside it, movement-to-unit `0x43a180` must succeed; then set state one and return true. When already in range, set state one and return true directly.

For a nonzero state, `0x420ab0` tests visibility and effective maximum range without minimum-range rejection. If outside, AI units check unreachable-target `0x421320` and movement-state `0x41a250`; an unreachable or stuck result attempts AI retargeting `0x41f6c0(0,1,0)`, otherwise stops and ends. Without AI, a switchable-target status `0x08000000`, neither status bit `4` nor `8`, and an airborne target also causes stop/end; movement-state two causes stop/end. Remaining units approach: Scourge ID `47`, or a ground weapon with a zero flingy field `0x524f18`, takes melee approach `0x41d8f0`; other cases take `0x438ba0`. If inside range, stop movement and return true; Scourge instead uses its melee approach and returns true.

Five original attack-main cases include one **complete state-zero in-range return** and four prefixes: state-zero outside range or invisible target before `0x43a180`; state-one in range before `0x43a490`; state-one ranged approach before `0x438ba0`. They verify target-position/waypoint writes and state persistence without substituting movement helpers. The movement-state helper has four additional complete cases: different position returns zero; exact position returns one or two according to status `0x80000`.

Teippi's later `Order_AttackUnit` explicitly adds states two and three and describes changed behavior to prevent stuck iscript-moving units. The local `0x420770` sets state one and treats every nonzero state through the same branch. Those Teippi corrections must not be copied as original 1.17 campaign semantics. [Teippi's implementation](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L3258) is useful as a comparison, not a native FSM substitute.

**AiAttackMove `0x41e510`:** after attack-reaction and timer zero, store `15`, compare current and order-target region using `0x481ba0` and `0x481b10`. Equal regions stop movement and end. Otherwise movement-state and movement-target helpers decide whether to retry movement, hold, or call acquisition `0x4206e0`.

**HarassMove `0x41e890`:** after attack-reaction and timer zero, store `15`, then apply group-leader decision `0x4afaf0`, leader readiness `0x4aff20`, follower predicate `0x4b09c0`, regroup decision `0x4afec0`, and group support `0x4b0e10`. A follower can move toward its leader through `0x43a180`. Subsequent region and position checks distinguish exact arrival, movement-target updates and acquisition. In the equal-region, unequal-position branch, the local comparison at `0x41e96b` routes an approximate center distance **greater than 96** to order completion; lesser/equal distance continues toward movement-target update. Preserve this branch as observed while recovering the preceding group helper contracts; do not reverse it based on expected strategy behavior.

## Remaining discriminating work

The recovered formulas, loop bounds, geometry and guarded transitions above are concrete native contracts. Full movement, queue mutation, weapon-animation effects and military-group behavior still require their called routines. The next narrow original probes are:

- Execute native order queue insertion/activation and order completion, with empty, one-entry and multi-entry queues; observe order IDs, target pointers/positions, queue link mutation and reservation bits.
- Execute `0x43a180`, `0x438ba0`, `0x41d8f0` on matched small terrain fixtures, then continue attack main across multiple invocations. Distinguish already-in-range, unreachable, target disappearance, ground attacker versus flyer, Scourge, minimum range and subunit-parent geometry.
- Recover `0x4b2110`, `0x4b1100`, `0x4afaf0`, `0x4aff20`, `0x4b09c0`, `0x4afec0`, `0x4b0e10` before interpreting group cohesion and return-home behavior. Use different AI types, guard-home distances, regional target flags and leader/follower strength ordering.
- Trace all callers of cache refresh through one logical object step with controlled energy, damage, regeneration and type changes. Compare on-demand strength against the cached words before and after each refresh; do not assume a per-tick cache rewrite.
- Continue original spell weapon-target validity and issue-order effects from the [combat AI report](native-combat-ai.md), using the actual shared globals and reservation flags rather than independent callback fixtures.

For Defold, authoritative combat decisions should operate on fixed-point unit state and original integer formulas, while visual interpolation follows that state. Cache refresh events, spatial ordering, reservation clearing and shared RNG calls are observable simulation behavior. They must be modeled explicitly before campaign parity is asserted. The frame and regional scheduling anchors remain in [native-frame.md](native-frame.md), AIScript execution in [native-ai.md](native-ai.md), and PRNG behavior in [native-prng.md](native-prng.md).
