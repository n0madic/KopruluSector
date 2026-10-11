# Native 1.17 reaction spell families and positive order consumers

**145 independent complete original-code calls pass.** This batch closes selected positive decisions for Nuke, Recall, Dark Swarm and the other ordinary reaction casters through actual target reservation and primary order issuance. It also executes the campaign Queen's original RNG branch and the positive existing-Dark-Swarm spatial query. The spell orders' subsequent movement, energy debit and effects are separate consumers.

Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. Evidence: [26 new witnesses / 767 instructions](evidence/native-spell-families-static.json), [145 complete calls](evidence/native-spell-families-probes.json), and [inspector/probe](../../tools/research/inspect_native_spell_families_117.py). Every case returns through original instructions; there are no callable helper replacements, prefixes, full-game/WinMain execution, Windows APIs or guest host-file/network calls. The per-call limit is100000 instructions; the largest final call executes7606.

Fixtures provide declared private CUnit/sprite records, spatial intervals, terrain/region inputs, order pools, availability/researched bytes, prerequisites, diplomacy and properties. Unchanged effective retail DAT fields supply weapon flags, order-to-technology mappings, energy costs and primary-order flags. Original packed technology recipes, their lazy initializer, evaluator and all reached helper instructions execute. These are discriminating input states, rather than captured campaign snapshots. The prior [reaction-spell permission report](native-reaction-spells.md) defines weapon/technology gates, readiness, shared scores and reservation contracts; [primary orders](native-primary-orders.md) defines the actual allocator and activation behavior.

## Nuke readiness, timing and neighborhood score

`0x436f60`, ECX=caster and stack`[skip_busy,caster]`, walks the caster owner's original unit list through`4256f0`. It looks for ID108 with nonzero ready DWORD`+d4`. Its missile pointer is`+d0`; missile`+80` may be null, the requesting caster, or a parent whose type is108. When `skip_busy` is nonzero, an unrelated parent of another type rejects that silo. Zero ignores this busy-parent exclusion. Six complete scans exercise readiness and attachment exceptions.

`0x4832a0`, ECX=player, compares unsigned current tick`5349d8` with the wrapped sum of PlayerAI last-Nuke tick`+214` and rate byte`+212` multiplied by60. Equality passes. Rate0 still requires current tick≥last tick. Eight calls include equality and uint32 wrap. Successful full Nuke selection invokes`483270` to store the current tick; this is distinct from spell energy/cooldown.

Area callback`432150`, ECX=candidate and EDX=caster, rejects untargetable status`04000000` and non-enemies. Worker property8, or absence of status bit`20000`, permits rounded life contribution, including enabled shields. For the building-property path with status`20000`, an active detector or exact IDs103/146 **assigns800 outright** to shared score`665d28`. The operation is neither addition nor maximum. Detector helper`431a90` requires DAT Detector property`8000`, Completed status, no status`400`, and zero Lockdown`+117`, Stasis`+119`, Maelstrom`+124` and Blind`+123` bytes.

The ID103/Building combination is explicitly synthetic: the executable contains that exact ID comparison, but supplying a Building property for103 does not establish ordinary shipped reachability. ID146 and the detector branch provide separate native witnesses. Status`20000` is kept separate from DAT Building property1; assigning a building interpretation to every use of that status bit would be incorrect.

Nuke callback`4335b0` evaluates a192-pixel neighborhood and accepts score≥800. It includes the cloak/detection gate, but does not require the sprite visibility byte used by most other reaction callbacks. Two complete neighborhoods prove enumeration sensitivity: a1000-life nonbuilding before the important building leaves score800; placing it afterward gives1800. Preserve native enumeration order when porting this score.

Complete Ghost1 decisions execute`48d3a0→4832a0→436f60→432f40→48e5c0→42a380`, then set the missile's parent to the Ghost and update the last-Nuke tick. Enemy life799/800, silo readiness, tick59/60 and reservation bit1 have distinct results. Nuke has no ordinary `48e580` energy-readiness gate: a zero-energy fixture still selects and issues126. Visibility byte0 also passes the selected path.

The Nuke branch lacks the usual dispatcher same-order/same-target early return. If order126 already exists, reservation can return1 while retaining its old target and leaving the new target's bit clear; missile-parent and timestamp updates still occur. An empty real order pool likewise reports1, reserves the target and updates those fields while no126 becomes active. Selection success is not an order-allocation success API.

## Recall consumes real regional permission

Recall selector`433140` gets the caster's sprite region through actual`481ba0→481b10`. Gate`4aea70`, ECX=player and EDX=region ID, rejects region state3, a nonnull air-target pointer`+1c`, ground-target pointer`+20`, or flags bit`20` at`+8`. States0/5/8 with those blockers absent pass.

