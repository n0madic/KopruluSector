# Native 1.17 computer combat, queued AttackMove, Hold and suicide attacks

This batch connects genuinely allocated GuardAI and MilitaryAI records to the complete original AttackUnit consumer, including return-home decisions, Stim eligibility, populated stock images, projectiles and native damage. It also executes selected main AttackMove, HoldPosition, Scourge suicide and Infested Terran Sap paths. It extends [AttackUnit and timers](native-attack-orders.md), [guard lifecycle](native-guard-lifecycle.md), [military arrival](native-military-arrival.md), [projectiles](native-projectiles.md), [primary orders](native-primary-orders.md) and [damage](native-damage.md), preserving their published sources.

The target is the exact local **StarCraft 1.17.0.1** executable, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. All addresses below are preferred virtual addresses with ImageBase `0x400000`; subtract that base for RVAs. Earlier reverse-engineered versions provide structural clues, not reusable addresses or automatic parity evidence.

## Evidence and execution boundary

- [Static extractor](../../tools/research/inspect_native_computer_combat_117.py), SHA256 `bf65ed9a0de9f45f7bdcd4b3a8989ed1f123dcfaff1f1391f52ce99d612642b9`, and [static evidence](evidence/native-computer-combat-static.json): **25 instruction ranges / 1,195 decoded instructions**, no undecoded tails.
- [Original-instruction probe](../../tools/research/probe_native_computer_combat_117.py), SHA256 `6bfefee5180a0a2b492918923822531fd9ffb9b7685cefbab95949bf61fe238c`, and [probe evidence](evidence/native-computer-combat-probes.json): **69 passing complete cases**, **zero prefixes**, **zero helper substitutions**. The evidence records **1,704 complete top-level sentinel returns**, **3,713,846 original instructions**, **306 visited function starts**, **749 reviewed ranges** and **58 imported source-module hashes**.
- **Separate new asset provenance:** [native-computer-combat-assets.json](evidence/native-computer-combat-assets.json), SHA256 `3c9fa988fd8e0b1f7e2fb110cf44f9f75d3adad90fa0ddc87c1959838c014eeb`: **40 image IDs / 36 distinct GRP paths**, with archive provenance, bytes, hashes and LO inputs. It retains the previous attack batch's resources and adds stock Scourge images 0–4, Infested Terran images 8–10 and Bullet image 503. Earlier manifests are unchanged. Retail resources remain in private scratch storage.

| Cases | Complete selected contracts |
|---:|---|
| 8 | Actual guard allocation and return-home predicate/issuance, including check-only, region, distance, target, previous-attacker and queue gates. |
| 3 | Full computer AttackUnit at home distances 191/192/193, including the actual connected ground-AI chase dependency. |
| 3 | Stock mechanical Wraith's exact maximum-HP gate for returning home. |
| 4 | Direct Stim application at ten-HP and existing-timer boundaries. |
| 21 | Computer Stim decision: HP rounding, target HP/shields, native technology requirements, speed/Ensnare/Stim statuses, vulnerability and range. |
| 4 | Allocated-guard Marine/Wraith/Zealot AttackUnit followed by real stock ranged or melee damage. |
| 3 | Actual MilitaryAI allocation/arrival, then Marine/Wraith AttackUnit and damage. |
| 3 | Main human AttackMove acquires an enemy, queues its continuation, activates AttackUnit and deals stock damage. |
| 2 | Main human HoldPosition acquires a ground/air enemy and deals stock damage while retaining Hold. |
| 2 | AttackMove continuation after target invalidation, and immediate arrival at the current position. |
| 12 | Main Hold's four cooldown RNG residues on Marine ground, Wraith ground and Wraith air attacks. |
| 2 | Human/null-AI and computer/allocated-guard Scourge attacks, actual weapon-55 impact, Death issuance and AI removal. |
| 2 | Infested Terran main SapUnit134 and SapLocation135: actual animation signal, weapon-54 splash and Death issuance. |

