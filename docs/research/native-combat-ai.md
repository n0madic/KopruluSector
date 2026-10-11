# Native 1.17 combat targeting and military AI

The three disputed acquisition rules are now resolved against local original instructions: an unarmed completed target with status bit `0x20000` (the mobility bit: ordinary mobile units and lifted buildings) has priority **3**, and one without it (grounded buildings and other immobile units) has priority **4** (corrected after the function atlas, see the priority section); an AI Scourge chooses the greatest **rounded** HP-plus-shields within the first nonempty priority bucket; and best-target acquisition for an AI unit uses the greater of seek range and the **raw DAT sight range**, except for the bunker branch. Equal scores retain the first candidate. These decisions are supported by static recovery and isolated original-function execution.

The exact target is StarCraft.exe, PE version **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. All addresses below are preferred VAs at ImageBase `0x400000`; RVA is VA minus this base. Function names are analytical labels. They were located through local instructions and structural relationships; 1.16.1 reference addresses were not copied into this target.

Evidence consists of [62 static witness blocks](evidence/native-combat-ai-static.json), [93 passing isolated original-instruction probes](evidence/native-combat-ai-probes.json), the [static extractor](../../tools/research/inspect_native_combat_ai_117.py), and the [bounded probe runner](../../tools/research/probe_native_combat_ai_117.py). The probes use Unicorn 2.1.4 in an x86_64 process, x86-32 guest mode, explicit private-memory fixtures, and an instruction allowlist. There are **no replacement helper stubs**. They execute neither WinMain nor a complete game. The best-acquisition range probe stops at a verified breakpoint before searching; its result is explicitly a function-prefix result.

## Reference sources and matching method

