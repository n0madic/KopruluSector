# Native 1.17 AttackUnit, acquisition, cooldowns and periodic timers

This batch executes ordinary AttackUnit issuance, positive automatic target selection, range/chase decisions, native cooldown writers and stock Marine/Wraith image-to-projectile damage. Three additional cases run the complete original object callback for **48 consecutive calls**, allowing real unit cooldowns, orders, images and Bullets to interact. It extends [projectiles](native-projectiles.md), [active-unit/IScript ordering](native-active-iscript.md), [combat target selection](native-combat-ai.md), [primary orders](native-primary-orders.md) and [damage](native-damage.md), without changing their published tools.

Two corrections matter for a faithful remake. The original initializes live acquisition ranges after reading DAT; raw Marine/Wraith acquisition values are zero and cannot be used directly. Also, **`0x430a8f` is a strength-cache refresh inside Hallucination conversion**, not a periodic timer-update call. The actual periodic spell service is `0x43a320 → 0x4351e0`, gated by a per-unit eight-step counter.

The target is the exact local **1.17.0.1** executable, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. All addresses are preferred virtual addresses with image base `0x400000`; subtract the base for RVAs. Earlier reverse-engineered versions supply structural clues, not transferable addresses or automatic parity evidence.

## Evidence, resources and execution boundary

- [Static extractor](../../tools/research/inspect_native_attack_orders_117.py), SHA256 `e53d9e2ddb3cf61d991ed0e80140e292935cc15cb1f112373fb23100f28ddeb9`, and [static evidence](evidence/native-attack-orders-static.json): **39 instruction ranges / 1,913 decoded instructions**, no undecoded tails.
- [Original-instruction probe](../../tools/research/probe_native_attack_orders_117.py), SHA256 `ee0d831eec788f08b8c879ac70f8616f2f20cad5687ab820f84bec0660af28f6`, and [probe evidence](evidence/native-attack-orders-probes.json): **234 passing complete cases**, **zero prefixes**, **zero helper substitutions**. The evidence records **2,099 complete top-level sentinel returns**, **10,102,126 original instructions**, and **300 visited function starts** under **681 reviewed ranges**. All 55 imported source-module hashes are recorded.
- **Separate new asset provenance:** [native-attack-orders-assets.json](evidence/native-attack-orders-assets.json), SHA256 `512bc3b6261c757bb4ffb192fabf1cf6912f3698c7659021423e9edf76e583f4`: **31 image IDs / 28 distinct GRP paths**, with archive provenance, bytes, hashes and LO inputs. It preserves the projectile batch's assets and adds stock Civilian images 221/222. Earlier asset manifests are unchanged. Retail data remains in private scratch storage.

| Cases | Complete selected contracts |
|---:|---|
| 6 | Native AttackUnit issuance followed by Marine ground, Wraith ground and Wraith air attacks; three invalid-target fallbacks. Positive attacks run twenty image/Bullet/Thingy steps. |
| 4 | All 228 stock acquisition rows initialized by the original routine; three discriminating synthetic forward/back-reference profiles. |
| 84 | Stock weapons 0/15/16, four Acid Spore counts and seven Stim/Ensnare/weapon-speed-status combinations. |
| 27 | Synthetic base-cooldown and spore-count boundary inputs for minimum/maximum clamps. |
| 32 | Ground/air cooldown gates crossed with cooldown and order-wait boundary bytes. |
| 12 | Native AttackUnit cooldown jitter for all four RNG residues on the three stock attack pairs. |
| 9 | Native weapon facing around the Marine's attack-angle boundary. |
| 8 | Native rectangle-distance, minimum-range and maximum-range boundaries. |
| 24 | Positive spell-timer service across all eight counter phases and three completion/visibility profiles. |
| 6 | Stim/Ensnare expiry with real Marine/Wraith speed recalculation. |
| 2 | Stock Zealot shield regeneration with cache sentinels. |
| 2 | Full Marine/Wraith Hallucination conversion, timed life, secondary order and cache refresh. |
| 12 | AttackUnit state zero/nonzero crossed with in-range/out-of-range stock targets. |
| 3 | Positive auto-acquisition, current-target retention or replacement, then stock projectile damage and nearby allied-human reaction. |
| 3 | Complete `0x445070` object callbacks, 48 consecutive calls per stock attack pair. |

