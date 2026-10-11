# Native 1.17 ranged weapons and projectile lifecycle

This batch connects stock Marine, Wraith and Goliath attack IScript to actual Bullet allocation, populated sprites/images, projectile movement, impact, native damage and recycling. It executes the positive Bullet-list consumer `0x40a5f0`, extending [active-unit/IScript ordering](native-active-iscript.md), [image initialization](native-image-initialization.md), [damage](native-damage.md) and [visible Death](native-unit-death.md). Their published tools are unchanged.

The target is the exact local **1.17.0.1** executable, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. All addresses below are preferred virtual addresses at image base `0x400000`; subtract that base to obtain RVAs. No 1.16.1 address is assumed to apply.

## Evidence and reproducibility

The dedicated files are:

- [Static extractor](../../tools/research/inspect_native_projectiles_117.py) and [static evidence](evidence/native-projectiles-static.json): **32** complete instruction ranges, **1,883** decoded instructions, no undecoded tails; native dispatch tables, direction vectors, tangent table and DAT descriptors.
- [Original-instruction probe](../../tools/research/probe_native_projectiles_117.py) and [probe evidence](evidence/native-projectiles-probes.json): **245** passing cases with complete original entry returns, **zero prefixes**, **zero helper stubs**.
- **Separate new asset provenance:** [native-projectiles-assets.json](evidence/native-projectiles-assets.json), identifying **29 image IDs / 26 distinct GRP paths**, their archive source, bytes, SHA256 and required LO inputs. The earlier manifests remain unchanged. Raw retail files stay in private scratch storage.

| Cases | Executed scope |
|---:|---|
| 33 | Marine ground, Wraith ground and Wraith air stock animation chains; weapon/armor levels 0/1/3, hallucinated source and invulnerable target. Each runs 48 image/Bullet/Thingy steps through damage and reclamation. |
| 3 | Stock Goliath turret firing two missiles, stationary and repositioned Wraith targets; a Wraith point missile against a repositioned Wraith. |
| 36 | Native Marine Bullet creation and impact at low-byte miss draws 0/1/118/119/120/255, cover on/off and ground/air flag combinations. |
| 5 | Actual 100-record initializer; sprite/image exhaustion; the shared 80-Bullet Halo Rockets limit; 100 populated successful Bullet allocations and the next failure. |
| 3 | Complete `0x445070` object steps that create/update a positive populated Bullet list under explicitly declared unit-order and scheduler gates. |
| 165 | Real stock-created laser Bullet fixture: clamped acceleration/velocity over 13 directions and twelve boundary inputs, plus nine map-boundary clamps with actual sprite movement. |

The host is x86_64 via Rosetta, Unicorn **2.1.4**, pefile **2024.8.26**. Each invocation uses a reviewed-code allowlist and a 100,000-instruction bound. Every completed call reaches its sentinel and checks stack cleanup. The evidence records actual call arguments, VM bytecode offsets/image IDs, initialization contexts, impact contexts, pool/list state and all fifty RNG-source counters. The latter are checked against an independent exact-integer LCG. [RNG specification](native-prng.md).

Private memory supplies stock decoded DAT, IScript, GRP and LO data, finite object pools, terrain/FOW arrays and player fields. Native routines create and maintain the selected objects. The numeric group deliberately reuses one original-created Bullet and sets the scalar fields stated by each fixture; its records repeat that shared initialization trace. The cap cases likewise share their progressively populated pool. Summed recorded trace lengths are therefore not a count of distinct physical host executions.

No WinMain, whole game, original renderer, Windows import, file/network operation or substituted callee executes in the guest. Authentic assets and successful simulation instructions do not establish rendered pixels or a running campaign.

## Caller contracts and data