Execution uses x86_64 through Rosetta, Unicorn **2.1.4**, pefile **2024.8.26**, bounded at 100,000 instructions per invocation and restricted to reviewed original ranges. Every selected call reaches its sentinel and validates stack cleanup. For every case, the probe derives the observed RNG source IDs from actual call arguments, checks their counter sum against native total `0x673938`, and independently verifies the exact shared LCG state. This includes **source56**, used by military arrival; it does not assume that all relevant sources fit within the first fifty counters. [PRNG specification](native-prng.md).

Private memory contains authenticated stock Units, Flingy, Sprites, Images, Weapons and Orders DAT, IScript, GRP and LO resources; finite unit/sprite/image/order/Bullet/Thingy pools; and explicit player, visibility and terrain fixtures. All **54/7/6/14/24/19 native DAT columns** are copied using the executable's descriptors. The real `0x425610` acquisition initializer executes before unit creation. The guest creates/completes/shows the selected units and initializes their actual image lists. Python supplies resource state; the original archive/resource loader and renderer are not executed or certified. PyMS remains pinned to `bfc5d3aad0b5614a5aff72c223f8efa00afddfa4`.

The terrain fixture has four regions, visible tiles, empty obstacle contours, selected region-grid assignments and eight valid private per-player region arrays with the native 52-byte stride. Guard and military **records are produced by original allocators**, rather than manufactured AI type bytes. The region arrays, free military-pool record and technology/status/HP inputs are explicit fixtures. These cases do not initialize or certify all campaign AI strength tables, scheduling, connectivity generation or army state.

The direct computer attack-effect fixtures deliberately use the same owner for source and victim to isolate attack/support effects from unrelated hostile hit reactions. Positive automatic acquisition, AttackMove, Hold and Infested Terran splash use owners3/4 with self-alliance1 and cross-alliance0. No full object callback, accepted game frame, continuously moving chase, complete world, WinMain, Windows import, renderer, host-file/network operation or substituted helper executes in this batch. [Physical movement and full object-callback compositions](native-movement-fsm.md) are separate evidence.

## Caller contracts and actual metadata

| Entry | ABI / selected role |
|---|---|
| `0x4b2640` | No arguments; initializes the native shared guard-record pool and per-player lists. |
| `0x4b2010` | ECX=CUnit; allocates/attaches the actual matching/free GuardAI record. |
| `0x4b2270` | ECX=CUnit, DX=home x, stack=[home y]; sets guard home. |
| `0x4b2110` | ECX=CUnit, EDX=check-only; returns 1 when return-home gates pass. Zero check-only also issues order163. |
| `0x4b1100` | ECX=CUnit; computer attack-support Stim decision; native void return. |
| `0x438af0` | ECX=CUnit; Stim effect; native void return. |
| `0x428af0` | ECX=CUnit; signed movement-speed status modifier. |
| `0x4ae760` | ECX=AI region, EDX=CUnit, stack=[force requested region]; actual MilitaryAI allocation and arrival. |
| `0x41e5c0` | ECX=CUnit; complete main AttackUnit consumer. |
| `0x439420` | ECX=CUnit; main-order dispatch under native order-wait gates. |
| `0x41e4a0` | ECX=CUnit; main AttackMove14 consumer, imported unchanged from the earlier order evidence. |
| `0x41e9b0` | ECX=CUnit; **main HoldPosition107** consumer. |
| `0x420bd0` | ECX=CUnit; Hold attack attempt/cooldown/animation writer; returns whether the attempt starts. |
| `0x431b70` | ECX=CUnit; Hold-specific target acquisition. `0x432060` tail-jumps here. |
| `0x41eeb0` | ECX=CUnit; main SapLocation135 consumer. |
| `0x41ef70` | ECX=CUnit; main SapUnit134 consumer. |
| `0x409590` | ECX=candidate CUnit, EDX=Bullet; ground-splash callback, returns zero after evaluating/applying the candidate. |
| `0x40a9f0` | ECX=Bullet, stack=[radius,candidate CUnit]; inclusive point-to-target-bounds radius predicate, `ret 8`. |

Guard record type1 is at `+8`, parent at `+0x0c`, home WORDs at `+0x12/+0x14` and other-home WORDs at `+0x16/+0x18`. Actual `0x4b2270` changes the first pair and leaves the second pair at its original creation position in these fixtures.

