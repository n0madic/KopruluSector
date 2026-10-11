# Native 1.17 bounce, splash, line damage and Reaver/Scarab lifecycle

This batch executes populated stock Mutalisk, Firebat, Archon, Siege Tank and Lurker shots against multiple original-created targets. It also connects paid Scarab production to hangar selection, launch, actual weapon82 delivery, damage, Scarab Death and pending-CUnit recycling. It extends [projectiles](native-projectiles.md), [computer combat](native-computer-combat.md), [damage](native-damage.md), [training](native-training-lifecycle.md), [image initialization](native-image-initialization.md) and [Death](native-unit-death.md). Their published scripts and generated evidence remain unchanged.

The executable is **StarCraft 1.17.0.1**, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred virtual addresses with image base `0x400000`; subtract that base for RVAs. Earlier-version sources supply structural clues; the local addresses and observations come from this exact binary.

## Evidence and execution boundary

- [Static extractor](../../tools/research/inspect_native_weapon_effects_117.py), SHA256 `5d007625bd9872e9285a3e43ab408834c2ee8f50c762f5d8a762c452f3dc5088`, and [static evidence](evidence/native-weapon-effects-static.json): **20 ranges / 1,341 decoded instructions**, no undecoded tails. The named `iscript_attkshiftproj_handler_fragment` is a switch-handler fragment, not a separately callable complete function.
- [Original-instruction probe](../../tools/research/probe_native_weapon_effects_117.py), SHA256 `df879c4c0f510cd33d3ba2905ffdd0f979a206f0a4035b5f0a7906d54fceb3c5`, and [probe evidence](evidence/native-weapon-effects-probes.json): **55 complete passing cases**, **14,415 complete top-level sentinel returns**, **6,835,978 original instructions**, **330 visited function starts**, **769 reviewed ranges** and **60 imported source hashes**, zero prefixes and zero substituted helpers. Caller inputs, VM offsets/operands, Bullet contexts, raw and normalized damage arguments, pools and RNG checks are recorded in the evidence.
- **Separate new asset provenance:** [native-weapon-effects-assets.json](evidence/native-weapon-effects-assets.json), SHA256 `eb315592d34c87ac8f7551f89c982d1bc1c9c5d6c947f9d081157e2f0bced80c`, covering **65 image IDs / 59 distinct GRP paths** and their LO inputs, archive source, byte counts and SHA256. Retail payloads remain in private scratch storage. Earlier asset manifests are unchanged.

| Cases | Complete selected scope |
|---:|---|
| 12 | Mutalisk bounce count, attenuation, spatial iteration, square corners, owner, invulnerability, cloak/detection and an air secondary target. |
| 26 | Firebat/Archon/Siege stock shots, adjacent inner/medium/outer splash boundaries, same-owner/allied targets, burrow, air, invulnerability and lethal attribution. |
| 7 | Lurker line samples, repeated-shot history expiry and the 16-record current-slot capacity. |
| 7 | Paid multi-Scarab production and hangar choice; empty hangar/CUnit failure; complete launch, explosion, Scarab Death and direct pending recycling. |
| 3 | Actual 100-Bullet cap, real sprite exhaustion and mandatory-image failure/rollback. |

Execution uses x86_64 through Rosetta, Unicorn **2.1.4**, pefile **2024.8.26**, and a **250,000-instruction bound per invocation**. Each selected call must return through its sentinel with exact stack cleanup and remain inside reviewed original code. All RNG sources observed in native calls are checked against source counters, total calls and an independent integer implementation of the shared LCG. [PRNG contract](native-prng.md).

Private memory supplies authenticated Units/Flingy/Sprites/Images/Weapons/Orders DAT, IScript, GRP, LO and `rez\stat_txt.tbl`. Native descriptors load all columns; actual `0x425610` initializes acquisition data. Actual `0x424900` creates every source and victim, including genuine Tank body/turret links, and `0x424da0` completes/shows the selected objects. Pools, lists, image chains and spatial indices are maintained by original code.