| Entry | Exact selected caller contract | Result |
|---|---|---|
| `0x41f5d0` | ECX = firing CUnit; stack: selector; selector 1 chooses ground, other values choose air | `ret 4`; fires again when stock damage-factor byte is exactly 2. |
| `0x420030` | ECX = firing CUnit; stack: weapon ID | `ret 4`; derives spawn coordinates, then invokes allocation. EAX is not a meaningful success contract. |
| `0x408f00` | ECX = weapon; EDX = x; stack: y, owner, heading, spawner CUnit | `ret 16`; Bullet pointer on success, zero on failure. |
| `0x409d70` | ECX = reserved Bullet; stack: weapon, x, y, owner, heading, spawner | `ret 24`; success/failure. Executes real Flingy/sprite/image initialization. |
| `0x4178e0` | ECX = generic Flingy; stack: Flingy ID, x, y, owner, heading | `ret 20`; initializes common order fields after sprite/Image Init. |
| `0x40a0e0` | No arguments | Initializes the actual fixed 100-record Bullet pool. |
| `0x40a5f0` | No arguments | Updates the actual active Bullet list and clears Bullet/Flingy context globals. |
| `0x40a6d0` | ECX = Bullet | Dispatches the same primary states from Init's immediate continuation. |
| `0x4090e0` | ECX = Bullet | Dispatches weapon explosion/hit behavior. Normal hit reaches actual `0x41e290`. |
| `0x41e290` | ECX = target CUnit; stack: Bullet, damage divisor | `ret 8`; ordinary damage or hallucinated-hit effects. |
| `0x41fd10` | ECX = source CUnit; stack: target CUnit | `ret 4`; integer miss threshold. |
| `0x420970` | ECX = source CUnit; stack: output-x pointer, output-y pointer | `ret 8`; target's clipped inner-rectangle point. |

A Bullet occupies `0x70` bytes. Relevant fields are: sprite `+0x0c`; move target `+0x10/+0x12`, next target `+0x18/+0x1a`; heading `+0x21`, next velocity direction `+0x23`; pixel coordinates `+0x28/+0x2a`, raw fixed coordinates `+0x2c/+0x30`; maximum/current speed `+0x34/+0x38`; owner `+0x4c`; primary state `+0x4d`; signal `+0x4f`; order target position `+0x58/+0x5a`, target pointer `+0x5c`; weapon `+0x60`, remaining-time byte `+0x61`, flags `+0x62`, bounce count `+0x63`, attributed source pointer `+0x64`, previous target `+0x68`.

| Stock shot | Weapon | Behavior | Base/upgrade bonus | Lifetime | Flingy / Sprite / main Image | Speed / acceleration / turn |
|---|---:|---:|---:|---:|---|---|
| Marine | 0 | 2: appear at target | 6 / 1 | 255 | 142 / 341 / 527 | 0 / 0 / 0; IScript movement control 2 |
| Wraith ground laser | 16 | 0: move to point | 8 / 1 | 255 | 149 / 348 / 535 | 17067 / 8533 / 127; control 1 |
| Wraith air missile | 15 | 0: move to point | 20 / 2 | 255 | 144 / 342 / 528 | 8533 / 667 / 40; control 1 |
| Goliath turret air missiles | 8 | 1: follow target | 10 / 2 per missile | 30 | 146 / 345 / 529 | 8533 / 267 / 13; control 1 |

Speeds and fixed coordinates are raw native integers, not displayed pixels. The evidence also preserves complete selected stock weapon/Flingy/Sprite rows. Behavior and explosion type are separate DAT columns: all four selected weapons use normal explosion/hit type 1.

## Pool allocation, failures and initial order

`0x40a0e0` clears **100 × 0x70 = 0x2bc0** bytes beginning at `0x53db90`, clears allocated count `0x53db8c` and active head/tail `0x540750/0x540754`, and builds free head/tail `0x540758/0x54075c` with original list helpers. Its exact free-list order by physical record index is:

```text
0, 99, 98, ... , 2, 1
```

The first record remains the head while each new record is inserted immediately after it. This is established by the native initializer, not by an ascending Python-built Bullet list. Successful `0x408f00` removes the chosen free record, inserts it immediately after the active head when one exists, and increments the count. Thus list position is neither ordinary append order nor a universal push-front rule. The positive update consumes that native list and saves each next pointer before updating the current Bullet.

All 100 Marine Bullet allocations execute actual stock populated Image 527 initialization. The 101st returns zero without consuming RNG or changing the active count, remaining pools or list head. Weapon **103, Halo Rockets**, has an additional `allocated >= 80` rejection, based on the **shared** Bullet count: 80 existing Marine bullets reject a Halo Rockets request despite twenty free Bullet records. Successful Halo Rockets flight/splash is outside this batch.

Sprite exhaustion fails before Bullet insertion. Image exhaustion allocates a sprite, fails its actual image initialization, returns it through the original list helper, and returns zero without allocating a Bullet. This **changes the free-sprite reuse order**: the first two free sprites are exchanged in the tested six-free-sprite profile. A remake that treats this failure as leaving every pool byte unchanged would disagree with the original.

`0x408f00` also rejects ordinary weapons from a spawner with status `0x8000`, using its native weapon allowlist. The table is extracted, but this batch does not claim all positive allowed spell-weapon paths.

## Initialization context and IScript signals

