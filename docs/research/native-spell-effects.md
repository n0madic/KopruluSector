# Native 1.17 selected spell orders, effects and expiry

**73 complete original-code calls pass**, closing selected Matrix casting/application/expiry, Irradiate impact/application/damage/expiry, EMP impact/area application, Recall's initial debit/wait and empty completion, and the shared spell-order completion handshake. Dark Swarm's field-creation routine is recovered statically here; its successful creation and expiration remain separate dependencies.

Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase`400000`. Evidence: [27 new witnesses /1424 instructions](evidence/native-spell-effects-static.json), [73 complete calls](evidence/native-spell-effects-probes.json), and [bounded inspector/probe](../../tools/research/inspect_native_spell_effects_117.py). No callable helper replacements, prefixes, game/WinMain execution, Windows imports or guest host-file/network calls occur. Each call is limited to100000 instructions; the largest final call executes1683.

Private fixtures provide CUnit/sprite/image records, spatial intervals, order pools, visibility, map bounds and controlled unit properties. Weapon flags, order-to-technology/weapon/animation mappings, energy costs, weapon explosion types and the selected effect's damage/cooldown/radius are unchanged effective retail DAT fields. Original code supplies allocation, permission, targeting, range, damage and order resumption. The **image pool and lone-sprite pool are genuinely empty**, and the original sound-backend guard sees0. Effect-state writes still execute; successful overlay/effect-sprite rendering is not certified by those branches.

[Spell selection](native-spell-families.md) and [reaction permission](native-reaction-spells.md) establish the preceding decision/issuance layer. This report crosses into selected consuming routines without treating a direct handler call as a complete caster movement or campaign run.

## Native order routes and the completion signal

Static decoding of the actual `439420` main-order dispatch tables gives:

| Order | Consuming handler |
|---|---|
| Matrix141 | `438560` |
| Recall137 | `42fc40` |
| DarkSwarm119 / EMP122 / Irradiate143 | Generic spell order`434460` |

The static JSON records the native mapping byte, wrapper pointer and original tail jump for each selected order. The complete handler calls begin directly at those destinations, after declared fixture state has been installed.

For generic spell order state2, signal byte`+4f` bit2 is the completion handshake. When set, `434460` clears that bit and invokes actual`42a5a0` to resume the next/idle order. With the supplied nonnull AI pointer and free order pool, the result is ComputerAI156. With no completion signal and a nonnull target, the handler copies the target's sprite position to the order-target position and updates the attack waypoint through`40cbd0`. A null target leaves the position alone. Nine complete calls cover these three branches for119/122/143, with energy unchanged. This state2 work neither applies an effect nor debits energy a second time.

The initial generic spell-state path also has a static witness, but its complete movement/weapon-animation composition is outside these nine state2 calls. Irradiate and EMP effect acceptance is closed separately through actual bullet impact below.

## Energy debit is an unchecked word operation

`435300`, ECX=unit and stack`[raw_cost]`, tests The Gathering cheat bit`800` in`5432fc`. With that bit clear it truncates the argument to uint16 and subtracts it from energy WORD`unit+a2`, wrapping modulo65536. The helper itself does not check affordability. With the bit set it does not subtract.

Six complete calls include exact depletion, ordinary subtraction,0−1 wrap,255−256 wrap and an argument`10001` truncated to1. Affordability belongs to the caller's gate, so a safe remake must reproduce both layers rather than inserting a saturation check into this native helper.

## Matrix full casting and application

Complete `438560` calls execute the original sight helper`423150`, technology/target/range gate`434b80`, actual target permission`434130`, target visibility`422d10`, range`421120→4229a0`, movement-stop/clamp, debit`435300`, application`438900`, original empty-image allocation branches, sound-backend guard and primary order resumption.

The positive fixture uses sight byte10, visible nonbuilding target128 pixels away, a declared stationary/facing-compatible movement state and a real20-node order pool. It consumes100 energy from200, sets Matrix capacity250×256 and timer168, then resumes ComputerAI156. The Gathering suppresses debit while retaining application and resumption.