The host is x86_64 through Rosetta, Unicorn **2.1.4**, pefile **2024.8.26**. Each invocation is bounded at 100,000 instructions and restricted to reviewed original code. Every completed call reaches its sentinel and validates stack cleanup. All fifty RNG-source counters and final state are checked against an independent exact-integer LCG. [PRNG specification](native-prng.md).

Private memory contains authenticated stock Units, Flingy, Sprites, Images, Weapons and Orders DAT, IScript, GRP and LO resources; finite unit/sprite/image/order/Bullet/Thingy pools; and explicit player, visibility and terrain fixtures. All **54/7/6/14/24/19 native DAT columns**, respectively, are copied according to the executable's own descriptors. Python copying supplies resource state; it does not execute or certify the original archive/resource loader. PyMS is pinned to `bfc5d3aad0b5614a5aff72c223f8efa00afddfa4`.

Native creation, completion/show, order issuance, list maintenance and populated image initialization execute before the selected attack calls. Numeric cases deliberately set the scalar timer/status/DAT fields named in their inputs. Those boundary fixtures establish instruction behavior, not the reachability of every synthetic state through normal play. No WinMain, whole game, Windows import, renderer, host-file/network operation or substituted callee executes in the guest. A complete object callback is not a complete accepted game frame or campaign execution.

## Caller contracts and live unit fields

| Entry | Contract | Selected result |
|---|---|---|
| `0x425610` | No arguments | Normalizes all 228 acquisition bytes in ascending unit-ID order. |
| `0x42a380` | ECX = CUnit; stack: primary order, target CUnit | Actual target-order issuance; these cases request AttackUnit **10**. [Queue/allocation contract](native-primary-orders.md). |
| `0x41e5c0` | ECX = attacking CUnit | Complete main AttackUnit consumer; native void return. |
| `0x41de10` | ECX = attacking CUnit | Current-target validation and selected acquisition/replacement. |
| `0x420770` | ECX = attacking CUnit | Range/chase transition; nonzero permits the later attack-animation attempt. |
| `0x41f630` | ECX = attacking CUnit; stack: base animation | `ret 4`; selects ground/air weapon and animation, then attempts the cooldown writer. |
| `0x41f530` | ECX = attacking CUnit; stack: cooldown selector, weapon, animation | `ret 12`; returns 1 when it installs the firing animation, otherwise 0. |
| `0x41fb70` | ECX = CUnit; stack: weapon ID | `ret 4`; modified/clamped cooldown before jitter. |
| `0x41ff50` | ECX = CUnit; stack: weapon ID | `ret 4`; weapon readiness, including distance and facing. |
| `0x43a320` | ECX = CUnit | Actual per-unit timer/progress service. |
| `0x4351e0` | ECX = CUnit | Periodic spell-timer service; tails into Acid Spore timers. |
| `0x430a00` | ECX = CUnit | Hallucination conversion, including the call at `0x430a8f`. |
| `0x445070` | No arguments | Complete object callback under the declared scheduler/list/terrain fixtures. |

Relevant CUnit fields are: move target position `+0x10/+0x12` and target pointer `+0x14`; Flingy flags `+0x20`, heading `+0x21`, next velocity heading `+0x23`; next waypoint `+0x1c/+0x1e`, position `+0x28/+0x2a`; owner `+0x4c`, primary order `+0x4d`, order state `+0x4e`; timers `+0x54` order, `+0x55` ground cooldown, `+0x56` air cooldown, `+0x57` spell cooldown; saved order target coordinates `+0x58/+0x5a`, target pointer `+0x5c`; shields `+0x60`; previous attacker `+0x7c`; order wait `+0x85`; movement state `+0x97`; status `+0xdc`; target-position refresh timer `+0xe3`; timed life WORD `+0x110`; master spell-timer byte `+0x122`; AI pointer `+0x134`; cached air/ground strength WORDs `+0x138/+0x13a`.