Image Init runs inside `0x4178e0 → 0x40c2b0 → sprite/image initialization`, **before** the generic owner/order/signal fields and the weapon-specific Bullet fields are finalized. The VM still has the firing unit as both current CUnit and current Flingy, and the current Bullet global is zero. The recorded initialization contexts show this directly for all selected weapons, including a Goliath turret.

Wraith laser Image 535 makes this distinction observable. At stock bytecode offsets **2882/2883/2888**, `tmprmgraphicstart` (50), `trgtrangecondjmp` (58) and `tmprmgraphicend` (51) execute during creation with the firing **Wraith** as current CUnit. The range conditional uses the firing unit's target and actual `0x420970 → 0x417aa0` rectangle/distance calculation. The image's temporary flag `0x40` is set/cleared through real `0x40f890/0x411820`, with original dirty-rectangle handling. The VM's generic context must not be prematurely replaced with the partially initialized Bullet. [Teippi initialization context](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/bullet.cpp#L218).

After initialization, `0x409d70` sets weapon/lifetime, movement flag 8, zero hit flags and generic primary state 0. It calls Init once with signal zero, which returns without changing state. Later, the stock image emits `sigorder 1` in the first Bullet update; this signal reaches the **Bullet's** `+0x4f` because the current Flingy/Bullet globals now refer to it. For Image 527 this occurs at bytecode offset **23396**. Image 535 uses offset **2897**, Image 529 **22770**.

The creator sets source `+0x64` to the parent for a DAT subunit and to the Reaver parent for a Scarab; otherwise it retains the spawner. Hallucinated-spawner status `0x40000000` sets Bullet flag **2**. Goliath's two real missiles are produced with the turret as VM/spawner context, but both attributed source pointers equal the genuine body CUnit.

## Positive Bullet step and state machine

For every active Bullet, `0x40a5f0` performs, in order:

1. Save the native list's next pointer.
2. Set current Flingy `0x543304` and current Bullet `0x543308` to this Bullet.
3. Run actual visibility `0x417e50` and populated-image step `0x418b20`.
4. Dispatch the current primary-state byte after those image/VM effects.
5. Continue through the saved next pointer; finally clear the two globals.

It **does not set or clear current CUnit `0x543300`**. The full object caller has already cleared that global before entering the Bullet phase. Isolated Bullet-step calls explicitly reproduce that caller condition. Every recorded impact here has current CUnit zero and both other contexts equal to the impacting Bullet.

| State | Real native consumer | Selected behavior |
|---:|---|---|
| 0 | `0x40a8c0` | Wait for signal bit 1, clear it, select the state/animation and immediately dispatch the new state. |
| 1 | `0x409990` | Move to the saved point with actual Flingy motion; decrement lifetime; select Death on arrival or expiry. |
| 2 | `0x409aa0 → 0x409990` | For a nonmiss with a target, update movement target from the target sprite using actual `0x40cc40`; then move. |
| 3 | `0x409830` | Bounce; extracted dispatch target, not a completed positive path in this batch. |
| 4 | `0x409a30` | Ground damage over time; extracted dispatch target, not completed here. |
| 5 | Inline in `0x40a5f0`; `0x409950` in tail dispatch | Keep the Bullet until its sprite disappears; then unlink it, return it to the free pool and decrement allocated count. |
| 6 | `0x409b00` | Move near target; extracted dispatch target, not completed here. |

Init maps stock behavior 2 to Death state 5 and behavior 0 to point state 1; behavior 1 selects follow state 2. It forces the applicable animation through real `0x417220`, then enters `0x40a6d0` immediately. Consequently both the newly selected movement and a Death-animation impact can happen in the same Bullet update that consumes `sigorder 1`.

The moving-point helper runs `0x40cdc0`: calculate desired direction, speed/acceleration, walking-animation gate, prepared fixed position, then commit through original `0x40bdb0`. It sets remaining time to zero when the pixel position lies outside map bounds, applies actual `0x40bae0` map clamp, and performs a **byte post-decrement**. An old value of zero expires after that movement; ordinary arrival also selects Death. The Death animation/VM determines the hit and the later sprite lifetime, not a separate immediate deletion on arrival.

## Native movement arithmetic

The table at `0x5026b0` contains 256 integer direction vectors scaled by 256. Heading 0 points up, 64 right, 128 down and 192 left. Desired heading uses signed integer division and native binary search `0x40b940` over the tangent table; turning uses the real native Flingy helpers.

For the tested nonoverflow acceleration inputs, `0x40bd10` computes:

```text
newSpeed = min(maxSpeed, max(0, currentSpeed + delta))
velocityX = (directionX * newSpeed) >> 8
velocityY = (directionY * newSpeed) >> 8
```