The terrain/FOW/player arrays are declared fixtures: four same-group regions, no obstacle contours, visible tiles, eight valid native-stride player-region arrays, owners3/4 and explicit alliances. Heading64, order-wait0, selected cooldown readiness, burrow/cloak/invulnerability/HP fields are explicitly supplied where each case says so. Coincident targets in the history-capacity case are native-created objects at fixture coordinates; successful native placement validation is not claimed for that arrangement.

The selected step calls actual source-image `0x418b20`, then positive Bullet-list `0x40a5f0` and Thingy-list `0x418990`. Lurker additionally calls the actual history producer `0x40a880` before the Bullet phase, following its static unit-phase callsite. These are complete original callbacks, **not a complete accepted game frame or continuously moving world**. Init readiness for Archon, Tank and Reaver comes from64 real image/Bullet/Thingy steps. No WinMain, Windows import, renderer, guest host-file/network operation or replacement callee executes.

## Caller contracts

| Entry | ABI and role |
|---|---|
| `0x409830` | ECX=Bullet; complete primary bounce consumer. |
| `0x40a820` | ECX=candidate CUnit; uses globals `0x53db80/84/88` for source/current/previous target; returns candidate eligibility. |
| `0x409590 → 0x4095a0` | ECX=candidate CUnit, EDX=Bullet; ground splash evaluation/damage, callback returns zero. |
| `0x40a9f0` | ECX=Bullet, stack=[radius,candidate]; point-to-inclusive-target-bounds predicate, `ret 8`. |
| `0x409770` | ECX=candidate, EDX=Lurker Bullet; history eligibility and real damage, returns zero. |
| `0x41c630` | ECX=Reaver/Carrier; secondary TrainFighter consumer. Dispatch `0x439310`, secondary order63. |
| `0x41cc80` | ECX=parent, stack=[completed fighter], `ret 4`; hide/register that fighter in the parent's inside hangar. |
| `0x41ccd0` | ECX=parent; choose inside fighter, move/show it and transfer to outside hangar; fighter pointer or zero. |
| `0x41c860 / 0x41c790` | ECX=Reaver; main ReaverAttack59 / launch61 consumers. |
| `0x481cb0` | ECX=unit, stack=[other unit]; same-ground-group predicate. The selected positive path uses genuine same-group fixture inputs. |
| `0x48e9b0` | ECX=unit, DX=target x, stack=[target y]; selected same-region route-distance return, `ret 4`. |
| `0x41c950` | ECX=Scarab; main order65 consumer. |
| `0x41f4d0` | ECX=CUnit, stack=[weapon]; IScript useweapon producer, `ret 4`. |
| `0x41cfe0` | ECX=fighter; detach from inside/outside hangar during actual Death teardown. |

Damage uses real `0x41e290 → 0x41dfc0` and its armor/size/shield/HP/attribution callees. Damage-event evidence preserves raw stack slots and also normalizes byte weapon/direction arguments: callers need not clear the upper24 bits. HP and damage are native **1/256 integers**. No floating display rounding participates in comparisons.

## Mutalisk bounce: iteration order and animation timing

Stock Mutalisk43 fires weapon48: normal9 damage, behavior7, normal hit type1, lifetime60. Actual stock Images38/39 and Bullet511/512/513 run through ordinary attack order10 and real IScript. Observed three-hit losses are **9,3,1**; one target receives9, two targets receive9/3, and a fourth eligible target remains untouched after the third hit.

The bounce consumer follows the nonmiss target, performs actual Flingy movement/map clamping, and tests arrival. It decrements Bullet byte `+0x63` before looking for another target. With a nonzero remaining count and nonnull attributed source `+0x64`, it supplies source, current target `+0x5c` and previous target `+0x68` to the actual square query `0x442b20`, using radius **96** and callback `0x40a820`.

The callback requires real attack legality with detection enabled, checks target status`0x300` against detection bit `1<<source.owner`, requires native hostility and excludes both the current and immediately previous target. It returns the **first acceptable spatial candidate**. It does not select the nearest candidate or draw a random choice.