Eight additional complete order calls distinguish energy **one raw unit below100×256**, exactly equal and one above; null target; Building DAT property; Stasis byte; existing Matrix and exhausted order pool. Insufficient energy or invalid target leaves energy/effect unchanged and resumes the ordinary next order. Recasting an existing Matrix resets it to full capacity/duration. With no free order nodes, the effect and debit still occur, but resumption allocation fails and current order141 remains. This is an execution-side acceptance boundary, distinct from the earlier AI reservation anomaly.

Direct application`438900` sets capacity WORD`+112` to`fa00` and duration byte`+114` to`a8`, irrespective of prior capacity/duration. When the old timer was0 and the unit is not burrowed, it chooses size through`428d90`, requests front overlay`173+size` via`422ae0→415260`, then back overlay`176+size` via`4152d0`. A nonzero old timer or burrow bit suppresses those initial overlay requests while retaining the full reset. Six complete calls cover those paths and the two size-property bits. Original allocation failure is handled inside each creation helper.

Timer service`4351e0` decrements nonzero Matrix duration. Only the transition1→0 invokes actual`4384c0` with current Matrix capacity, causing capacity0 and real overlay removal searches. Timer2 leaves capacity intact and becomes1;168 becomes167. Timer0 does not clear a deliberately supplied stale capacity. Six complete calls cover zero, partial duration and exact exhaustion. Existing populated overlay release is not exercised by the absent-overlay fixtures; the real searches and their no-match returns are.