## Raw DAT and the original acquisition initializer

`0x425610` is a required producer of live unit data. It operates on the acquisition array at `0x5219f0`, reads subunit IDs at `0x51fb90`, and uses the chosen attacker's ground/air weapon rows. Its complete algorithm is:

```text
for id = 0 .. 227, ascending:
    attacker = subunit1[id] if subunit1[id] != 228 else id
    acquisition = liveAcquisition[attacker]
    for weapon in [groundWeapon[attacker], airWeapon[attacker]]:
        if weapon < 130:
            acquisition = max(acquisition, weaponMaximumRange[weapon] >> 5)
    liveAcquisition[id] = uint8(acquisition)
```

This is **in-place** initialization. A backward subunit reference reads the already modified earlier row; a forward reference reads its earlier value. It is neither an immutable-row calculation nor a topological/cyclic-reference solver. The three synthetic two-row cases suppress both weapons with sentinel 130 and start acquisition values `[9,2]`: subunit references `[1,0]` produce `[2,2]`; `[228,0]` produce `[9,9]`; `[1,228]` produce `[2,2]`. These discriminate ascending mutation from reading an immutable snapshot.

The stock test independently checks all 228 output bytes. Selected normalized values are:

| Unit ID | Live acquisition in 32-pixel units |
|---|---:|
| 0 Marine | 4 |
| 1 Ghost | 7 |
| 3 Goliath / 4 turret | 6 / 6 |
| 5 Tank / 6 turret | 8 / 8 |
| 8 Wraith | 5 |
| 12 Battlecruiser | 6 |
| 32 Firebat / 37 Zergling / 65 Zealot | 3 / 3 / 3 |
| 66 Dragoon | 4 |
| 103 Lurker | 6 |
| 124 Missile Turret | 7 |

Marine and Wraith have **raw acquisition zero**, so an auto-acquisition probe using only raw DAT can incorrectly see no enemy. Every populated attack setup in this batch executes the real initializer before creating units. The pinned OpenBW initializer was a useful structural clue; the exact local native loop and its synthetic reference ordering are independently executed here. [OpenBW initializer](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L19913).

## Complete main AttackUnit and target selection

The selected human-controller path through `0x41e5c0` performs `0x41de10` target selection, then `0x420770` range/chase. A nonzero result permits `0x41f630` with base animation **5**. A nonnull AI pointer adds `0x4b1100` before the animation attempt; the controller-1 preamble can call `0x4b2110`. Those computer/nonnull-AI branches are visible in static evidence but are not reached by this batch's human-controller, null-AI fixtures.

Target switching in `0x41de10` requires status **`0x08000000`**. The positive composition uses genuine created/shown stock units and their sorted spatial entries: attacker Marine owner 3 at `(200,200)`, a Marine or Civilian owner 4 at `(280,200)`, and a second owner-4 Marine at `(240,200)`. Both controllers are 2, self-alliance bytes are 1 and cross-alliance bytes are 0. These alliance/status assignments are explicit fixtures.

The native chain reaches `0x431c40` acquisition, normalized seek range, `0x432280` candidate buckets, `0x433c50` minimum-range logic, `0x442b20` radius iteration, actual `0x422330` spatial query and `0x433910` bucket-member selection. The three full AttackUnit outcomes are:

| Initial current target | Result |
|---|---|
| Null | Acquires the nearer Marine at `(240,200)`. |
| Armed Marine at `(280,200)` | Retains it despite another armed Marine being closer. |
| Unarmed Civilian at `(280,200)` | Replaces it with the Marine at `(240,200)`. |