Own-unit callback`433c10` requires the same owner, a nonnull current attack-target pointer and exact IDs72/82/83/81. Shields are compared after clearing the fractional low byte: `(raw_shields & ffffff00) ≤ 10×256`; ten shields plus255 fractional units passes, eleven shields fails. Five complete Arbiter71 decisions then execute actual Recall technology21 permission, reservation and order137. State3, eleven shields, unresearched Recall or an existing bit1 blocks these fixtures. Stasis22 availability is explicitly cleared to isolate this fallback.

## Dark Swarm's selection and genuine existing-field query

Dark Swarm selector`4331d0` searches for a visible enemy attacking a regular-owner non-enemy unit. Callback`433e60` requires the protected target to be a ground unit with no subunit pointer; its ground weapon must be valid and have behavior5. The enemy's effective weapon is taken from its actual subunit when present, otherwise the enemy itself. Its ground weapon must be valid and behavior one of **0,1,2,4,7**. The native switch excludes3/5/6. The actual projectile-miss helper`41fd10(enemy,protected_target)` must return0.

If the selected enemy is airborne, the selector returns that enemy's **attack target** as the final spell target; a ground enemy remains the target. Complete Defiler46 fallback decisions issue DarkSwarm119 and reserve bit1 on the returned object. The two positive fixtures prove that the bit follows this redirection.

The miss chain executes real tile flags, elevation and existing-field checks. A tile flag`100000` produces119 and blocks selection. Nine ground-height pairs execute`445690`; lower attacker elevation rejects while equal or higher elevation passes the supplied otherwise-clear terrain. These isolated results describe this helper's eligibility input, rather than a full random projectile-hit decision.

The positive existing-field branch begins when completed neutral ID202 count at`52c0c4[(202×12)+11]` is nonzero. It executes:

```text
41fa00 → 409cc0 → 432070 → 443110 → 4224c0
        → type predicate432410 → release4222b0
```

The protected unit's original bounds are used to query overlapping spatial records. An overlapping type202 yields miss255, preventing both direct selection and the full Defiler reservation/issue path. A nonoverlapping202 or overlapping201 leaves miss0 and permits selection. Query scratch depths`5d6e48/5d6e5c` return to0 after the real nested query.