The discriminating case has first impact at`(280,200)` and candidates`(250,200)` and`(290,200)`: the spatial iteration chooses250 first even though290 is closer. A candidate at`(370,290)` is accepted after280 despite its center being more than96 pixels away in Euclidean distance: preserve the **square query**, rather than adding a circular-distance filter. Candidate400 is outside that tested square and is skipped. Same-owner, invulnerable and undetected-cloaked secondary candidates are skipped; detected cloak and a Wraith secondary are accepted. The detection-positive fixture uses mask8 for source owner3.

Before overwriting the current target with the next target, the consumer forces **SpecialState1 animation13**. Its stock VM damage consequently still applies to the old target, with the already decremented bounce counter. When no next candidate remains, it selects primary state5 and Death animation1. Normal impact `0x4090e0` supplies divisor`3^(2−remainingBounceCount)`, producing1/3/9 divisors in these stock shots. Ordinary bounce state does not use the point-missile lifetime post-decrement; substituting the generic state1 timer would change its contract.

## Ground splash: native bounds, owner filters and fractional damage

| Source | Weapon / behavior / hit type | Base damage/type | Inner / medium / outer |
|---|---|---|---|
| Firebat32 | 25 / 5 / 3 | 8 concussive | 15 / 20 / 25 |
| Archon68 | 70 / 2 / 3 | 30 normal | 3 / 15 / 30 |
| Siege Tank30 + turret31 | 27 / 2 / 2 | 70 explosive | 10 / 25 / 40 |
| Scarab85 | 82 / 5 / 3 | 100 normal | 20 / 40 / 60 |

The exact selected stock rows are retained in the probe JSON. Behavior and hit type are different columns. Ground splash queries the outer square, then evaluates each candidate through real weapon-target eligibility and `0x40a9f0`. This measures the Bullet **Sprite** center against the target's stock rectangle, including native right/bottom`+1`, invokes original approximate distance and accepts `distance<=radius`. Center-to-center distance and floating Euclidean distance are unsuitable replacements.

The callback excludes the attributed source, apart from its separate weapon84 exception. Hit types3/24 exclude candidates with the **same owner byte** unless they are the explicit target. Hit type2 permits same-owner collateral. Accordingly, random own Marine victims survive Firebat/Archon splash but take Siege damage. An allied Marine owned by another player still takes damage under all three weapons despite actual reciprocal alliance bytes. The owner comparison must not be rewritten as an alliance comparison.

Ground mode (`0x541770=1`) applies full inner damage with divisor1. Outside the inner radius, burrowed status`0x10` suppresses damage; otherwise medium/outer divisors are2/4. An inner burrowed victim takes damage. Ground weapon flags reject Wraith splash for Firebat/Siege; Archon can damage it. Invulnerable victims retain HP.

Archon against stock small Marines demonstrates raw losses30/15/7.5. Siege's explosive size modifier yields35/17.5/8.75. Adjacent bounds cases test Archon target-center offsets12/13,24/25,39/40 and Siege offsets19/20,34/35,49/50. The rectangle edge explains why these center offsets are larger than the DAT radii. Fractional results are retained as raw integer HP, not rounded to displayed whole hit points.

Lethal collateral cases invoke actual attribution and issue primary Death0, incrementing the source's kill byte once. The victim is **not immediately physically reclaimed**: its live/completed counters and CUnit remain until the Death consumer runs. This batch does not run those Marine victim Death lifecycles.

### Firebat's persistent live DAT mutation

Raw stock weapon25 forward-offset is20. Its actual attack Image226 executes opcode**49**, `attkshiftproj`, at IScript offsets**28109/28113/28117**, with operands**24/52/80**. Native handler fragment `0x411023` obtains the current ground weapon through `0x41fc20`, writes the operand to live byte **`0x5260b0 + weaponID`**, then calls actual `0x41f5d0` with ground selector1.

`0x420030` reads that live byte when computing the spawn point. With source`(200,200)` and heading64, the three real Bullet25 impacts are **`(224,200)`, `(252,200)`, `(280,200)`**. The baseline victims at224 and offsets y0/20 receive total12/10 damage, while y40 remains untouched. This is not three full hits at one location. Native creation traces record live offsets24/52/80 independently of the unmodified raw DAT input.