After eight actual image/Bullet/Thingy steps, the selected Marine loses **6 HP** through original projectile/damage code. The enemy damage path also reaches complete human reaction `0x420190` and nearby allied reaction `0x420120`, including a real populated radius search and previous-attacker state. This closes the selected attack/reaction composition; it does not establish all cooperative AI orders or all target-ranking categories. [Rank and bucket rules](native-combat-ai.md).

The order is installed through actual `0x42a380 → queue/activation`, rather than writing the primary-order byte. Facing is prepared by explicit heading 64 plus actual `0x417190` sprite/image direction update. A stock object's random initial direction therefore does not silently determine readiness in these tests.

## Range, fallback and chase state

`0x420770` first invokes real target legality `0x41da50` with detection checking enabled. A null target or an invalid target with target-switching enabled stops through `0x43a490` and completes the order through `0x42a5a0`. For a nonnull unattackable target, no target-switching bit and no AI pointer, it first prepends **Move 6** to the target sprite's coordinates through complete `0x429b70` and actual order pools, then completes AttackUnit and activates that Move.

Three complete invalid-target calls confirm: null target returns to primary order **2**; an invulnerable target activates Move **6**; a source with fixture status `0x400` also activates Move **6**. None writes a firing cooldown. The last case calls AttackUnit directly: the ordinary main-order dispatcher would gate that source before entering AttackUnit. It must not be described as a normal full-world legal attack.

For a legal target, the routine saves target coordinates to `+0x58/+0x5a` and updates the next waypoint through `0x40cbd0`. Its native order-state split is **zero versus nonzero**:

- State zero checks `0x421060`. When out of range, it starts following through `0x43a180`; success sets state **1** and returns 1. That return only permits a subsequent readiness check; it does not guarantee a shot at an out-of-range target.
- A nonzero state checks `0x420ab0`. Selected out-of-range human ranged attacks execute `0x438ba0 → 0x43a180`; in-range attacks stop through `0x43a490` and permit firing. Computer-AI escape, target-switching and Suicide/melee branches remain separate selected paths.

The twelve stock calls cross Marine/Wraith ground/Wraith air with initial state 0/1 and target distances 80/500 pixels. Every successful state-zero call becomes 1; the selected nonzero calls remain 1. At 500 pixels, native follow target becomes `(700,200)` with the target CUnit pointer and refresh timer `+0xe3 = 15`; at 80 pixels it is stationary at `(200,200)` with a null movement target pointer. State-zero in-range leaves that timer 0; nonzero in-range stop produces 15.

These are complete **chase-issuance** contracts. Continuous ground movement, moving-target interaction with local paths, and pursuit through authentic obstructed maps are not composed here. [Local-path storage](native-path-storage.md), [terrain/collision](native-movement-collision.md).

The pinned Teippi AttackUnit rewrite uses additional states to remedy a stuck-unit problem. Those states are not substituted for this executable's zero/nonzero branch. [Teippi reconstruction](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L3258), [GPTP structural reference](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/base_orders/attack_orders.cpp#L1245).

## Readiness, range boundaries and firing animation

`0x41f630` clears the last-Bullet-spawner global through `0x40a9e0`. Status `0x8000` prevents this ordinary firing path. Airborne-target status 4 selects the air weapon, cooldown selector **2**, and base animation plus one (**6**); otherwise it selects the ground weapon, selector **1**, and animation **5**. Weapon accessors include DAT subunit handling. Successful turret/other unit-order compositions are outside these three Marine/Wraith pairs.

`0x41ff50` rejects an already-attacking Flingy flag 8, distance below minimum, distance above maximum, and unit byte `+0x105` bit 2. It uses actual inclusive unit bounds and rectangle distance through `0x422850`. If the next waypoint equals the source position, the angle test is skipped; otherwise `0x41fe60` computes target heading through original native direction arithmetic and accepts the smallest wrapped difference when it is at most the stock attack angle.