Military record type4 instead stores an **assigned-region pointer at `+0x10`**. The same bytes are not guard home coordinates. The new evidence reports record fields according to the actual type.

For successful type4 allocation, the requested region's owner/state/record-pool fields are initialized explicitly, its free-record pointer at `pool+0x4e20` is populated, and `0x4ae760` performs the real pop, list insertion, parent/region writes and `unit+0x134` assignment. The selected state0 arrival performs three source56 draws and issues157 through the real primary-order chain and populated-image idle reset. An explicit native AttackUnit request then replaces that arrival order. This extends earlier military-arrival evidence to genuine populated stock images; it does not execute later strategic order157 behavior.

## Computer AttackUnit and guard return-home

The local main AttackUnit preamble checks controller **1**. It calls `0x4b2110` only when the current target is null or lacks DAT building-property bit1. A building target bypasses this return-home attempt. If return-home succeeds, AttackUnit returns before target selection/chase/firing. Otherwise it executes `0x41de10 → 0x420770`; a successful attack opportunity calls `0x4b1100` when the source AI pointer is nonnull, then `0x41f630` with base animation5.

The complete return-home contract is:

```text
require AI != null and AI.type == 1
if race == 1 and DAT properties include 0x40000000:
    require current raw HP == DAT maximum raw HP
require previousAttacker == null
require nativeRegion(home) != nativeRegion(unit)
require nativePointInArea(unit, radius=192, home) == false
require target == null or target is outside current weapon range
require queued head is absent or queued head order != 152
if checkOnly == 0:
    issue native order163 at guard.home
return 1
```

Each failed gate returns zero without issuing163. The stock Wraith is race1 with the mechanical-property bit; **120×256 HP passes, `120×256−1` and 119×256 fail**. This is exact raw-HP equality, not rounded displayed HP. The native point predicate uses the unit's fixed-position center and original integer distance, not rectangle distance or floating Euclidean distance. At horizontally aligned home distances **191/192**, returning is rejected; **193** permits it when the region/target gates also pass.

In the eight direct guard cases, check-only1 returns1 while retaining AttackUnit10. Issuing mode changes10→163. A queued head152 suppresses return; a queued14 does not suppress it and is removed by the actual interrupting primary-order issuer. Same region, nearby home, in-range target or a nonnull previous-attacker pointer suppress it.

The three full AttackUnit boundary cases also execute the real ground-AI destination helper `0x437540` when returning is rejected. A genuine stock Marine carries initialized status bit`0x20000`; the probe does not clear it to bypass the helper. The selected path checks the actual same-group terrain relation, GuardAI's nonmilitary classification through `0x4b0580`, and actual `0x4ab450 → 0x4ab330` region acceptance. It returns0, allowing normal chase setup. These stationary selected calls establish the eligibility/destination decision; transport requests, disconnected terrain and physically moving pursuit remain separate contracts.