The velocity shifts are **arithmetic**, including negative components. The 156 cases verify values just below/at/above maximum, exact zero, negative deceleration, and thirteen headings. Native instructions use 32-bit multiplication/addition; this simple scalar formula must not be generalized to malformed signed-overflow inputs without preserving native wrap/comparison behavior.

Map clamp uses signed WORD positions and the map's WORD dimensions, clamps to `[0,width-1] / [0,height-1]`, recomputes fixed coordinates by shifting eight bits, and executes real sprite relocation when clamping. Nine complete calls verify the four edges, two corners and unchanged in-bounds positions. These are Flingy movement rules; terrain collision, ground CUnit pathfinding and a complete trigger Move pipeline are separate. [Movement collision](native-movement-collision.md), [grounded Move](native-trigger-grounded-move.md).

## Miss threshold and spawn point

`0x41fa00` returns 0 for an airborne target; otherwise the tested cover tile flag **0x100000** yields **119**, with zero when absent. `0x41fd10` starts with that target threshold. When both units are ground and source height is lower than target height, it raises the threshold to at least 119. Native height `0x445690` reads the selected MegaTile/VF4 subtile, with flag 4 producing height 2 and flag 2 height 1. [Positive height and Dark Swarm selectors](native-spell-families.md).

The creator then consumes native RNG **source 1**, takes the low byte, and declares a miss for:

```text
(nativeRandom15bit & 255) <= missThreshold
```

The inclusive comparison matters: threshold 0 misses for draw 0, and threshold 119 misses for draws 0 through 119. Expressed over 256 equally represented low-byte values, those thresholds contain **1/256** and **120/256** values respectively; this is the branch's arithmetic, not a statistical measurement of an arbitrary gameplay trace. Draws 0/1/118/119/120/255 are selected through the real generator with known initial seeds, after eight verified stock-unit creation RNG calls. All cover/air combinations reach actual creation and impact.

For Marine's behavior 2, spawn point `0x420970` takes the target's stock inner rectangle, shrinks each edge by one quarter of the width/height, and clips a center-to-attacker line through original `0x48c6d0`. The tested target at `(280,200)` is hit at `(276,200)`. Heading 64 plus a miss shifts this sprite thirty pixels backward to `(246,200)` and sets Bullet flag **1**. Normal hit dispatch then suppresses ordinary damage. Wraith/Goliath moving behaviors shift their saved target position on a miss instead.

Existing Dark Swarm is recovered separately: the complete positive `0x41fa00 → rectangle/spatial query` chain returns **255**, with native overlap/type checks in [spell families](native-spell-families.md). Combined with the creator's proven inclusive low-byte comparison, threshold 255 implies that all 256 low-byte values select a miss. That cross-batch conclusion is derived from the two completed mechanisms; this batch does not claim a populated Dark Swarm field-to-Bullet composition.

## Stock impact, damage and recycling timelines

Baseline objects are stock-created and completed at source `(200,200)` and target `(280,200)`, owner 3/controller 2, RNG seed 12345. The native initialization yields heading **200** for Marine/Wraith; it is retained. A stock attack animation is installed through `0x417220`; the ordinary attack-order decision, legality, range and cooldown selection are outside this caller. A same-owner target intentionally suppresses unrelated AI/notification work.

| Producer | First Bullet update/appearance | Damage step from animation setup | Baseline HP loss | Bullet returned to pool |
|---|---:|---:|---:|---:|
| Marine GroundAttack 2 | 3 | 3 | 6 | Step 18 |
| Wraith GroundAttack 2 | 1 | 3 | 8 | Step 14 |
| Wraith AirAttack 3 | 1 | 10 | 20 | Step 30 |

The laser's first three positions are `(160,158)`, `(226,169)`, `(280,200)`. The air missile turns from its stock initial heading and follows `(177,194)`, `(175,189)`, `(176,181)`, `(183,173)`, `(195,168)`, `(210,170)`, `(227,177)`, `(246,185)`, `(268,194)`, `(280,200)` over ten updates. The evidence preserves raw fixed positions, speeds, directions and populated images as well as pixels.

The stock VM calls `domissiledmg` (27) during Death: `0x4090e0` normal-hit branch tests target pointer and miss flag, then executes actual `0x41e290 → amount/damage pipeline`. Weapon damage uses the Bullet owner for upgrade lookup and the attributed source pointer for reactions. Levels 0/1/3 verify the damage bonus and armor composition against stock Marine/Wraith targets. Hallucinated sources preserve the full visual/Bullet lifecycle and flag 2, but subtract no HP. Invulnerable targets preserve HP. [Exact armor, minimum, shield/matrix and attribution rules](native-damage.md).