Nine Marine calls use target heading 64 and stock angle **16**. Source headings **48,64,80** pass, **47,81** fail, and 0/128/200/255 fail. This establishes the inclusive angle boundary rather than a guessed normalized floating-angle test.

Stock Marine extents are `[left=8, up=9, right=8, down=10]`. Two horizontally aligned Marines at center separation `d` have native rectangle distance `max(0,d-17)`. Eight calls use synthetic weapon minimum 10 and stock maximum 128. At separation **27** the gap is 10: weapon readiness accepts it, but `0x421060` in-range rejects it. At separation 28 both accept; at 145 both accept the maximum gap 128; at 146 both reject. The native readiness check rejects **distance < minimum**, while this in-range check rejects **distance <= minimum**. A single shared predicate would erase that observed distinction.

## Cooldown modification, wait scheduling and jitter

The actual `0x41fb70` formula, before firing jitter, is:

```text
value = stockWeaponCooldown
value += acidSporeCount * max(3, value >> 3)
modifier = (stimTimer != 0) + (status & 0x20000000 != 0)
           - (ensnareTimer != 0)
if modifier < 0: value += value >> 2
if modifier > 0: value >>= 1
value = clamp(value, 5, 250)
```

Two positive speed sources still halve once; they do not quarter the cooldown. The spore term precedes the speed adjustment and clamp. The 84 stock and 27 synthetic-boundary cases independently check this integer formula. Synthetic spore count 255 and overridden stock cooldown bytes establish clamps, not ordinary attainable Acid Spore states.

`0x41f530` returns zero for weapon sentinel 130. When the chosen cooldown byte is nonzero, it applies:

```text
orderWait = min(orderWait, chosenCooldown - 1)
```

It does **not** decrement that cooldown, consume RNG or install an animation. All 32 crossed cooldown/wait fixtures verify this. Cooldown decrement belongs to the earlier unit timer service. An implementation that decrements once here and again in the unit step would fire too quickly.

When ready, it sets Flingy flag 8, calculates the modified cooldown, calls the actual generator `0x478200` with source **12**, and adds:

```text
jitter = (nativeRandom15bit & 3) - 1       # -1, 0, +1, +2
```

The chosen cooldown is stored as a byte and copied to the **other** ground/air cooldown byte for selectors 1 or 2. There is no post-jitter clamp. It then calls actual `0x417220(sprite, animation, force=1)` and returns 1. Twelve full AttackUnit cases choose creation seeds so that all four next source-12 residues occur after the independently checked native creation draws.

| Stock selected weapon | Base | Native firing cooldowns before other effects |
|---|---:|---|
| Marine ground, weapon 0 | 15 | 14,15,16,17 |
| Wraith ground, weapon 16 | 30 | 29,30,31,32 |
| Wraith air, weapon 15 | 22 | 21,22,23,24 |

## Complete object callback: repeated stock attacks

Three cases execute original `0x445070` 48 times with native-created, completed, visible units, real stock populated images and actual AttackUnit order **10** on the attacker. Both units are owner 3/controller 2 with null AI pointers. Victim fixture status **`0x400`** suppresses its primary-order decisions while leaving actual timers/images active. The attacker heading is 64. This keeps the enemy-reaction composition separate from the repeated object-callback timing cases.

The selected world fixture is 128×128 tiles with empty contour geometry and native sorted unit borders. Creep countdown `0x6c46d4`, two slow-AI countdowns `0x69b568/0x69b564`, and FOW countdown `0x6663e4` start at **200**. The AIScript list, resource-area list and auxiliary Flingy list are empty. Regional scheduling starts at player 0/region 1 with four regions; it performs actual cursor sweeps and reaches the real nonzero-player enemy-strength-rebuild guard when wrapping. A positive all-player-zero strength rebuild is not claimed.