Pinned [Teippi return-home implementation](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit_ai.cpp#L101) corroborates the gate structure. Pinned [GPTP AttackUnit](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/base_orders/attack_orders.cpp#L1245) corroborates the calling order. Local native instructions and the discriminating 1.17 executions establish the addresses, numerical order IDs and exact boundaries here.

## Stim: decision, payment and two different speed flags

`0x4b1100` only considers unit IDs **0 Marine / 32 Firebat** and requires a nonnull current target. It contains no direct controller test; ordinary AttackUnit invokes it only for a nonnull AI pointer. The eligible unit-ID branch is static evidence; the positive decision cases here use stock Marine.

Its first gate is the signed **movement** speed-status modifier at `0x428af0`:

```text
modifier = (stimTimer != 0) + ((status & 0x10000000) != 0)
           - (ensnareTimer != 0)
require modifier <= 0
```

This uses **`0x10000000`**, whereas the weapon-cooldown modifier in `0x41fb70` uses **`0x20000000`**. The new positive/negative cases distinguish those bits. Reusing the cooldown modifier for this AI decision changes original behavior.

The remaining gates are:

1. Obtain maximum whole HP from DAT raw maximum shifted right8. For a zero maximum, the native fallback is current rounded HP, or1 when that is zero.
2. Require `ceil(currentHP/256) >= floor(maximumWholeHP/2)`. Marine raw19×256 fails; **19×256+1 and20×256 pass**.
3. Execute **the reverse attack-legality test**: `target.CanAttack(source, detection=1)` through actual `0x41da50`.
4. Require the source target to be inside its current weapon range through `0x421060`.
5. Call the real technology-use chain `0x407f90 → 0x407570` for **tech0**, source owner, source unit; require return exactly1. Actual `0x4080d0` initializes the native technology-use offsets. Available/researched bytes are declared fixture inputs, not substituted checks.
6. Require target life from `0x41faa0` to be at least100: rounded-up HP plus rounded-down shields when stock shield-enabled DAT says shields apply.
7. Tail-jump to actual `0x438af0`.

Target raw99×256 fails; 99×256+1 qualifies. A stock Zealot with40 HP and60 shields qualifies; reducing shields by one raw unit changes their floor to59 and rejects the99-life case.

An already active Stim timer by itself suppresses the decision. Active Stim and Ensnare together cancel in the movement modifier and permit another use when all other gates pass. Ensnare alone also permits it. An invulnerable **source** rejects the reverse attack-legality test. An invulnerable **target** still permits a direct `0x4b1100` call in this fixture because that reverse test asks whether the target can attack the source. Ordinary AttackUnit filters the invulnerable target earlier; direct-helper evidence must not be generalized past its caller preconditions. Disabled source, out-of-range target, unavailable technology and unresearched technology each reject.

`0x438af0` requires **raw HP strictly greater than10×256**. It consumes one RNG draw from **source31**, range278..279, even with the declared sound backend disabled, then applies10×256 real HP damage through `0x41e150`. It raises Stim timer to37 when below37 and recalculates movement speed through `0x43aea0`; stock Marine's speed recalculation reaches the actual Walking IScript dependency. For an existing timer37 or40, HP payment and the draw still occur but the timer is retained and speed recalculation is skipped. The direct boundary tests distinguish tenHP exactly from tenHP plus one raw unit: the latter pays and survives with rawHP1.

The allocated-guard Marine→Wraith composition starts at40HP, applies Stim, ends at30HP/Stim37 and delivers a real6HP ranged hit. Guard Wraith→Marine and Wraith→Wraith deliver8HP and20HP without Stim. The three actual type4 military compositions produce the same selected effects with real region-linked records and arrival RNG consumption.

## AttackMove queue composition and main HoldPosition

The native main-order dispatch matters. **HoldPosition107 dispatches `0x439838 → 0x41e9b0`**. The earlier inspected helper `0x41e760` is not this main Hold handler. The new cases call real `0x439420`, validating the selected dispatch before the actual attack effect.

### AttackMove14

For owner3 attacker at `(200,200)` and owner4 enemy at `(280,200)`, an actual `0x42a340` request installs14 with destination `(500,200)`. Main dispatch executes the real movement-goal/waypoint setter `0x43a070`, reaction/acquisition prelude and target selection using native-normalized seek range and actual spatial entries.

The order transformation is concrete: the native queue helpers save **AttackMove14 at `(500,200)` with null target**, prepend **AttackUnit10 targeting the selected enemy**, and activate10 through actual `0x429d00 → 0x42a5a0 → 0x42a610`. The source gains target-switching status`0x08000000` through `0x41cf90`. A second main dispatch attacks the acquired target. Twenty real source-image/Bullet/Thingy steps produce Marine6HP, Wraith-ground8HP or Wraith-air20HP damage, while the continuation14 remains queued.

Invalidating the acquired target with the declared invulnerability bit and setting order-wait0 permits the next main call to reject it and activate the saved14. The resulting order is14, queue is empty and saved target coordinates are still`(500,200)`. A separate14 destination already at the current position completes immediately through actual order-done/activation: **order2**, null queue and saved target coordinates`(0,0)`.

That immediate return does not imply that the unit remains on Guard2 indefinitely. The separate [movement object-callback batch](native-movement-fsm.md) proves subsequent Guard2 processing through `0x41e700` selects PlayerGuard3 for its controller2 fixture on a later callback. The raw stock idle columns are2; order completion and a later idle handler are separate transitions.

### HoldPosition107

Its state0 initialization calls real `0x43a4e0` to stop and enters state1. It attempts the current target through `0x420bd0`. If no successful attempt and the order timer is zero, it sets that timer15 and acquires through `0x432060 → 0x431b70`, storing the target and resetting order-wait.

The next selected main call uses the current target and actual readiness/range/facing/cooldown gates. On success `0x420bd0` computes the modified cooldown, consumes **source13**, takes `(random &3)−1`, writes **both ground and air cooldown bytes**, and selects ground animation5 or air animation6. It preserves primary order107 and does not prepend AttackUnit or chase the target.

Twelve cases independently choose seeds for all four residues on three stock weapon paths:

| Source / target | Base cooldown | Written paired cooldowns |
|---|---:|---|
| Marine / Marine | 15 | 14,15,16,17 |
| Wraith / Marine | 30 | 29,30,31,32 |
| Wraith / Wraith | 22 | 21,22,23,24 |

Two populated Marine ground/air effects then retain107, target the acquired enemy and deal6HP through original images/Bullets/damage. Hold's jitter source13 is distinct from **AttackUnit source12**. Its acquisition code has a source19 random tie-selection branch; these one-enemy positive fixtures do not reach that branch, so it is static evidence here.

## Real Zealot melee and Scourge suicide

The genuine guard Zealot65 at24-pixel horizontal center distance executes the complete main AttackUnit function and actual stock image151 IScript. The native VM reaches melee effects and applies **two8HP hits**, taking Marine40→24HP. The evidence records the original melee call/VM path and no Bullet initialization or projectile impact for that case. It is not a ranged weapon synthesized as melee damage.

Scourge47 retains its stock AttackUnit10 order column. The two cases use an actual stock Scourge/Wraith pair, source at`(200,200)`, target at`(224,200)`, matching heading, and either controller2/nullAI or controller1/originally allocated GuardAI. Main dispatch and the real air-attack animation create the Scourge attack explosion image4 with populated GRP/IScript; its main/shadow images0/1 also execute.

The source VM produces a real **weapon55 Bullet**: stock damage110, behavior6, explosion1, cooldown1, graphics172→sprite370→image503. Bullet processing applies **Wraith120→10HP on image/Bullet step6**, then returns the Bullet to its pool on step7. Actual `0x420910`, primary Death issuance `0x42a4d0`, generic AI removal `0x48dbc0` and guard removal execute. Both cases end with source primary order0 and AI null.

Crucially, primary order0 is an **issued Death**, not proof of completed unit reclamation. These subsequent steps update source images, Bullets and Thingys, not the source Death main-order dispatcher. The source CUnit/Sprite and live/completed counters remain present in the final evidence. Overlay/Bullet resource recycling is observed separately; [visible Death and full reclamation](native-unit-death.md) establish other declared lifecycle compositions.

## Infested Terran SapUnit134 and SapLocation135

Infested Terran50 uses stock attack-unit134 and attack-move135, unlike Scourge's stock10. The two selected cases issue those actual orders through native primary-order entries and call the complete real main dispatcher. Source is at`(200,200)`, enemy Marine at`(212,200)`; the location target is the current source position. This close stationary profile avoids physically moving approach while executing the actual near-target/arrival tests. It is not a broad moving suicide-pursuit proof.

**SapUnit134, `0x41ef70`:** null target goes to order-done; a target rejected by the native attack-legality helper changes the request to Move6 at its position. Otherwise state0 attempts the real unit-target movement setter and enters1. State1 checks actual rectangle distance4 or unmovable-state2, stops, and requires the target to be within the stock weapon54 outer radius60. Failure completes the order. Success changes the order target to **self**, plays animation13 across the actual image list, sets status`0x1000` and enters2. State2 waits for order-signal bit1, then sets status`0x80000000` and issues Death0. Far moving-target/approach branches are static witnesses, not executed cases here.

**SapLocation135, `0x41eeb0`:** state0 sets native move goal/waypoint and enters1. State1 waits until Sprite coordinates equal the move goal; its unmovable flag branch applies the native outer-radius check before continuing. It then makes the same self-target / animation13 / status`0x1000` / state2 transition. Signal bit1 subsequently sets self-destruct status and issues Death0.

Both stock image8/9 scripts and actual explosion image10 execute. The VM creates a **weapon54 Bullet**, targeting its source at`(200,200)`, and emits the order signal through real IScript. Weapon54 is stock500 damage, explosive, behavior5, explosion3, splash radii20/40/60. The real Bullet impact at step6 reaches `0x409590` through the actual spatial overlap iteration, evaluates the enemy Marine, and invokes native damage: **40HP→0**, primary Death is issued. Step7 recycles the Bullet.

The newly witnessed ground-splash callback first excludes the source except its separate weapon84 exception. For explosion3/24 it excludes same-owner candidates unless they equal the Bullet target. It executes actual weapon-target eligibility, then the outer-radius point-to-bounds test. The inner, medium and outer selected damage calls pass divisors **1,2,4** to `0x41e290`. These cases reach the inner region and actual full-damage/kill path; medium/outer attenuation and the weapon84 per-unit marker branch are static instructions here and require their own discriminating positive cases.

`0x40a9f0` measures the Bullet Sprite center against the candidate's stock DAT rectangle, including the native right/bottom `+1`, applies the original approximate-distance helper and compares **distance≤radius**. It does not use unit-center distance. Preserve that geometry when implementing splash; [damage](native-damage.md) remains the authority for subsequent damage type, size, armor, shields and attribution.

Pinned [GPTP SapUnit/SapLocation](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/base_orders/attack_orders.cpp#L917) and [Teippi Sap consumers](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L3082) provide the structural anchors. Their source implementations and enum choices do not replace these local 1.17 static/isolated execution results.

## Defold implementation contracts and remaining compositions

Keep GuardAI and MilitaryAI as distinct simulation records with their own field layouts. Return-home should query the same native-style region IDs, fixed-position integer distance, raw HP and current queue head that drive the original decision. Preserve the early return before target selection/firing; performing the support decision after firing changes Stim eligibility/payment and the effective attack timing.

Keep three independent concepts: a successful attack opportunity, installation of an IScript attack animation, and the later actual melee/Bullet effect. Stim has source31 randomness/payment; AttackUnit cooldown jitter uses12; Hold jitter uses13; military arrival consumes three source56 draws. Sound being disabled does not suppress Stim's RNG consumption. Queue activation must preserve/interrupt actual continuation orders, rather than storing AttackMove as a UI-only destination.

The observed functions and cases close these selected contracts. Remaining compositions include positive computer AttackMove/Hold acquisition through strategic selectors, physically moving transport/chase/Sap approaches, full world/AI scheduler interaction, multi-candidate Hold ties, Firebat's populated positive Stim/attack path, disconnected terrain return behavior, splash attenuation/eligibility boundaries, broader ranged projectile behaviors, and death/owner/counter reclamation after these particular suicide attacks. [Movement](native-movement-fsm.md), [military AI](native-military-ai.md), [spell fields](native-spell-fields.md) and [unit lifecycle](native-unit-death.md) supply separate stable dependencies; they must be composed and compared without treating any isolated green batch as full six-campaign parity.

## Reproduction

```sh
python3 tools/research/inspect_native_computer_combat_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-computer-combat-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_computer_combat_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-computer-combat-assets \
  --asset-manifest docs/research/evidence/native-computer-combat-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out docs/research/evidence/native-computer-combat-probes.json \
  --case-group all --execute-original-code
```

Without the explicit execution flag the probe prints its plan and executes no original instructions. The evidence records exact inputs, output state, complete-call counts, actual callee/register/stack traces, populated image state, VM opcodes, Bullet initialization/impact contexts, per-source RNG checks, target/data/asset/probe/dependency hashes and reviewed ranges. Those distinctions are part of the remake specification, not optional testing metadata.