Laser impact adds stock Image **447**; air missiles add stock hit Image **530**. Missile movement produces genuine smoke Thingys using stock Image **422**. The isolated composition updates the real Thingy list after the Bullet step, as the object step does. All 48-step Marine/Wraith cases finish with **100 free Bullets, 28 free images, six free sprites and eight free Thingys**, preserving the two live parent units and their four images.

Wraith behavior 0 keeps a saved movement point. After one Bullet step, actual `0x40cc00` repositions the target's Flingy/sprite to `(320,200)`; the missile still arrives at `(280,200)` on step 10 and inflicts 20 HP on its live target pointer. Normal hit dispatch has no distance-to-target test. This fixture executes the native position helper, not spatial border updates or the complete trigger Move action, which can clear targeting references.

Goliath air animation calls the real factor-2 producer and creates **two** stock weapon-8 Bullets. The genuine turret's launch heading is 128; their initialized headings are **192 and 64**. The first nonzero launch-spin producer consumes RNG source 0 once; the consecutive shot from the same spawner flips the previous spin instead of drawing again. Both shots consume miss RNG source 1. The two missiles attribute damage to the body and subtract 10 HP each from a stock large Wraith. They impact on steps **12/19** for a stationary target and **15/20** after the target is repositioned to `(320,200)` after the first update. Both moving-target impacts occur at `(320,200)`, demonstrating the actual follow-state update. The initialized source/body links and impact contexts are recorded.

Native `0x409d70` clears the hit-flag/bounce word `+0x62` and then sets bounce count `+0x63` to **3** only for behavior 7: jump table `0x40a0b0` sends behavior 7 to `0x40a036` (`mov byte [+0x63],3`) and behaviors 0 and 1 to `0x40a03a`, past that store. This matches the pinned OpenBW rewrite, which also initializes it to 3 only for behavior 7, and the recorded probe states (`bounces` 0 for every tested bullet). Corrected after the function atlas ([P02 `0x409d70`](atlas/p02.md)). Normal damage consults the bounce divisor only for behavior 7. [OpenBW initialization](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L14443).

## Complete object-step composition

Three cases execute the entire original `0x445070` call with real positive Bullet creation/update. They supply status **0x400** on both live units so that ordinary primary-order decisions return through their actual gate; they retain the stock populated attack animation and its original VM effects. This fixture bit is disclosed rather than being treated as ordinary legal player attack state.

Marine receives two preparatory image steps; the complete object step then reaches its third image update, creates the Bullet in the active CUnit image phase, and hits in the **same object step's later Bullet phase**. Wraith ground/air cases start at their first stock attack-image update and create/move the real projectile in that later phase. This closes creation-to-consumption ordering under the declared fixture, not the attack-order state machine.

Each call also executes original creep scheduler with countdown 100, script-list scheduler with an empty list, regional sweep starting at player 0/region 1, empty resource-area update, slow-AI countdowns 100, FOW countdown 100, actual active-unit/image phases, positive Bullet step, empty auxiliary Flingy list and Thingy update. All four countdowns become **99**, regional cursor becomes **334**, RNG is disabled on return, and all three current-object context globals are zero. The source-ordered call trace establishes active images before Bullets and Bullets before Thingys. Positive creep recalculation, slow-AI expiry and FOW refresh remain separate selected branches. [Frame order](native-frame.md), [active object composition](native-active-iscript.md).

The surviving damage path reaches actual `0x4a8f30` strength-cache refresh. Hallucination conversion's refresh at `0x430a8f` is **not reached here**. The earlier periodic-refresh interpretation of this address was incorrect; the containing routine is `0x430a00`. Conversion and the actual periodic timer consumers remain separate tasks. [Strength/cache rules](native-unit-ai.md).

## Scope that remains separate

This batch establishes selected stock ranged producers and consumers with actual populated resources and complete original returns. It does not complete all ordinary AttackUnit/AttackMove/hold/range/cooldown decisions; bouncing, ground-over-time or move-near states; every splash/spell explosion type; interceptor/Scarab or Suicide source removal; successful Halo Rockets; target destruction/source removal during flight; all collision/path states; full campaign frame data or rendering/audio output. The extracted dispatch inventory is a map of those dependencies, not a claim that they were executed.

The next distinguishing compositions are ordinary attack-order timing through the stock VM, a moving target whose actual normal movement maintains Bullet references, projectile timeout/target loss, and the remaining positive hit/state families. They can reuse this fixed native Bullet pool, authentic new asset manifest, context protocol, normal impact pipeline and exact boundary fixtures.