Each callback reaches original unit timer service before the main primary-order dispatcher; populated unit images then run before the positive Bullet-list consumer, followed by Thingys. Cooldowns therefore decrement before AttackUnit's gate, while a Bullet emitted in the image phase can be updated and hit during the same callback's later Bullet phase. The native order-wait byte and its cooldown-dependent shortening operate together with the primary-order scheduler. The observed firing dates cannot be modeled as one universal attack attempt every eight callbacks.

With initial RNG seed **12345**, the exact timelines are:

| Stock attacker/target | Cooldown installations: callback → value | Damage callbacks | Target HP after callback 48 |
|---|---|---|---:|
| Marine → Marine | 1→14, 15→14, 29→14, 43→14 | 3,15,29,43: each 6 HP | 16 |
| Wraith ground → Marine | 1→31, 32→30 | 2,33: each 8 HP | 24 |
| Wraith air → Wraith | 1→21, 22→22, 44→23 | 7,28: each 20 HP | 80 |

At callback 48 the paired ground/air cooldowns are respectively **9/9**, **14/14**, and **19/19**. The third Wraith air projectile is still flying; its creation is not counted as another impact. Animation 5/6 repeat timing differs from the earlier projectile report's externally forced initial animation 2/3 and random heading. Both reports retain their exact caller conditions.

The ground stationary path executes actual movement states and helpers `0x42b1b0`, `0x42ca80`, `0x42b240`, `0x42eab0`, `0x42e130`, unit bounds and native spatial query/pair eligibility, plus the imported authentic terrain-fit/collision helpers. The querying unit is excluded by the original predicate. Empty contour geometry and nonoverlapping unit placement are explicit; there is no stubbed clear-path return and no claim of blocked moving-ground pursuit. Wraith's selected airborne state executes its original consumer.

All four scheduler countdowns become **152**, the AttackUnit remains **10**, RNG is disabled by the callback on return, and current CUnit/Flingy/Bullet context globals are cleared. The accepted-frame counter increment, trigger phase and external main-loop phases are outside this entry and are not executed. [Frame contract](native-frame.md).

## Actual periodic timer cadence and expiration

`0x43a320` first decrements nonzero order, ground, air and spell cooldown bytes at `+0x54..+0x57`. Shield-enabled units receive **7/256 shield HP** per call, capped at the stock maximum, with native dirty-image handling when required. It also clears the is-being-healed byte `+0x107`.

Spell-service eligibility is:

```text
if unitCompleted or not spriteHidden:
    masterSpellTimer++
    if masterSpellTimer >= 8:
        masterSpellTimer = 0
        progressSpellTimers(unit)         # 0x4351e0
```

The call site is **`0x43a3d7`**, not `0x430a8f`. The 24 complete timer cases cover phases 0..7 for completed-visible, incomplete-visible and incomplete-hidden units. Positive Stasis, Stim, Ensnare, Lockdown, Maelstrom and Storm timers start at 2. On phase 7 the first two profiles reset the master byte to zero and decrement all six timers to 1; other phases preserve them. Incomplete-hidden units preserve the master counter and the timers. The missing completed-hidden quadrant follows the static OR branch but is not a separate executed case in this batch.

The service's native order and callbacks are preserved in the static witness:

| Timer byte | Native periodic action |
|---|---|
| `+0x119` Stasis | Decrement; zero calls `0x4310f0`. |
| `+0x115` Stim | Decrement; zero calls speed recalculation `0x43aea0`. |
| `+0x116` Ensnare | Decrement; zero calls overlay removal/speed recalculation `0x43dee0`. |
| `+0x114` Matrix | Decrement; zero passes remaining matrix WORD to `0x4384c0`. |
| `+0x118` Irradiate | Decrement, then `0x4385d0` damage even on the final service; zero calls `0x438620`. |
| `+0x117` Lockdown | Decrement; zero calls `0x4386e0`. |
| `+0x124` Maelstrom | Decrement; zero calls `0x438730`. |
| `+0x11a` Plague | Decrement, then `0x43ddb0`; zero calls `0x43ddf0`. |
| `+0x11b` Storm | Decrement. |
| `+0x127..+0x12f` Acid Spore slots | Tail into `0x43e280`, with count at `+0x126`. |