After the selected complete shot and40 image/Bullet/Thingy steps, the live weapon byte remains**80**. The handler has no restoration write. Preserve mutable initialized gameplay tables separately from raw resource bytes; a per-shot API that assumes weapon25 remains20 would disagree with this original execution.

### Siege body/turret scope

Native creation produces body30 and child31 with original parent/subunit links and populated Images253/254/255, firing overlay537 and Bullet533. The selected calls issue actual body order100 and **separately issue actual turret order22**. Both main consumers and populated image steps execute, and the Bullet's attributed source is the **body**, not the turret. This establishes selected body/turret firing and attribution; it does not claim automatic UI-command propagation to the child order.

## Lurker line samples and bounded hit history

Stock Lurker103, with explicit burrow readiness, executes main order11, populated Images921/922 and weapon109. The weapon has normal20 damage, behavior9, radius20 and max range192. The creator extends the forward destination to**212 pixels**; genuine Bullet985 moves in ordinary point state1. Its stock VM emits multiple damage samples along the line. It is not the DOT primary state4 used by other weapons.

Actual targets at280/320/380 along the horizontal line each lose20; side target`(340,240)` retains40. Damage-event evidence records both Bullet pixel fields and live **Sprite position**: the sampled geometry follows the Sprite, which must not be replaced with a stale saved impact coordinate.

History contains **32 slots × 16 source/target pairs =512 pairs**, at `0x540768`. Cursor and current used count are `0x541768/0x54176c`. Actual `0x40a880` advances/wraps the cursor, clears128 bytes for that slot and zeroes its used count. The hit callback `0x409770` excludes the source, applies the same-owner/explicit-target exception, real weapon eligibility and the inner-radius bounds test. With a nonnull attributed source, it scans **all512 pairs** and rejects an already recorded source/target combination.

If the current slot has room, the callback records the pair and increments the used count before applying damage. When used count is exactly**16**, it **still damages the target but records no pair**. A genuine17-target coincident group therefore leaves16 targets at20HP while one unrecorded target is hit again and dies; source kill count becomes1. A faithful implementation must retain this overflow behavior rather than enlarging the set or suppressing unrecorded hits.

Repeated real shots use explicit cooldown-readiness reset and gaps24/29/30/31/32 selected history/image steps. Gaps24/29 leave all three line victims at20HP; gaps30/31/32 eventually kill them. History-slot lifetime is32 updates, but a shot emits **later moving samples**: a shot started before old records expire can hit after expiry. The observed30-step transition is not a30-slot table or a universal attack-frequency rule.

## Paid Scarab production, hangar choice and complete selected launch

Actual admission `0x41b4b0` queues type85 and debits its15 mineral cost. Secondary order63, issued through `0x42a1e0`, reaches `0x439310 → 0x41c630 → 0x41bc30 → 0x424900`. The resulting genuine Scarab has parent at **`+0xc0`**, not ordinary current-build fields. Stock build time is105: after allocation, **106 subsequent secondary calls** are needed, because105 countdown decrements precede the following completion call. Actual `0x41cc80` hides/registers the completed child in parent inside-head`+0xc0`, count byte`+0xc8`.

### Hangar choice is not a maximum-HP reduction

Four cases genuinely pay for and train three/four Scarabs, then set the stated HP fixtures in actual inside-list order and call complete original `0x41ccd0`.

| Inside-list HP | Selected index, zero based |
|---|---:|
| 10,18,14 | 2 |
| 10,18,9 | 1 |
| 10,10,9 | 0 |
| 10,18,14,17 | 3 |

The function first finds a completed inside child and stores **that child's HP as a fixed comparison baseline**. Later completed children replace the selected pointer whenever their signed raw HP is strictly greater than that initial value; the baseline is not updated. Thus it selects the **last later completed child above the first eligible child's HP**, or retains the first when none qualifies. It does not choose maximum HP; ties do not replace the first. The separate Interceptor minimum-HP branch is extracted but is not exercised by these Scarab cases.

### Launch, explosion and teardown