The native contracts agree with the operation shapes in [GPTP's Matrix hooks](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/spells/defense_matrix.cpp#L17). Local1.17 instruction execution defines the addresses and values. [Damage recovery](native-damage.md) separately closes capacity depletion by incoming damage.

## Irradiate impact, attribution, damage and final tick

The original bullet-hit dispatcher`4090e0` reads the stock weapon explosion byte. Weapon34 maps to explosion9, whose native branch rejects a null or dying target, requests sound through the real disabled-backend guard, then calls`438960(target,[source,owner])`. The dying test`41fe40` returns true for a missing sprite or primary order0; it does not inspect the supplied HP value. Three complete impact calls cover ordinary, null and dying targets.

Application`438960` resets timer byte`+118` to unchanged weapons.dat cooldown34, **75**, replaces source pointer`+11c` and writes the low byte of owner argument to`+120`. It requests overlay`17c+size` only when the previous timer was0 and the unit is not burrowed. Five direct calls exercise existing durations, burrow and owner-argument truncation. Timer replacement is not a maximum operation.

Cleanup`438620` searches/removes overlay IDs`17c..17e` through actual unit/subunit search`422e70`, then stores timer0, source pointer0 and owner sentinel **8**. Four complete calls cover different initial durations. An absent overlay does not prevent state cleanup.

The periodic effect service performs this sequence for nonzero Irradiate duration:

```text
4351e0: decrement +118
        → 4385d0 damage dispatch
        → if +118 is now0: 438620 cleanup
```

Thus **the final1→0 tick still damages before attribution is cleared**. Complete timer0/1/2/75 calls use an organic burrowed subject, for which damage dispatch invokes the actual callback on itself. Callback`438650` requires Organic property, excludes Building and IDs35/36/97, excludes another burrowed victim, and requires bounding-rectangle distance≤32 unless the irradiated subject is in transport. It calculates `stock_damage250×256 / stock_cooldown75 =853` raw units using native integer division, then invokes actual`41dfc0`. The declared independent-damage fixture loses853 raw HP; final tick1 then clears source and owner to0/8. Timer0 neither damages nor clears deliberately supplied stale attribution.

Five complete normal-area calls execute native160-pixel candidate enumeration and the32-pixel callback check. A nonorganic irradiated subject can still damage an organic neighbor. A burrowed neighbor is excluded while an organic subject can damage itself. With zero bounding extents, horizontal center delta33 has rectangle gap32 and passes;34 fails. Both organic subject and qualifying neighbor receive their own actual damage call. A separate final-tick InTransport fixture with null carrier causes no damage, but still performs cleanup. Positive populated-carrier enumeration remains open.

These direct service calls establish **per-invocation** behavior. The active-unit caller's cadence is a separate simulation composition. [Teippi's Irradiate effect code](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/tech.cpp#L186) corroborates organic/burrow/range/division predicates; its deferred damage buffers do not replace the original1.17 direct damage calls.

## EMP applies regardless of target owner

Weapon33 maps to stock explosion8. A complete original bullet-hit call reaches`433fc0`, using its sprite position as the area center. Direct `433fc0`, ECX=x, EDX=y and stack`[source]`, uses stock inner splash radius **64**, clips its rectangle through native map bounds and obtains actual spatial candidates through`422330`.

Each candidate except the source and its subunit is affected regardless of owner. A Stasis byte prevents clearing. Ordinary candidates have energy WORD and shields DWORD both set to0; a selected sprite triggers actual image palette11 redraw`416bb0`. Hallucinated candidates take removal`42a4d0` instead; that populated-removal branch is statically recovered but not part of the six ordinary area cases here. The actual candidate scratch stack is released through`4222b0` and returns to depth0.

The six direct calls vary source/null/source-as-target/subunit, Stasis and selected-image state. Enemy-owner0 energy123×256/shields45×256 become0/0. Source/subunit and Stasis cases retain both. Selected image flags become5 through the original redraw helper. [GPTP's EMP effect reconstruction](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/weapons/wpnspellhit.cpp#L325) corroborates the exceptions and owner-independent clearing; the local1.17 tests supply runtime proof.

## Recall start/wait and Dark Swarm's remaining creator

Recall handler`42fc40` treats state1 as waiting/completion. A nonzero order timer`+54` returns without decrementing it. At zero it enumerates the original64-pixel target area with callback`4302c0`, sets shared recalled-something flag`665b68` to0 before enumeration, then resumes the order. Three complete calls use a genuinely empty target-area candidate set, proving waiting versus empty completion and scratch release.

For a state other than1, the handler compares stock Recall energy cost150×256, debits it through`435300`, copies a nonnull target's sprite position when present, requests Recall lone sprite`17b` through`417b80`, calls the original RNG getter with source17 for sound choice (the explicitly disabled fixture RNG returns0), then sets timer22 and state1. Four complete calls include start states0/2 and waiting states. The real empty lone-sprite pool returns0; the handler still consumes energy and enters the wait. Successful candidate teleportation through`4304d0` and successful Recall sprite allocation remain open. [GPTP's Recall order reconstruction](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/spells/recall_spell.cpp#L117) is corroboration, not the local execution oracle.

Dark Swarm creator`43dc10` is **static evidence only** in this batch. It clamps a point for type202, calls original`424900` with neutral owner11, and on success sets NoCollision status`200000` and sprite elevation11, completes through`424da0`, then sets timed life`+110` to900 when zero or larger, preserving a smaller nonzero lifetime. Failure calls notification`425020` with the supplied caster owner. Successful field creation, original timed-life countdown and expiry removal are not inferred from the completed119 state2 handshake or the earlier [existing-field overlap query](native-spell-families.md).

## Reproduction and remaining compositions

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_spell_effects_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --weapons-dat /private/tmp/koprulu-campaign-data/arr/weapons.dat \
  --techdata-dat /private/tmp/koprulu-campaign-data/arr/techdata.dat \
  --out docs/research/evidence/native-spell-effects-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-spell-effects-probes.json
```

Default mode only validates/disassembles/decodes. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4, x86_64 Darwin/Rosetta. Script SHA-256:`9abe92e59f3ba1420d600091aa4ad48455c0e16712b5e85ec19bf1a37734afc9`. Both JSON files record dependency script and unchanged DAT hashes.

Remaining concrete work: positive Recall relocation/placement and field sprite allocation; positive DarkSwarm creation/timed-life expiry; populated Matrix/Irradiate overlays and their release; generic spell movement/animation→projectile composition; positive Irradiate cargo enumeration; EMP hallucination removal; reservation-mask release and full active-unit timing/interleavings. The completed calls close the selected direct contracts above, while whole-cast/campaign parity remains a separate acceptance condition.