The completed-neutral count is a guard, not a repeated ownership/completion predicate inside the overlap callback. With that guard supplied positive, an artificial overlapping202 owned by7 and not Completed also yields255: the callback only tests type202. This preserves a real boundary without claiming such an input is a stock campaign snapshot. [GPTP's `isUnderDarkSwarm`](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/api.cpp#L137) and its [type-only callback](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/api.cpp#L13) corroborate this query shape; the local1.17 execution supplies the count guard and miss-return contract.

## Other complete ordinary-caster decisions

Every positive row below executes full`48d3a0`, actual prerequisite/readiness helpers, original spatial callbacks, reservation and real primary-order allocation/activation. Inputs isolate fallbacks by clearing declared availability bytes. Negative boundaries also return completely.

| Caster / spell / issued order | Distinct measured input and boundary |
|---|---|
| Battlecruiser12 / Yamato113 | Ordinary nonhero target rounded life **200..450 inclusive**;199/451 fail. Armed-building and loaded-Bunker special predicates remain additional branches. |
| Ghost1 / Lockdown115 | Mechanical Carrier72 positive; existing Lockdown byte or missing research blocks. Failed fixtures genuinely scan an empty silo list afterward. |
| Queen45 / Broodlings121 | Noncampaign energy149 fails,150 passes with alternatives disabled. Original`429ee0→429f60` additionally appends Move6 to the Queen's current sprite position512/512. |
| Queen45 / Parasite120 | Full-energy noncampaign ordinary target life299 fails,300 passes. Reservation uses **bit4**. Existing bit1 survives and becomes5; bit4 blocks. Existing parasite ownership bit128 blocks owner7, unrelated bit1 permits. |
| Queen45 / Ensnare146 | With alternatives unavailable, target status bit`20000`, attack pointer and area score permit selection. Shared threshold0 requires≥250; shared1 permits100. Clearing status`20000` rejects. |
| High Templar67 / Storm142 | Reactive0 enemy249 fails/250 passes; reactive1 permits100. Hallucination availability is cleared for isolated failures. |
| Arbiter71 / Stasis147 | Attacking enemy249/250 boundary with shared flag0; shared flag1 permits100 even with the same reactive argument1. Recall availability is cleared. |
| Dark Archon63 / Feedback184 | Enemy spellcaster rounded life100 requires integer energy≥100;99 fails. Other abilities are unavailable. |
| Dark Archon63 / Mind Control182 | Preferred enemy Battlecruiser12 selected when Feedback is unavailable. |
| Dark Archon63 / Maelstrom186 | Caster energy199 fails/200 passes despite Mind Control availability being0: reserve is `min(Maelstrom cost + MindControl cost, actual maximum energy)`. |
| Medic34 / Restoration180 | Same-owner harmful-timer target life60 fails/61 passes. The failed path invokes actual`482450`, comparing this Medic pointer against PlayerAI`+4d4`. |
| Medic34 / Optical Flare185 | Full-energy ordinary enemy life80 fails/81 passes. Restoration unavailable. |
| Science Vessel9 / Irradiate143 | Actual`438b50` requires Organic, nonbuilding, and excludes IDs35/36/97. A synthetic nonorganic target fails; ordinary organic target succeeds with reactive1. |
| Science Vessel9 / EMP122 | Neighborhood enemy enabled shields199 fails/200 passes. Original score callback`433cd0` sums integer enabled shields of targetable enemies. |
| Corsair60 / Web181 | Ground Turret124 attacking an allied airborne unit selects through real bounding-box range helper`4229a0`. |

Web helper`4229a0` substitutes the caster's subunit when DAT property`10` requires it, computes both original bounding rectangles with right/bottom+1, and compares their approximate gap distance inclusively to the requested limit. Eight horizontal cases use extents8: center delta17 has gap0, delta18 gap1; delta273 has gap256 and passes limit256,274 fails. This is distinct from a center-distance check.

Status`20000` in the Ensnare fixture is explicit. [Teippi calls it `Reacts`](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.h#L39); GPTP's different `IsBuilding` name does not define the native predicate. Existing [unit-creation evidence](native-unit-creation.md) proves that the original air-density helper requires this bit while excluding DAT buildings, so the bit cannot simply be equated with Building property1. This batch measures the bit test directly without asserting all original status initialization writers.

Stasis and Ensnare select their range using the supplied reactive argument, but **do not write shared`665d30`** before their score callback reads it. Storm, Broodlings, Irradiate and Maelstrom selectors do write it. The 249/250/100 compositions confirm the resulting inherited threshold; the prior reaction report separately executes cross-caster Storm→Plague sequences. Keep this shared state in simulation rather than silently converting each caller to an independent reactive parameter.

## Campaign Queen RNG and selected order priority

PlayerAI flags word`+218` bit20 selects the campaign Queen branch. Actual`478200` is called with source42. For enabled RNG it computes wrapped `next = current×015a4e35 + 1`, increments total`673938` and source counter`673940[42]`, stores current seed`67393c`, and returns `(next>>16)&7fff`.

The Queen compares **AL to1**, not the complete15-bit result. Complete campaign calls returning0/2/255 try Broodlings first;1 and257 select Parasite first. All five cases execute the original draw once and verify the live seed and both counters. This is a probabilistic script-independent choice; the supplied seed is a private fixture, not a campaign-save claim.

Outside campaign mode, full-energy high-byte equality with actual maximum energy selects Parasite first; otherwise Broodlings is tried first. Broodlings success queues Move6. The later Ensnare fallback requires campaign mode or that same full-energy equality. The other measured ordered families preserve the dispatcher sequence: Vessel EMP→Matrix→Irradiate, Templar Storm→full-energy Hallucination, Arbiter Stasis→Recall, Defiler Plague→DarkSwarm, Ghost Lockdown→Nuke, Dark Archon Feedback→MindControl→reserved-energy Maelstrom, and Medic Restoration/additional caster path→full-energy OpticalFlare.

All selected issuance cases leave caster energy unchanged. Reservation bit1/4 is not proof that the cast effect ran. Same-order behavior and empty-pool anomalies remain the original contracts described above and in the prior reports.

## Reproduction and remaining effect consumers

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_spell_families_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --weapons-dat /private/tmp/koprulu-campaign-data/arr/weapons.dat \
  --techdata-dat /private/tmp/koprulu-campaign-data/arr/techdata.dat \
  --out docs/research/evidence/native-spell-families-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-spell-families-probes.json
```

Default mode only validates/disassembles and decodes data. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4, x86_64 Darwin/Rosetta. Script SHA-256:`7eef9e8514bbd0538a339bc0c77e665a229fb96c94be631490d437f541e6c002`. Static/probe JSON records exact dependency and unchanged DAT hashes. [Teippi's reaction wrapper](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/ai.cpp#L1360) corroborates invocation of the original reaction decision; it does not replace this exact-version oracle.

Remaining dependencies are concrete: later126/137/119 and other spell-order state machines; effect creation, expiration and reservation-mask release; successful cargo-dependent callback branches; armed-building Yamato exceptions and other preferred-ID sets; original status-writer cadence; complete-game spatial/visibility/timer interleavings. The selected positive chains above close readiness, targeting and issuance for their declared inputs. They do not close those downstream effects or establish full campaign parity.