Complete ReaverAttack59 activates launch61. Its real target/range chain reaches `0x481cb0` and the same-region return of `0x48e9b0`; the selected route calls real approximate distance and does not invoke connected-region `0x491300`. Disconnected/blocked routes and physical Scarab pursuit are outside this composition.

Actual `0x41ccd0` places the chosen Scarab **25 pixels forward** through native direction arithmetic, relocation and show. It removes the child from inside, inserts it into parent outside-head`+0xc4`, decrements`+0xc8`, increments outside-count`+0xc9` and sets child outside byte`+0xcc=1`. With parent at200 and heading64, the native child starts225; no authored teleport supplies this launch coordinate. The launch consumer issues stock Scarab order65, preserves parent`+0xc0` and sets parent order timer60.

Scarab order65 state0 caps/sets nonzero removal timerWORD`+0x110` to90, sets NoCollision`0x200000`, timer7 and actual chase state2. The close target at235 satisfies real inner-radius/2=10. State2 runs SpecialState1 animation13 and enters6. Genuine stock IScript `useweapon82` calls `0x41f4d0 → 0x408f00`, creating populated Bullet503 with **Reaver as attributed source**. State6 consumes signal1, sets lifetime-expired status and issues real Death0.

The selected40-step continuation runs actual Scarab main/image and Bullet/Thingy callbacks. Native Death calls `0x41cfe0`, removing it from the parent's outside list/count. Weapon82 hits at step6: the first40HP Marine dies from100 full damage, the second dies from50 half damage, and the outer-position third retains40. Reaver kill count becomes2. Bullet recycling follows at step7.

After the genuine Scarab Sprite/Images terminate, it is on the native pending-CUnit list with null Sprite. A **direct complete original `0x4256b0`** returns it to the free pool; allocated CUnit count changes5→4 and parent outside count is0. This establishes selected actual reclamation. It does not run the outer pending producer/full object frame or reclaim the two Marine victims, whose actual Death orders are merely issued.

Empty hangar performs real main59/61 continuation without a Bullet. With a genuinely exhausted two-CUnit pool, admission still pays15, allocation fails through the real error-string getter using authenticated stock TBL, and the retained queue85/current-childNULL/secondary-state1 remains available for retry. No phantom child or projectile is produced. Notification rendering is excluded by the explicit viewer/player fixture; its original string lookup and guard execute without substitution.

## Real finite Bullet/resource failure contracts

Actual `0x40a0e0` builds the100-record Bullet pool. Direct original `0x420030` Mutalisk creation calls fill it with100 populated stock bullets; the101st fails with unchanged allocated count/RNG and no free records. These direct calls deliberately bypass ordinary attack cadence; they establish the finite-pool contract, not100 natural simultaneous unit shots.

A genuine three-Sprite pool is exhausted after source, victim and one Bullet; the next creation fails and retains one active Bullet/99 free records. A genuine four-Image pool is consumed by the two units' populated image lists; mandatory Bullet image creation fails and its native rollback leaves zero active Bullets/100 free records, without consuming additional RNG. Private pool capacities are disclosed inputs; no null allocator result is stubbed.

## Source reconciliation and remaining boundaries

Pinned [GPTP weapon fire](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/weapons/weapon_fire.cpp#L16) and [OpenBW weapon/IScript code](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h) supplied useful producer, bounce, splash and Scarab structure. OpenBW's `release_fighter` retains the same fixed initial HP baseline. Its `attkshiftproj` passes a per-call offset, while the recovered original handler mutates the shared live DAT byte. Open-source structure therefore cannot replace original state-transition evidence.

This batch closes the selected populated effects and failure contracts. Remaining compositions include disconnected-region Reaver routes, physically moving Scarab travel, automatic Tank command inheritance, full-frame concurrent multi-source Lurker history interactions, air-splash random victim rules, and other weapon/explosion families. Spell DOT state4 belongs to separate spell evidence. Upgrades, armor/shields, matrix and hallucination already have [native damage](native-damage.md) contracts; this batch does not claim a cross-product of every modifier with every selected splash family, rendering correctness or six-campaign parity.