The positive expiry cases here cover **Stim, Ensnare and both**, on stock Marine/Wraith. The real speed consumer `0x43aea0` uses native Flingy speed/acceleration/turn helpers for ordinary movement control; Marine's IScript movement control instead reaches complete `0x42dff0`, which runs stock Walking bytecode with the real VM and restores its context. Selected Ensnare overlays are absent, but the actual removal traversal and speed recalculation execute. Matrix and Irradiate final-service effects are independently closed in [spell effects](native-spell-effects.md); the other zero-expiry/positive spore/plague callbacks in the table are a static dependency inventory, not claims of execution in these six cases.

Completed-unit continuation also contains HP regeneration, energy progression, target-position refresh, timed-life decrement/Death issuance and Terran burn logic. Those remain subject to their own native gates; the table above must not be used to advance every timer identically or on hidden incomplete units.

## Strength-cache correction and Hallucination

All 24 positive periodic-timer cases leave deliberately supplied `0xabcd` strength-cache WORDs unchanged. Two stock Zealot tests, phases 0/7, start shields at `60×256−10` and finish at `60×256−3`; both retain the cache sentinels and record **zero calls to `0x4a8f30`**. Neither passive shield regeneration nor the ordinary periodic service implies blanket automatic cache recomputation.

The earlier interpretation of `0x430a8f` as a periodic refresh was incorrect. Its containing complete routine is **`0x430a00` Hallucination conversion**. The actual instruction sequence updates unit accounting/buttons, sets status **`0x40000000`**, selects a timed-life duration through `0x434a90`, calls the strength refresh at `0x430a8f`, and issues secondary order **149 (`0x95`)** through `0x42a1e0`. Existing nonzero shorter timed life is retained; the selected previously untimed objects receive **1350**. Static continuation recursively handles subunits and selected hangar types.

Two full conversion probes use stock-created Marine and Wraith at full HP, with explicitly synthetic type-strength inputs air 100/ground 80. Both finish with the Hallucination bit, timed life 1350, secondary order 149 and refreshed cache words **100/80**. These cases prove the conversion call's identity and real cache producer; they do not establish all recursive turret/hangar conversion branches or the original producer of the synthetic base-strength table. The secondary dispatcher recognizes order 149 before ordinary disabled-unit gates and routes to `0x430c70`.

Surviving combat damage retains its separately established native strength refresh. HP assignment/regeneration, type changes and individual spell effects have their own producer paths. [Unit strength/cache specification](native-unit-ai.md), [damage](native-damage.md). A faithful port should reproduce those producers and their timing instead of refreshing both cache words whenever any health or status field changes.

## Reproduction and remaining compositions

The exact original-code probe invocation is:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_attack_orders_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-attack-assets \
  --asset-manifest docs/research/evidence/native-attack-orders-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out /private/tmp/native-attack-all.json \
  --case-group all --execute-original-code
```

Without the execution flag, the probe prints its plan and does not run original code. The full asset manifest, DAT hashes, allowlist, source dependencies, inputs/outputs, call arguments, VM bytecode/context, projectile initialization/impact events and timelines are preserved in the generated evidence.

The closed contracts cover selected ordinary ranged AttackUnit paths, auto-acquisition and actual cooldown/timer/object ordering. Remaining compositions include computer-controller/nonnull-AI AttackUnit reactions; all AttackMove/Hold/Suicide/melee-specific main orders; continuous blocked ground chase and genuinely moving-target pursuit; every status-expiry/Acid Spore branch; more weapon/projectile families; and authentic complete campaign frame data. The highest-value next probes are computer-AI AttackUnit with its real guard/regional metadata, path-produced chase through obstacles, and selected positive expiration callbacks attached to the same object scheduler. None requires treating a fixture or an inventory as full-game compatibility proof.