The principal comparison is [GPTP attack_priority.cpp](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/attack_priority.cpp#L104), with independent [OpenBW acquisition code](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L1841). GPTP supplies a precise hypothesis for six priority buckets, sixteen entries each, and the Scourge special case. OpenBW supplies a useful comparison, but the pinned implementation tests a can-move predicate in the unarmed branch where the PE tests the status-`0x20000` mobility bit (same intent; see below), and lacks the AI sight-range and Scourge branches in `find_acquire_target`. The local PE determines the target behavior.

Guard recovery follows the sequence in [Teippi Unit::Order_AiGuard](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit_ai.cpp#L14) and the independent [Samase_scarf spell-cast detector](https://github.com/neivv/samase_scarf/blob/f1681dbac8c7d1ac184dc84918ab7dd5771061ed/src/ai.rs#L1800): order `0xa0` calls a function with the unit and zero before attempting an ordinary attack target. Regional recovery follows [Samase_scarf's state-8 signature](https://github.com/neivv/samase_scarf/blob/f1681dbac8c7d1ac184dc84918ab7dd5771061ed/src/ai.rs#L2148): a `0x34`-byte region, state switch, strength clear at `+0x10`, target update, flag `0x20`, abandonment call, and transition to state zero before choosing an attack target. The local instructions match these relationships, not merely the reference function names.

Numeric unit/order/tech names come from pinned schemas: [unit IDs](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/unit.h), [orders](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/order.h), [tech IDs](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/tech.h), and [CUnit fields/flags](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/structures/Cunitlayout.h#L268). The PE DAT descriptors independently corroborate 228-element shield-enable, property, seek, and sight columns at `0x520458`, `0x521660`, `0x5219f0`, and `0x521ad8`. Field names are schema correlations; exact offsets, widths, masks, comparisons and writes are native evidence.

## Attack-priority algorithm

`target_rank` is at **`0x4319d0`**. Its local ABI is ECX=attacker and one stack argument=target. Smaller ranks are selected first.

```text
effective = target
if target.id == Bunker and first_loaded_unit(target) != null:
    effective = first_loaded_unit(target)

if target.id in {Larva, Egg, Cocoon, LurkerEgg}:
    return 5

if DAT.properties[effective.id] & Worker:
    rank = 2
else if can_attack_target(effective, attacker, 1):
    rank = 0
else if has_weapon(effective):
    rank = 2
else if effective.status & 0x20000:     # mobility bit (units.dat 0x08000000), not IsBuilding
    rank = 3
else:
    rank = 4

if not (effective.status & Completed) or effective != target:
    rank += 1
if rank == 0 and effective.status & 0x8000:  # CanNotAttack / Web
    rank = 1
return rank
```

The egg check uses the original target ID. The completion/cargo adjustment uses the effective unit. A loaded bunker therefore inherits its first valid occupant's properties/capability and gets one rank increment even if that occupant is completed. `first_loaded_unit` at `0x438140` reads DAT cargo capacity, scans the stored cargo slots, resolves each slot through `0x428b50`, and returns the first valid unit. Its full cargo-index validity rules have not been executed in these probes.

The unarmed branch is a **status-bit test**. At `0x431a46` the code masks `0x20000` and produces 3 when set, otherwise 4. GPTP names that bit `IsBuilding`, but the name is wrong: the bit is copied from units.dat flag `0x08000000` at creation (`0x425a70`) and transport unload (`0x4249e0`), recomputed as set for a non-subunit building on liftoff (`0x435f00`, `0x437010`), and cleared during Archon meld (`0x42f830`, `0x42fa70`) and grounded-building/bunker load (`0x437c70`). No stock building has the DAT flag; ordinary mobile units do. The branch therefore ranks unarmed **mobile** targets 3 and unarmed **immobile** ones, including grounded buildings, 4. This matches the intent of the pinned OpenBW branch that produces 3 for `u_can_move(target)`; exact equivalence of that predicate with this status bit is not verified. Corrected after the function atlas ([V01 item 1](atlas/v01.md), [P12](atlas/p12.md) `0x4319d0`); the probe results stand on their inputs, because the probe's `building` fixture flag sets exactly status `0x20000` (input status `132097`/`132096` yields 3/4, `1025`/`1024` yields 4/5) and not the DAT Building property. Probe cases vary worker/status-`0x20000`/completion flags while running the original disabled-unit rejection and original has-weapon helper. Further cases exercise actual native Carrier retaliation, the Web adjustment, and armed-but-disabled rank 2.

`has_weapon` at `0x41d9c0` is broader than a nonempty ground-weapon column: it considers air weapons, the burrowed-Lurker restriction, Carrier/Gantrithor fighter counts, Reaver/Warbringer ammunition, and a subunit's weapons. The Carrier count addition is byte arithmetic. Preserve the actual unit and subunit checks rather than inferring capability from a generic attack flag.

`can_attack_target` at `0x41da50` rejects an attacker with status bit `0x400`, lockdown `+0x117`, stasis `+0x119`, or maelstrom `+0x124`; a null target; target status bit `0x04000000`; and target sprite flag `0x20`. Its optional cloak check calls `0x40a180`, which tests target status `0x300` against the attacker's player bit in detection mask `+0xe4`. It selects a real subunit through `0x420000`. Special branches handle Carrier/Gantrithor, ground-only Reaver/Warbringer with reachability, a special attacker/target pair of predicates, and AI Arbiter rejection. The final ordinary branch chooses air or ground weapon by target status bit 4. The meaning and full behavior of the special predicates at `0x41ff30`/`0x41fed0` and reachability at `0x481cb0` remain to be recovered separately.

## Six buckets, eligibility, enumeration and ties

`candidate_bucket_callback` at **`0x432280`** applies these gates, in order:

1. Candidate must differ from the attacker.
2. Candidate sprite visibility byte must contain the attacker's player bit.
3. Native attackability `0x41da50` and enemy predicate `0x4290e0` must pass.
4. If the stored minimum range is nonzero, a candidate within that range is rejected.
5. Candidate must be within the stored maximum range.
6. For a unit that cannot turn around to attack, the appropriate subunit's ground-weapon attack-angle test must pass.
7. Native chase rejection `0x48d2e0` must return zero.
8. Compute the native rank; append only if that bucket has fewer than sixteen entries.

Bucket storage is six arrays of sixteen pointers at `0x665b70`, with six uint32 counts at `0x665cf0`. Element address is `0x665b70 + rank*64 + index*4`. Extra entries are **discarded**, with no replacement by a nearer or healthier candidate. A global sort over every eligible unit changes this algorithm.

The chase helper tests **player type 1**, rather than merely the unit's nonnull AI pointer. For a ground attacker belonging to that player type, if the candidate is outside the native attack-range predicate `0x421060`, the helper returns the candidate's status bit `0x80`; all other branches return zero. This bit is also managed by the slower AI refresh documented in [native frame research](native-frame.md). Treating it as a timeless unit-type property would change chase eligibility.

`0x442b20` builds a signed-16-bit rectangle around the supplied center. `0x442b60` obtains a null-terminated pointer list from `0x422330`, then visits it from its first entry to its last. The callback may terminate iteration by returning nonzero. Target bucketing uses a callback that returns zero after insertion/rejection, so this path processes the collected candidates.

`0x422330` assembles the list by walking the native spatial array at `0x5c99b8` forward, deduplicating with an incrementing marker at `0x5da38c` and per-index markers at `0x5d6e60`, and checking the candidate's stored bounds against the other spatial interval. Thus tie behavior and the sixteen-entry cap depend on spatial enumeration. The exact equal-coordinate ordering maintained by the insertion/movement routines is still a required recovery item; a Defold unit-ID sort must not be substituted without proof.

## Seek range and the two acquisition paths

Seek-range accessor **`0x41f320`** returns an 8-bit result used by the callers. Its base is DAT `0x5219f0[unit_id]`. The native switch adds:

| Unit IDs | Condition | Tile bonus |
|---|---|---|
| Marine `0` | Upgrade `0x10` nonzero | 1 |
| Goliath/turret `3,4` | Upgrade `0x36` nonzero | 3 |
| Hydralisk `38` | Upgrade `0x1e` nonzero | 1 |
| Dragoon `66` | Upgrade `0x21` nonzero | 2 |
| Alan Schezar/turret `17,18` | Expansion-mode byte `0x536720` nonzero | 3 |
| Fenix Dragoon `78` | Same expansion-mode byte | 2 |

Ghost, Sarah Kerrigan, Samir Duran, Alexei Stukov and Infested Duran IDs `1,16,99,100,104` return zero when status `0x300` is set, unless current order is `0x6b` (HoldPosition). These checks happen before the upgrade switch. The upgrade accessor `0x49a470` selects the base or expansion upgrade array and uses the owning player's row; it is executed in the probes.

**Random acquisition**, `0x431b70`, clears all six counts, takes `seek*32` pixels, adds 64 pixels when `InBuilding`, stores minimum range zero, and searches a rectangle with an additional 64-pixel margin. It takes the first nonempty bucket. One entry returns directly; more than one calls the original raw RNG with source **`0x13`**, then uses `result % count`. It does not consume a random number for an empty result or a singleton. [Exact RNG](native-prng.md).

**Best acquisition**, `0x431c40`, clears the same counts and applies:

```text
tiles = seek_range(unit)
if unit.status & InBuilding:
    tiles += 2
else if unit.ai != null:
    tiles = max(tiles, DAT.sight_range[unit.id])
maximum_pixels = tiles << 5
minimum_pixels = minimum_present_air_or_ground_weapon_range(unit)
search_radius = maximum_pixels + 64
```

The sight value is a raw DAT byte. This code does not call a dynamic sight accessor or apply blindness/sight-upgrade rules through that accessor. The bunker branch skips the AI maximum. For example seek=4/sight=9 gives 128 pixels for a non-AI unit, 288 for an AI unit, and 192 for an AI bunker occupant. These values were read after executing the actual prefix through `0x431c8a`, stopping before the minimum-range call. Cloaked-Ghost seek suppression and the subsequent AI DAT maximum are separate operations; do not assume the suppression itself forces a zero best-search radius.

The minimum-range helper `0x433c50` selects the smaller minimum among present air and ground weapons, one present weapon's minimum, or zero if neither is present. Minimum-range exclusion is inclusive. The search rectangle's 64-pixel expansion is a collection margin; it does not extend the maximum eligibility range.

## Best member: rounded life or approximate center distance

Selector **`0x433910`** receives attacker in ECX and rank in EDX. A singleton returns immediately. For `unit.ai != null` and unit ID `47` (Scourge), it chooses the candidate with greatest:

```text
life = ((raw_hp + 255) arithmetic_shift_right 8)
if DAT.shields_enabled[candidate.id] != 0:
    life += raw_shields arithmetic_shift_right 8
```

This is ceil(HP) plus floor(shields) for ordinary nonnegative values. It is neither raw HP+shields nor ceil(HP+shields). `0x41faa0` supplies the first candidate's value; the loop repeats equivalent native arithmetic inline. Its strict unsigned `>` comparison preserves the first candidate on a tie. Stored shields are ignored when the DAT shield-enable column is zero.

Other attackers, including human-controlled Scourge, choose the smallest **sprite-center** distance. They use the approximation at **`0x405cf0`**, not squared Euclidean distance or unit-box distance:

```text
a = max(abs(dx), abs(dy))
b = min(abs(dx), abs(dy))
if (a >> 2) >= b:
    distance = a
else:
    c = (3*b) >> 3
    distance = a - (a >> 4) - (a >> 6) + c + (c >> 5)
```

The strict unsigned `<` comparison again retains the first candidate on a tie. Probes cover the diagonal approximation, its quarter-slope branch boundary, signed coordinate differences, greatest-life versus nearest selection, shield-enable behavior, and ties where raw-life ordering would choose a different target. The range gates and this final ranking distance are distinct mechanisms.

## Guard orders and execution throttles

Main-order dispatcher **`0x439420`** independently establishes the order mappings:

| Order | Local handler |
|---|---|
| ComputerAI `0x9c` | `0x48cd70` |
| AIGuard `0xa0` | `0x41e7f0` |
| ComputerReturn `0xa3` | `0x41e370` |

Before these dispatch cases, `0x439640` reads the old byte `unit+0x85`, writes old-1, and returns when the old value was nonzero. Old zero reloads **8**, calls `0x4af2c0`, and clears byte `unit+0x92` (AI action/reservation flags). Absent redistribution or another writer this makes eligibility recur after **nine dispatcher visits**, not eight. The unit-step redistribution and full per-unit timer cadence require the separate frame/order analysis; this byte is not automatically a once-per-game-frame countdown.

`AIGuard`, **`0x41e7f0`**, returns while `order_timer +0x54` is nonzero. Otherwise it sets 15 and:

1. Calls the reaction-spell dispatcher `0x48d3a0(unit,0)`; success ends this visit.
2. Calls `0x41f6c0` with the ordinary target-update arguments `(0,1,0)`; success ends this visit.
3. Returns for Observer ID `84`.
4. For AI type 1, compares guard home `AI+0x12/+0x14` with current sprite coordinates. If different, a sieged tank first queues TankMode `0x63`; it then queues ComputerReturn `0xa3` to home and finishes the current order.
5. Otherwise issues ComputerAI `0x9c`.

`ComputerReturn`, **`0x41e370`**, first attempts the same target update. Medic ID `34` receives its specialized handler and exits if that changes the order. State zero attempts movement to the stored order target, setting state one only when the movement helper succeeds. A non-guard AI record changes to ComputerAI. A guard record that is standing still updates its home and issues **Guard `0x02`**, not AIGuard `0xa0`. These are different order ordinals in the schema.

`ComputerAI`, **`0x48cd70`**, sets timer 15, handles Larva and dying-unit predicates, and has a Medic path. Without an AI record it issues the DAT default idle order. With an AI record it enters `0x48e180`, then dispatches AI type 2 to worker helper `0x487580`, type 3 to building helper `0x487110`, and type 4 to military helper `0x4b1010`; native branches reload timers 3 or 45. Worker repair, siege-tank, flying-unit and cloak branches follow. The bodies of these worker/building/military decision helpers are outside this recovered guard specification.

## Reaction spells: native decision chain

**`0x48d3a0`** receives unit in ECX and a reactive flag in EDX; AIGuard passes zero. When the flag is zero, byte `PlayerAi+0x22d` must be zero: accessor `0x4833c0` checks `0x67d295 + player*0x4e8`, with PlayerAi base `0x67d068`. A nonzero reactive flag bypasses this gate. The earlier relative label `+0x255` was incorrect; the absolute witness address was unchanged. Subsequent actual Comsat dispatch probes confirmed that setting `+0x22d` suppresses this branch, whereas `+0x255` does not.

The dispatcher subtracts one from the unit ID and uses a 71-byte mapping plus eleven-entry jump table at `0x48db28`/`0x48dafc`. The ten specialized unit IDs and their chains are below. All other IDs follow the default return, including hero variants not explicitly present in this table. This does not establish that heroes never cast spells: scripts, direct orders and other AI paths remain distinct mechanisms.

| Unit / ID | Block VA | Ordered attempts; order / tech / selector |
|---|---|---|
| Ghost / 1 | `0x48d75f` | Lockdown `0x73 / 1 / 0x432e20`, then nuclear path `0x7e / special / 0x432f40` |
| Science Vessel / 9 | `0x48d804` | EMP `0x7a / 2 / 0x432d00`, Matrix `0x8d / 6 / 0x432c70`, Irradiate `0x8f / 7 / 0x432dc0` |
| Battlecruiser / 12 | `0x48d3e1` | Yamato `0x71 / 8 / 0x433240` |
| Medic / 34 | `0x48d928` | Restoration `0xb4 / 24 / 0x432c50`; additional restoration path `0x433000`; Optical Flare `0xb9 / 30 / 0x432ed0` |
| Queen / 45 | `0x48d4c4` | Broodlings `0x79 / 13 / 0x432bf0`, Parasite `0x78 / 18 / 0x433020`, Ensnare `0x92 / 17 / 0x432d50`, with conditional ordering below |
| Defiler / 46 | `0x48d432` | Plague `0x90 / 15 / 0x433060`, Dark Swarm `0x77 / 14 / 0x4331d0` |
| Corsair / 60 | `0x48d8d7` | Web `0xb5 / 25 / 0x432cb0` |
| Dark Archon / 63 | `0x48d9f4` | Feedback `0xb8 / 29 / 0x4330f0`, Mind Control `0xb6 / 27 / 0x432eb0`, Maelstrom `0xba / 31 / 0x432fa0` |
| High Templar / 67 | `0x48d6b5` | Storm `0x8e / 19 / 0x4330a0`, Hallucination `0x94 / 20 / 0x432da0` |
| Arbiter / 71 | `0x48d625` | Stasis `0x93 / 22 / 0x433180`, Recall `0x89 / 21 / 0x433140` |

The usual attempt consists of readiness, selector, same-order/same-target check, and reservation/order issuance. **Already holding the selected spell order and selected target returns zero immediately from the entire dispatcher**; it does not necessarily try the next spell. Selection failure or reservation failure can enter the next spell's branch. Rewriting this as an unconstrained list of independently retried actions changes control flow.

Readiness **`0x48e580`** uses order-to-tech lookup `0x4265e0` and raw energy comparison `0x434b50`, then requires `CanUseTech` helper `0x407f90` to return exactly 1. The order's tech byte comes from `0x51b800`; tech IDs below 44 read uint16 energy cost at `0x51f868`, multiplied by 256. Other IDs return zero. The energy comparison is inclusive at the required cost and returns "insufficient" when current uint16 energy is smaller; cheat flag `0x800` at `0x5432fc` bypasses insufficiency. Those cost/comparison branches were executed in isolated probes. Full ownership/research and unit-state checks inside `0x407f90` are separate work.

Reservation helper **`0x48e5c0`** rejects a null target or `target.aiActionFlags & mask`. If the current order differs, it calls `0x42a380` and ORs the mask into target byte `+0x92`. If the current order already equals the requested order, it returns 1 **without issuing, retargeting or setting the bit**. The higher-level same-target check handles one case; an existing matching order with a different target can still reach this success return. Original-instruction probes confirm this behavior without stubbing order issuance. The actual different-order issuance branch is statically recovered and was not executed here.

All ordinary successful attempts shown use reservation mask **1**; Parasite uses **4**. Mind Control's call here uses **1**, not 2. Reservation lifetime depends on main-order clearing at `0x439663`, so an unconditional per-frame clear is not established by this helper alone.

Special chain predicates are also concrete:

- Queen: PlayerAi flag `0x20`, tested by `0x4831e0`, invokes raw RNG source `0x2a`. Low byte equal to **1** selects the Parasite-first branch; other bytes select Broodlings. Without that flag, current energy's high byte is compared with maximum energy: equality selects Parasite first. Successful Broodlings additionally queue Move `0x06` to the Queen's current position. Ensnare is reached after failed Broodlings, subject to the flag/max-energy gate.
- High Templar: Hallucination fallback requires the current-energy high byte to equal maximum energy; its selector rejects a nonzero reactive flag.
- Medic: Optical Flare fallback has the same maximum-energy gate. A second Restoration path uses `0x482450`, an order inequality, and selector `0x433000`; that selector rejects reactive calls and targets the caster's own units with `+0x121` or `+0x124` set and weapon-target validity for Restoration.
- Dark Archon: Maelstrom fallback requires energy of at least `min(cost(Maelstrom)+cost(MindControl), max_energy)` and `CanUseTech(Maelstrom)==1`. This preserves energy for a possible Mind Control. Feedback/Mind Control are attempted first.
- Ghost: the nuclear fallback checks player state `0x4832a0`, finds a nuke connection through `0x436f60`, selects a target, reserves/issues `0x7e`, writes the Ghost connection into the nuke's parent, and updates the player timer through `0x483270`. The complete nuke availability and area-evaluation predicates are not implied by those call names.

## Spell search windows and target predicates

The selectors use nearest-qualified-target searches at `0x442bc0`/`0x442c20`/`0x442d90`, with native approximate center distance and strict improvement. Their search rectangles are broader than actual cast range; the spell order subsequently moves/casts. Numeric windows below are native rectangle half-extents in pixels, not proven circular cast radii. Let `R` be the supplied reactive flag and `C` be PlayerAi flag `0x20`.

| Spell | Selector / callback VAs | R != 0 window | R == 0 window |
|---|---|---|---|
| Broodlings | `0x432bf0 / 0x432650` | 288 | 2048 |
| Restoration | `0x432c50 / 0x4327e0` | 1024 | 1024 |
| Matrix | `0x432c70 / 0x4328d0` | 288 | 2048 |
| Web | `0x432cb0 / 0x4326e0` | 288 | 2048 + 2048*C |
| EMP | `0x432d00 / 0x4329e0`, then `0x432950` | 288; no fallback | 2048; shield-first, energy fallback |
| Ensnare | `0x432d50 / 0x432ac0` | 288 | 1024 + 1024*C |
| Hallucination | `0x432da0 / 0x433260` | Return null | 1024 |
| Irradiate | `0x432dc0 / 0x4332f0` | 288 | 2048 |
| Lockdown | `0x432e20 / 0x433360` | 288 | 1024 + 1024*C |
| Mind Control | `0x432eb0 / 0x433460` | 1024 | 1024 |
| Optical Flare | `0x432ed0 / 0x433550` | 288 | 1024 |
| Nuke | `0x432f40 / 0x4335b0` | Whole-map rectangle | Whole-map rectangle |
| Maelstrom | `0x432fa0 / 0x4336b0` | 288 | 1024 + 1024*C |
| Parasite | `0x433020 / 0x433870` | 288 | 2048 |
| Plague | `0x433060 / 0x433a50` | 288 | 4096 |
| Storm | `0x4330a0 / 0x433b10` | 288 | 1024 + 1024*C |
| Feedback | `0x4330f0 / 0x433bb0` | 288 | 1024 + 1024*C |
| Recall | `0x433140 / 0x433c10` | Region gate, then whole-map | Region gate, then whole-map |
| Stasis | `0x433180 / 0x433d10` | 288 | 1024 + 1024*C |
| Dark Swarm | `0x4331d0 / 0x433e60` | 288 | 1024 + 1024*C |
| Yamato | `0x433240 / 0x433f40` | 512 | Whole-map rectangle |

A call to the campaign-flag accessor whose result is unused does not make the constant window depend on that flag. Whole-map rectangle pointer is `0x543228`. Recall's preceding region gate is `0x4aea70(player, caster_region)`; its full predicate is not recovered here.

Common visible-enemy predicate **`0x433df0`** checks sprite visibility, cloak/detection when status `0x300` is set, and native enemy relationship. It excludes Interceptor `73`, Spider Mine `13`, Scarab `85`, and orders `0x51/0x52/0x53` (MoveToGas/WaitForGas/HarvestGas). These exclusions are explicit even for a unit otherwise satisfying the spell's weapon-target flags.

The subsequent `0x41dbf0` calls use **weapon IDs**, not tech IDs: examples include Lockdown weapon `0x20`, EMP `0x21`, Irradiate `0x22`, Broodlings `0x39`, Plague `0x3c`, Stasis `0x53`, Storm `0x54`, Restoration `0x66`, and Maelstrom `0x6c`. The distinction is necessary to avoid looking up the wrong targeting flags.

Recovered callback rules include:

| Spell | Concrete native eligibility after common gates |
|---|---|
| Yamato | Armed **grounded** building (status bit 2), or occupied bunker, qualifies immediately. Otherwise reject Egg; require rounded life **200..450 inclusive** and no Hero property. The status test is not InAir. |
| Lockdown | Reject status `0x400` and existing lockdown/stasis/maelstrom. A moving loaded Shuttle/Dropship, Carrier or Reaver qualifies; other candidates must be attacking a non-enemy unit owned by a regular player. Hero variants are not automatically added to those exact-ID exceptions. |
| Matrix | Target is non-enemy, has a current attack target, no existing matrix, no RequiresDetection bit, max HP >=100 and rounded current HP <=75. It then requires the unresolved area/tech helper `0x434130` with native arguments `(6,target,228,x,y,0)`. |
| Restoration | Same owner, Restoration weapon-target validity, rounded life >60, and at least one of bytes `+0x116,+0x11a,+0x118,+0x117,+0x124,+0x126` set. A harmful-effect timer, rather than low life alone, drives this choice. |
| Hallucination | Same owner; area/tech helper `0x434130` with tech 20 must pass; exact unit IDs are Carrier 72, Scout 70, Arbiter 71, or Archon 68. |
| Feedback | Spellcaster DAT bit `0x200000`, no status `0x40000000`, no grounded-building status bit 2, no Hero property; current energy high byte must be >= rounded target life. This is a predicted lethal-damage criterion. |
| Optical Flare | No Building DAT property and no existing blindness byte `+0x123`; detector predicate `0x431a90` qualifies immediately, otherwise rounded life must be >80. |
| Mind Control | Reject Hero property. Loaded Shuttle/Dropship qualifies. Exact preferred IDs `5,9,12,30,34,39,42,45,46,62,63,67,71,72,83,103` qualify; other IDs require both mineral and gas DAT costs to be >= the Dark Templar's respective costs. The checks are two independent thresholds, not combined price. |
| Recall | Same owner, current attack target present, exact Carrier/Gantrithor/Reaver/Warbringer IDs, and shield integer part <=10. |
| Parasite | Reject existing parasite state as controlled by campaign flag and caster-player bit. Without campaign flag, candidates need detector/transport/flying-building/worker predicates or life >=300; all then pass weapon `0x38` target validity. |

Area spells use concrete score arithmetic. Storm, Plague, Stasis, Ensnare and Maelstrom evaluate a **96-pixel** neighborhood. Ordinary enemy contribution at `0x432090` is rounded HP+enabled shields, except Plague weapon `0x3c`, which counts rounded HP only. Allied contribution at `0x432100` is rounded HP+enabled shields for valid non-enemies. The final inequality is **enemy_score > 2*allied_score**, with equality rejected. Several nonreactive score paths additionally require enemy_score >=250. Maelstrom's enemy score at `0x4321e0` additionally requires Organic and no existing maelstrom; its callback excludes Larva/Egg/Cocoon/LurkerEgg and accepts a moving loaded Overlord before ordinary neighborhood evaluation. Stasis requires a nonbuilding currently attacking an allied regular-player unit; Ensnare (`0x432ac0`) rejects an existing ensnare `+0x116` and **requires** status `0x20000` at `0x432af6`, that is, a currently mobile target (not a building), whose current target is a regular-player unit that is not the caster's enemy. Corrected after the function atlas ([V01 item 1](atlas/v01.md)); the earlier text read the bit as `IsBuilding`.

EMP first evaluates candidates with shields: a 160-pixel neighborhood must contribute >=200 shield points. Only nonreactive calls fall back to an energy score >=200; that score counts eligible enemy spellcasters' energy high bytes. Nuke evaluates a 192-pixel neighborhood and requires a score >=800, with `0x432150` adding the rounded life of workers and of units **without** status `0x20000` (grounded buildings and other immobile units; mobile nonworkers contribute nothing), and hard 800 assignments for certain important building predicates/IDs. Corrected after the function atlas ([V01 item 1](atlas/v01.md)); the earlier "nonbuilding" reading inverted the status bit. Those special predicates still require their own semantic recovery.

There is a material shared-state detail: `0x665d30` is read by several score callbacks. Broodlings, Irradiate, Storm and Maelstrom wrappers explicitly write their supplied reactive flag to it; Plague, Stasis and Ensnare wrappers in the reviewed code do **not** do so before their searches. Consequently the callback's threshold gate must not be modeled as automatically reading the current selector's parameter. Preserve this shared input until all writers and call ordering have been accounted for. The static evidence records the actual writes and reads; a complete running-game claim about when stale values occur would require a cross-caster trace.

Dark Swarm's callback requires an enemy attacking an allied regular-player target; that allied target has no subunit, is grounded, and has ground-weapon behavior 5. It examines the enemy's real attacking subunit and accepts behavior ordinals `0,1,2,4,7` subject to `0x41fd10`; `3,5,6` reject. After selection, an airborne enemy is replaced by its current attack target, so the resulting cast destination can be the allied unit rather than the searched enemy. Full behavior names and the `0x41fd10` predicate must be independently resolved.

## Regional scheduler and state layout

The regional scheduler is **`0x4b0d70`**. Its caller `0x48dd90` runs once per accepted unpaused logical object step, after the AIScript scheduler and before resource-area/slow-AI work. This cadence is established independently in [native frame research](native-frame.md) and its [caller witnesses](evidence/native-economy-frame-static.json).

Each invocation performs exactly **333 slot visits**, including skipped visits. Persistent cursors are player `0x6b1a94` and region `0x6b1a98`. A visit calls `0x4b0ce0(player,region)` only when `region < actual_region_count` and player type is 1. The actual count comes from the word pointed to by `0x69b584`. After each visit the region cursor increments; reaching **2500** resets it to **1**, increments the player, and wraps player 8 to zero. At a boundary it also calls `0x4abf90`, and, for a computer player without flag `0x20`, `0x4aa8f0`.

This is a fixed-slot sweep, not a scan of every real region every game step. Under an unchanged steady cursor, eight players times 2499 slots form 19992 visits, distributed in 333-visit steps. A complete cycle therefore takes roughly sixty object steps; exact revisit steps alternate with cursor position. It is not valid to update every region every frame or to skip unused slots without preserving this clock.

`StepRegion`, **`0x4b0ce0`**, obtains `regions[player] + region_id*0x34` from `0x6b1a74`. It clears flags bit 4 at `+8`, decrements nonzero byte `+7`, and dispatches state byte `+5`:

| States | Native handler |
|---|---|
| 0 | `0x4ac9b0` |
| 1,2 | `0x4ac7c0` |
| 3 | `0x4aca00` |
| 4,5 | `0x4ac740` |
| 6 | `0x4ac690` |
| 7 | `0x4acbc0` |
| 8,9 | `0x4ac870` |
| >9 | Return |

The matched layout agrees with [AISE AiRegion](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781): ID `+0`, destination ID `+2`, owner `+4`, state `+5`, defense-priority addition `+6`, counters `+7/+9`, flags `+8`, ground-unit count `+0xa`, needed ground/air strengths `+0xc/+0xe`, local ground/air strengths `+0x10/+0x12`, all assigned ground/air strengths `+0x14/+0x16`, **enemy air/ground** strengths `+0x18/+0x1a`, air/ground targets `+0x1c/+0x20`, slowest military unit `+0x24`, detector `+0x28`, and military list head `+0x30`. The reversed enemy-field ordering is intentional in this schema and corroborated by local accesses.

## Regional strengths and attack-group transitions

Strength update **`0x4adbd0`** zeroes dwords `+0x10/+0x14`, clearing the four local/all uint16 strengths. It traverses the region's military list, obtains each unit at node `+0xc`, and skips units with `InBuilding` status. Unit cached **air** strength is `+0x138`; cached **ground** strength is `+0x13a`. The original leaf readers `0x4a9a10/0x4a9a20` corroborate these offsets.

It adds cached ground to all-ground `+0x14` and cached air to all-air `+0x16`; the same contributions go into local fields only when the unit's current region matches the region ID. These are **uint16 additions**, including wrap, rather than unbounded integer sums. With campaign flag `0x20`, it also searches 256 pixels around the region center using callback `0x4aa2c0`; the exact additional contributor filter is still required.

For each enemy strength, needed strength is raised to at least that enemy value. Without campaign flag `0x20`, the compared requirement is `enemy + floor(enemy/2)`. A nonzero cap from `0x482fd0` limits both needs. The field writes preserve uint16 width.

Target update **`0x4adcf0`** fills air/ground target pointers using `0x4a9800/0x4a9820`. For assigned military units it verifies AI type 4 and AI/unit back-reference, rejects Building DAT property, status bits `0x3000`, current targets already matching either region target, AttackMove/AiAttackMove/HarassMove orders `0x0e/0x9d/0x9e` (labels of `0x9d`/`0x9e` corrected after the function atlas, [P07](atlas/p07.md)), and transport predicate `0x4381f0`. If the unit can attack a region target, it changes that unit to ComputerAI through `0x4ada90`. The two target-provider bodies remain a required part of enemy-region evaluation.

States 8/9 at **`0x4ac870`** run strength update, then target update. If flags bit `0x20` is set, abandonment helper `0x4aa9b0` can end processing. It then computes three grouping/reachability outputs through `0x4aaef0`. The native branch reaching attack-target selection changes the current region to state zero, calls **`0x4aba70`**, obtains the chosen unit's region, changes that destination to state **9**, and transfers assigned military with `0x4b15d0` using arguments `(0,0,1)`. A null selection calls `0x4b1500`. Other branches examine attack-roster availability, cancellation and existing destination state; their helper predicates cannot be inferred from the presence of this sequence alone.

`0x4aba70` walks the active unit chain, rejects dying and non-enemy units, considers target-region state, reachability, special unit IDs and marker `0x1ffd`, and compares distance to the region center. It has a grounded-building preference and fallback candidate behavior. The branch structure is retained in the evidence, but a complete destination-selection specification needs the called reachability and region-eligibility predicates.

State changer **`0x4ab900`** returns for region ID zero. When leaving old state zero it clears strength/target/slowest/detector/enemy fields, initializes needed strengths through player/reachability helpers, and usually clears the destination unless the new state is 7. It then clears flags bit `0x20`, writes the state and assigns defense-priority byte `+6`:

| New state | Native priority / side effect |
|---|---|
| 0 | 10; 40 when flags `0x40` and campaign flag absent |
| 1,2 | 70 with campaign flag; 160 without |
| 3 | Clear flags bit 1, then priority 10 |
| 4 | 40 |
| 5 | 50 |
| 6 | 100 or 110 from `0x4861a0` |
| 7 | Clear `+9`; priority 110 if destination passes `0x4861a0`, otherwise 100/110 using current region |
| 8,9 | 50 with campaign flag; 160 without |
| >9 | Priority 10 |

The campaign arithmetic is byte arithmetic: expressions such as `0xa0+0x92` wrap to 50. These numbers are priorities, not durations. The complete state-1..7 actions and meaning of all region flags still need native recovery.

## Native unit-type strength formula

The type-strength routine **`0x4a9000`** uses raw DAT max HP shifted right by eight plus enabled max shields. Let that integer be `H`, weapon damage `D`, cooldown `T`, damage factor `F`, and maximum range `R`. For a real weapon and ordinary valid DAT inputs:

```text
A = low32(floor(R / T) * F * D)
B = trunc_signed32(low32(D * F * 2048) / T)
E = low32((low32(B * H) logical_shift_right 8) + A)
S = trunc(sqrt(uint32(E)) * 758.0 / 100.0)
```

`H==0` returns zero. Native multiplication, signed division and shifting order matter. The multiplication by HP wraps **before** the right shift. The uint32-to-double conversion explicitly adds `4294967296.0` when the signed conversion was negative. The square-root helper has an actual `sqrtsd` normal branch at `0x4d48eb`; constants at `0x4f0598/0x4f0590` are **758.0 and 100.0**. The finite double-to-integer helper truncates. Six isolated original-function probes execute this formula and real numeric helpers, including a case with integer multiplication wrap; x87 control `0x37f` and MXCSR `0x1f80` are explicit in the fixture.

This matches the broad formula described by [OpenBW strength calculation](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L20828), but native 1.17 actually uses double/SSE2 here. OpenBW's integer square-root approximation must not be assumed equal at every rounding boundary. A portable remake can precompute scores for the exact loaded DAT values and retain exact uint32 intermediate semantics; replacing the two floating operations by a single multiplication by 7.58 should first be checked at boundaries.

Modifier **`0x4a8f60`** applies to the calculated score:

| Unit IDs | Modification |
|---|---|
| SCV/Drone/Probe `7,41,64` | Unsigned shift right 2 |
| Spider Mine/Interceptor/Scarab `13,73,85` | Zero |
| Firebat/Mutalisk/Zealot `32,43,65` | Double, uint32 arithmetic |
| Scourge/Infested Terran `47,50` | Unsigned shift right 4 |
| Reaver `83` | Unsigned divide by 10 |
| Others | Unmodified |

Initializer **`0x4a98e0`** processes all 228 unit types. Larva/Egg/Cocoon/LurkerEgg get zero. Weapon columns come from a real subunit if present, from Interceptor for Carrier/Gantrithor, and from Scarab for Reaver/Warbringer; **HP and modifier still belong to the original unit type**. A missing weapon initially contributes **1**. If one direction's score is 1 and the other is greater than 1, that 1 becomes zero; otherwise unarmed types can retain `[1,1]`. It writes uint32 air/ground tables at `0x6ac530/0x6ac8c0`. One isolated invocation executes the complete 228-type loop using explicit DAT fixtures and verifies zero egg types, unarmed `[1,1]`, Marine direction suppression, and Carrier-versus-Interceptor alias behavior.

The dynamic process that scales/caches these type strengths into individual units, including damage, shields, status and bunker occupancy, is not completed by this type initializer. Its unit fields must not be treated as constant DAT values in the regional algorithm.

## Reproduction and completion boundary

Read-only static extraction:

```sh
python3 tools/research/inspect_native_combat_ai_117.py \
  --exe /path/to/the-reviewed/StarCraft.exe \
  --out docs/research/evidence/native-combat-ai-static.json
```

Default probe mode prints the plan without executing original instructions. The authorized isolated execution used:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_combat_ai_117.py \
  --exe /path/to/the-reviewed/StarCraft.exe \
  --out docs/research/evidence/native-combat-ai-probes.json \
  --execute-original-code
```

Both scripts reject an unknown EXE hash. Probe evidence records the executable hash, runner hash, engine/version, inputs, outputs, expected results, instruction counts and visited code ranges. The reviewed instruction limit is 10000 per ordinary invocation and 50000 for the bounded 228-type initializer. Static extraction excludes embedded spell jump-table data from instruction blocks and records those tables separately.

| Established item | Evidence | Next discriminating original probe / recovery |
|---|---|---|
| Rank 3/4, workers, completion, retaliation/Web | Native static + isolated execution | Loaded bunker with two different occupant orders; verify first valid cargo slot and inherited rank |
| Scourge rounded-life maximum, normal nearest, ties | Native static + isolated execution | Complete scan with seventeen same-rank candidates; move equal-position candidates to expose spatial insertion order |
| Seek upgrades and AI raw sight maximum | Native static + isolated execution/prefix | Full acquisition with blind AI, cloaked Ghost, bunker and dynamic sight upgrades; separate collection from attackability |
| Sixteen-entry cap and RNG source/modulo branch | Native static | Execute the complete collection with controlled native spatial arrays; compare final buckets, return and RNG counters |
| Guard sequence/order ordinals and main-order byte throttle | Native static | Whole unit-step interval trace, including timer redistribution, death/removal and order interruption |
| Spell chains, windows, energy and reservation branch | Native static; energy/cost/existing-order paths executed | Finish weapon-target validity, tech availability, area helper and exact different-order issuance; cross-caster shared-score trace |
| Regional 333-slot budget and caller cadence | Native static with independent caller | Execute region states with native military lists/reachability inputs; verify cursor rollover and all state transitions |
| Region strength sums, target-update gates and state-8/9 chain | Native static | Recover/execute dynamic unit strength, enemy-region target providers, abandonment, grouping and transfer helpers |
| Type-strength arithmetic/modifiers/228-type initialization | Native static + isolated execution | Load exact campaign DAT columns and exhaust their finite combinations/rounding boundaries; validate cache refresh timing |

This provides implementable native specifications for acquisition ranking/selection, seek arithmetic, guard sequencing, spell dispatch and several military-state mechanisms. It does not claim complete military AI, all spell helpers, full unit-update ordering or campaign parity. The remaining rows are concrete algorithms and discriminating tests, not uncertainty inferred merely from source disagreement.
