# Native 1.17 damage calculation and attribution

This report recovers the damage calculation of the locally supplied StarCraft executable, version **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. It establishes the actual order of matrix, shields, armor, size scaling, HP changes and reactions. Original x86 instructions, including real callees, were executed in isolated Unicorn memory; the original game process was never started.

The evidence is [native-damage-static.json](evidence/native-damage-static.json) and [native-damage-probes.json](evidence/native-damage-probes.json). The static extractor records 40 instruction blocks, 1,421 instructions, the embedded damage-factor table, preferred VAs, RVAs, file offsets and instruction bytes. No block has an undecoded tail. The probe batch contains **973 passing cases: 963 complete returns and 10 explicit prefixes**. There are no replacement helper stubs. A complete return establishes the behavior for its recorded artificial fixture; it does not establish game-wide, rendering or campaign parity.

The [extractor](../../tools/research/inspect_native_damage_117.py) and [probe](../../tools/research/probe_native_damage_117.py) reject every executable with another hash. Their hashes, the extractor dependency hash, the engine version and all inputs/outputs are recorded in the JSON. The runtime is Unicorn 2.1.4, guest x86-32, running under x86_64 Python on the local host.

## Version-specific anchors and caller contracts

All addresses below belong to this exact local **1.17** image, with preferred image base `0x400000`. Subtract that base to obtain the RVA. Names identify behavior recovered from these instructions; they do not authorize transplantation of 1.16.1 addresses or hook ABIs.

| Function | Local preferred VA | Entry contract | Exit contract |
|---|---:|---|---|
| Weapon damage | `0x41dfc0` | ECX = target; stack arguments: raw damage, weapon ID, divisor, direction, attacker pointer, attacking player | `ret 0x18` |
| HP damage | `0x41e150` | ECX = target; stack arguments: raw damage, attacker pointer, attacking player, notify | `ret 0x10` |
| Bullet damage amount | `0x409010` | ECX = bullet; stack: target | EAX = raw damage; `ret 4` |
| Bullet damage wrapper | `0x41e290` | ECX = target; stack: bullet, damage divisor | `ret 8` |
| Hallucinated weapon hit | `0x41e2f0` | ECX = target; stack: weapon ID, direction, attacker | `ret 0xc` |
| Melee damage | `0x41fc70` | ECX = attacker; target comes from `attacker+0x5c` | `ret` |
| Matrix reduction | `0x4384c0` | ECX = target; stack: raw reduction | `ret 4` |
| Armor upgrade bonus | `0x43ab10` | ECX = unit | EAX = upgrade bonus; weapon caller consumes AL |
| Damage type/size scale | `0x43eb20` | ECX = raw damage, DL = damage type; stack: size | EAX = scaled damage; `ret 4` |
| On-damage reaction | `0x4203d0` | ECX = target; stack: attacker, notify | `ret 8` |
| Kill count | `0x41fc40` | ECX = credited unit | `ret` |
| Death statistics | `0x414080` | ECX = victim, EDX = attacking player | `ret` |

The GPTP 1.16.1 weapon hook takes the target in EDI and damage in EAX; its HP hook takes damage in EAX. **Neither ABI matches the native 1.17 entries above.** The local stack ordering is verified directly at the original call sites `0x41e2df`, `0x41fd04` and `0x41e119`.

HP and shield amounts are integer raw values with **256 raw units per displayed point**. Matrix HP is a 16-bit raw quantity, while ordinary HP and shields occupy 32-bit fields. The weapon caller uses the low byte of weapon ID and direction; its divisor is a full 32-bit stack argument. A zero divisor reaches the native `div` instruction without a guard, so callers must supply a nonzero divisor. The normal producer wrappers supply positive divisors.

## Exact weapon pipeline

The following sequence describes native `0x41dfc0`, rather than a reimplementation inferred from visual gameplay. `D` is raw damage, `S` current raw shields and `M` raw matrix HP.

1. Return immediately if target HP is zero or target status contains Invincible `0x04000000`. No matrix, shield, image or strength changes occur in these cases.
2. If cheat flags at `0x5432fc` contain bit 4 and the supplied attacking player's type at `0x6726d0 + player*36` is **not 2 (Human)**, set `D=0`.
3. If the target is a hallucination, status `0x40000000`, double `D` with 32-bit wrap.
4. Compute `D = unsigned(D)/divisor + acidSporeCount*256`, preserving 32-bit arithmetic. The division at `0x41e01b` is unsigned `div`, not signed `idiv`. Acid spore count is byte `target+0x126`; its addition happens **after** division.
5. Using a signed comparison, replace values below 128 with 128.
6. If `M!=0`, set `A=min(D,M)`, subtract `A` from `D`, and call the real matrix reducer with `A`.
7. If units.dat enables shields and signed `S>=256`, process shields. For all damage types except IgnoreArmor 4, compute plasma shield armor `P=upgradeLevel(player,15)*256`. If `P>=D`, set `D=128`; otherwise subtract `P`. Then set `shieldDamage=min(D,S)` and subtract it from `D`. **Current shields are still unchanged at this stage.**
8. For every damage type except 4, compute `armor=(baseArmor + lowByte(armorUpgradeBonus))*256` and subtract `min(D,armor)`. This clamps the residual to zero before size scaling.
9. Apply the embedded damage type/size factor. The exact arithmetic is `D=low32(D*factor)>>8`, with a logical right shift.
10. If `shieldDamage==0` and signed `D<128`, set `D=128` again.
11. Call HP damage with attacker, attacking player and `notify=(weaponID!=34)`. Weapon 34 is Irradiate.
12. After the HP-damage call returns, subtract `shieldDamage` from current shields. If it was nonzero, damage type is nonzero and shields remain nonzero, request the shield impact image.
13. Refresh the target's cached air and ground strength through original `0x4a8f30`.

The instruction witnesses distinguish the first minimum at `0x41e020`, matrix at `0x41e02a`, shield interception at `0x41e052`, armor at `0x41e0b6`, scaling at `0x41e0ea`, second minimum at `0x41e0f2`, HP call at `0x41e119`, shield commitment at `0x41e122`, and final strength refresh at `0x41e140`.

Shield commitment after HP damage is part of the contract. The inner HP-damage call refreshes strength using the still-uncommitted shields; the weapon function's final refresh corrects the cache after shield subtraction. Reordering shield subtraction before HP damage would also change what on-damage/death reactions observe.

### Embedded damage factors

The native table begins at `0x50323c`. Each row is five 32-bit values: damage type followed by four factors. The helper indexes from `0x503240` as `damageType*5 + size`.

| Damage type | Independent size 0 | Small 1 | Medium 2 | Large 3 |
|---|---:|---:|---:|---:|
| Independent 0 | 0 | 0 | 0 | 0 |
| Explosive 1 | 0 | 128 | 192 | 256 |
| Concussive 2 | 0 | 256 | 128 | 64 |
| Normal 3 | 0 | 256 | 256 | 256 |
| IgnoreArmor 4 | 0 | 256 | 256 | 256 |

Divide the table values by 256 for their ordinary multipliers. Armor subtraction precedes this factor. For example, 3 points of explosive damage against one armor and a small target becomes `(3-1)*0.5=1` HP damage. Fractions remain in raw units rather than being rounded to displayed HP.

All 20 type/size combinations were executed both as complete weapon calls and as native factor-helper calls. The factor helper also ran zero, fractional and overflow inputs. Its low-32-bit multiplication and logical shift are relevant to exact integer semantics; malformed negative raw values must not be interpreted as a signed floating-point division.

### Verified edge cases

| Recorded fixture | Native result | Consequence for the remake |
|---|---|---|
| Matrix 512, incoming 512, no shields | Matrix becomes 0; HP loses 128 raw | The final minimum produces a 0.5 HP leak even when matrix absorbs the entire pre-armor amount. |
| Matrix absorbs everything; usable shields; non-IgnoreArmor | Shield armor comparison resets residual to 128; shields can lose 128 | Matrix absorption does not guarantee zero shield loss. |
| Matrix absorbs everything; usable shields; IgnoreArmor | Shield residual stays zero; final HP minimum applies | IgnoreArmor also skips the shield-armor minimum reset. |
| Shields 255, enabled | Shields remain 255; HP processes the hit | Less than one shield point does not intercept weapon damage. |
| Shields 256, enabled | Shields intercept the hit | The threshold is inclusive. |
| Armor absorbs all residual; no shield damage | HP loses 128 raw | Armor and size factors do not suppress the final 0.5 minimum. |
| Shields absorb the whole amount | HP damage can be zero | The final minimum is conditional on zero shield damage. |
| Cheat bit 4; attacking player type 1 | HP damage becomes zero; matrix/shield changes may remain | Early cheat zeroing does not remove later minima/spore effects; the lower HP layer zeroes damage again. |
| Cheat bit 4; attacking player type 2 | Ordinary damage remains | The test concerns the supplied attacking player's type, not the victim's owner. |
| Acid spores 1; incoming 9 raw divided by 9 | Before other defenses, 257 raw | Spores are added after division. |

The probe fixtures include matrix values around 128, 256 and 512; shield values around 256 and exact depletion; plasma armor 0/1/3; all damage types; base armor around minimum thresholds; hallucinated targets; selected-image refresh; disabled shields; and final cache values. These are original-instruction results, independent of whether a source rewrite contains the same formula.

## Armor and expansion rules

Base armor comes from byte array `0x521d90`; armor upgrade ID from `0x521bc0`; size from `0x521ca8`. Upgrade lookup uses original `0x49a470`, including the separate base and expansion upgrade tables.

`0x43ab10` checks expansion flag `0x536720`. Without expansion it returns the normal armor upgrade level. With expansion, unit IDs **39 (Ultralisk)** and **48 (Torrasque)** receive an extra 2 when either their units.dat Hero flag `0x40` is set or their Chitinous Plating upgrade, ID 52, is nonzero. Other unit IDs do not receive this bonus.

The Chitinous test is boolean: level 1, 2 and 255 all add **exactly 2**. The normal armor upgrade is then added in EAX, but the weapon caller zero-extends **AL**. Thus a synthetic upgrade level 255 plus Chitinous returns 257 from the bonus helper, while the weapon calculation consumes 1. This byte boundary is verified by the whole Ultralisk/Torrasque pipeline cases. The final sum of base armor and consumed bonus is wider than a byte.

Teippi's pinned `GetArmorUpgrades` uses `2*ChitinousLevel` for a nonhero Ultralisk. It agrees with ordinary level-1 play, but it does not define the native multi-level behavior. [Teippi source](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L1233)

## Defensive Matrix and visual side effects

Native matrix reducer `0x4384c0` accepts an integer raw reduction. For a partial reduction it subtracts from 16-bit `target+0x112`. On exhaustion it sets matrix HP and timer byte `target+0x114` to zero, removes image IDs `0x173..0x175` through the unit/subunit overlay search, and separately searches the target sprite for `0x176..0x178`.

If the timer remains nonzero and the unit is not burrowed, the reducer chooses image `0x179 + sizeClass` through `0x428d90`, then requests an impact overlay. That size class is derived from units.dat property bits `0x02000000` and `0x04000000`; it is separate from the damage-size byte. The burrow bit suppresses this image request while retaining matrix subtraction.

The complete pipeline probes run the actual reducer, actual linked-image search and actual image allocation failure branch. Their image list contains a valid artificial main image with ID 0, so searches for matrix images legitimately find none. For an impact request, original allocator `0x40eba0` sees an empty image pool and returns zero; the original creation function handles the failure and returns. No call is replaced with a fake success.

Shield image direction uses `frame=((uint32(uint8(direction)-124))>>3)&31` at `0x422fe0`, then tail-calls `0x415370`. All 256 direction bytes were executed. The shield-image helper reads real artificial LO offsets before attempting allocation; the fixture supplies valid 32-frame offsets and an empty pool. Successful image allocation, initialization, iscript, drawing and list removal of an existing matrix overlay are not certified by this batch.

Selected sprites are a separate real effect: sprite flag `0x08` makes HP damage call `0x416bb0`. It walks every image and sets image flag bit 1 when the drawing-function byte is 11. The selected-image fixture confirms this mutation. Completed units additionally inspect the main image's damage-overlay descriptor at `0x516d38`; nonzero entries call `0x422dd0`. The arithmetic probes supply a zero descriptor. The static report preserves that call and its overlay algorithm, without claiming that all building damage visuals were executed.

## HP damage, reactions and attribution

Direct `0x41e150` returns only when HP is zero. **It has neither the weapon function's invulnerability gate nor its minimum-damage rule.** Complete direct calls with invincible status and raw amounts 0, 1, 127, 128, 129 and 2560 confirm that the lower layer subtracts the supplied raw amount. A faithful remake must therefore distinguish weapon damage from direct HP damage rather than adding universal guards to one shared entry.

The HP layer marks selected images, then calls `0x4203d0` for a nonnull attacker **before** its second cheat check and before HP subtraction. A zero-damage result can still cause reactions. The supplied attacking-player argument controls cheat and score decisions; the attacker pointer's owner controls on-hit ownership and notification comparisons.

The native reaction returns immediately for a null attacker or the same owner. For another owner, it writes `target+0x90 = attacker.owner`. If a target has an AI pointer and no auto-target, it assigns the attacker to `target+0x7c` and propagates these fields to a valid subunit. It then calls military/AI help `0x4aebc0` before vision or ordinary retargeting.

When notify is true, the attacker is detected and the target owner is below 8, the reaction calls temporary reveal `0x46bbb0` at the attacker's sprite position. Undetected attackers suppress this reveal through actual `0x40a180`. An attacker flagged InBuilding `0x20` is subsequently replaced by its connected unit at `+0x80` for targeting. `0x4212b0` decides whether to replace the current target; a positive answer reaches `0x420190`. Targets without an AI pointer additionally search a 96-pixel rectangle for nearby allies and invoke their reaction.

Complete attribution cases cover same-owner early return, Irradiate's false notify, native auto-target/subunit writes, a hidden attacker, notification suppression and the empty-neighbor spatial search. For the AI-preassignment cases, the fixture explicitly combines a human player type with an artificial nonnull AI pointer, making the real military-help gate return. This proves the indicated branch and field propagation; it does not prove a computer-controlled military reaction. Additional human-unit cases use no AI pointer, an existing attacker target and real empty spatial indices, including the original search/release bookkeeping.

Attack notification `0x446f00` returns for a suppressed mode or a victim owned by another local player. For a matching local player it first writes notification timer `target+0x8a=20` and frame byte `target+0x8b=globalFrameByte`. A nonzero notification cooldown suppresses subsequent work. With zero cooldown, an in-camera target also returns through actual `0x422c20`. Complete fixtures verify both gates and the field writes; audio/minimap allocation for an off-camera attack remains outside this batch.

The two on-damage prefixes stop at military-help entry `0x4aebc0`, recording unchanged HP, changed last-attacker owner and the notify argument. They establish that Irradiate differs from ordinary weapons before HP changes; the helper is not stubbed. Computer help, nonempty ally propagation, successful temporary reveal and an actual retarget order require their own complete caller fixtures.

## Hallucinated attackers and hit producers

Incoming damage to a hallucinated **target** is doubled in the ordinary pipeline. An attack produced by a hallucinated **attacker** takes a different path. Bullet wrapper `0x41e290` tests hit flag `bullet+0x62 & 2`; melee `0x41fc70` tests attacker status `0x40000000`. Both choose `0x41e2f0`, which returns for zero HP/invulnerability, invokes on-hit reactions and applicable notification, then may request a shield impact image. It does **not** subtract HP, shields or matrix HP and does not refresh strength.

Complete hallucinated-hit calls verify unchanged HP, shields and matrix values with null and same-owner attacker pointers, shield amounts 255/256/512 and independent/normal damage. In native 1.17 the shield image check is outside the nonnull-attacker block: even a null source can request the image when enabled shields are at least 256 and damage type is nonzero. OpenBW's pinned rewrite places the image check inside `if (source_unit)`, a concrete source difference for this synthetic fixture. [OpenBW damage and hallucination source](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L13673)

For ordinary projectiles, `0x409010` reads weapon base damage from 16-bit table `0x525c80`, upgrade bonus from `0x525d88`, upgrade ID from `0x5256c0`, and the bullet owner at `+0x4c`. It computes `(base + upgradeLevel*bonus)<<8`, using a 32-bit product. Bullet wrapper `0x41e290` forwards heading `+0x21`, weapon `+0x60`, owner `+0x4c`, source pointer `+0x64` and its supplied divisor. Complete caller probes confirm damage and hallucinated branches with divisors 1/2/3/9 and upgrade levels 0/1/3.

Melee obtains its ground weapon from the attacker's unit type, with the native special case for unburrowed Lurker ID 103 selecting weapon 130. Its upgrade multiplication uses **AX**, retaining only 16 bits before adding the base damage and shifting to raw units. Complete caller probes verify ordinary levels and a synthetic large product; the projectile producer retains the wider product. These overflow fixtures specify arithmetic boundaries rather than claiming that stock campaigns contain such upgrade levels.

Weapons.dat explosion type **5 (Nuclear Missile)**, column `0x5258e0` tested at `0x40901a`, takes a separate amount branch in `0x409010`: `max(500, trunc(2*maximumLife/3))*256`. Yamato is explosion type 15 and uses the ordinary formula. Corrected after the function atlas ([P02 `0x409010`](atlas/p02.md)); this is the computed Nuke amount of [native-nuke.md](native-nuke.md). The probe case IDs keep the earlier `yamato_amount_*` prefix and the input key `weapon_behavior`; the value 5 they supply is this explosion-type byte. Maximum life uses configured HP plus maximum shields when enabled; zero configured HP falls back to rounded current HP, with a minimum of one. Complete amount-helper probes cover the 749/750/751 boundary, maximum shields and zero-DAT fallback. Thus a universal fixed Nuke amount would fail against sufficiently large or custom maximum-life targets. The separate hit-effect dispatcher chooses when this amount helper is called; only the amount helper and damage wrappers are certified here.

## Lethal handling, cargo and statistics

When signed supplied HP damage is at least current HP, `0x41e150` enters the lethal path. It first asks native `0x4381f0` whether the victim is an active transport. Hallucinations fail this predicate; Overlord ID 42 additionally requires upgrade 24; other units use the units.dat capacity byte. If the predicate is true **and the Building property is clear**, it inspects all eight packed cargo slots through real generation-aware resolver `0x4381d0 → 0x428b50`.

A cargo unit must resolve, have a sprite and have a nonzero main order to receive native removal `0x42a4d0`, followed by its death statistics. After cargo processing, the victim's HP is set to zero; removal and victim statistics follow. With a nonnull hostile attacker, the caller then invokes kill-count increment and the post-kill callback `0x4be950`, which is a literal `ret` in this executable.

Eight lethal prefixes stop **before** the original removal entry. Empty-cargo fixtures establish that HP is already zero at victim removal. A valid active cargo fixture establishes that the first removal argument is the cargo pointer while victim HP is still unchanged. The same packed handle with cargo order zero is skipped. A Building victim skips cargo death even with positive capacity. A hallucinated transport also skips it. These call-order boundaries are preserved explicitly; none is called a completed death.

The removal entry itself clears related state, clears the order queue, requests Death order 0, advances order state and removes AI membership through `0x48dbc0`. That work depends on live order/AI pools and can mutate broader unit state. This batch does not replace it, allocate a fake successful order, or claim that the Death FSM completed. The original removal-entry instructions are included in the static evidence for the next caller-closure step.

Death-statistics helper `0x414080` was separately executed completely. It excludes hallucinations and units with the Subunit property `0x10`. For other victims it records per-type/player losses and group-dependent unit, building and factory losses. Kill statistics require a different attacking player below 8 and a player that passes actual inactive-player helper `0x4bd590`. Group flag `0x08` selects unit loss/kill statistics; otherwise `0x10` selects building statistics; `0x20` additionally selects factory statistics. Kill-score value comes from units.dat word `0x524290`. Cases cover combined flags, self-owned damage, player 8, hallucinations and subunits with exact counter values.

Kill-count helper `0x41fc40` separately increments byte `unit+0x8f` with saturation at 255. For Interceptor ID 73 it also follows parent pointer `+0xc0` and increments that parent's count. The tests cover counts 0/1/254/255 and parent propagation. These complete helper calls do not imply that statistics and counts were reached after a fully executed live death/removal chain.

## Source reconciliation and remaining closure

[GPTP's pinned damage reconstruction](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/weapons/weapon_damage.cpp#L375) was useful for identifying operation order, factor-table structure and effect helpers. It targets 1.16.1 and contains optional event hooks. Its source arithmetic and injection ABI are not substituted for local evidence. The local matrix, shield and minimum results have now been independently recovered. Its transport death reconstruction omits the native 1.17 Building exclusion, while OpenBW includes that exclusion.

Additional concrete differences are the local Chitinous boolean bonus, the full-width unsigned divisor, factor overflow/shift behavior, the factor-table zero for Normal/IgnoreArmor targets of independent size 0, and the null-attacker hallucinated shield effect. Ordinary stock data often hides these differences. The probe batch makes them discriminating and reviewable instead of silently selecting one rewrite as authoritative.

The remaining work is concentrated in effects and callers rather than ordinary damage arithmetic:

| Boundary | Established here | Required complete original fixture |
|---|---|---|
| Death/removal `0x42a4d0` | Lethal comparison, cargo/victim call order, zero-HP commitment, separate counters | Live order pool, Death transition, queue disposal, AI detachment, cargo removal and post-return shield/cache effects. |
| Computer damage help `0x4aebc0` | Earlier than vision/HP change; player-type gate; native attribution writes | Regional/military state populated for player type 1, including target/dependents. |
| Reveal and retarget | Notify and detection gates; exact original callees | Actual map vision state and an admissible changed target, with live order pool. |
| Image effects | Actual direction/LO calculation, selected image flags, empty-pool handling, empty matrix-image search | Real image pool, existing matrix images, image/iscript initialization and completed-building damage overlays. |
| Off-camera notification | Local-owner and cooldown gates, timer/frame writes, real camera predicate | Original notification/audio/minimap state and remaining real callees. |
| Hit-effect scheduling | Complete normal bullet/melee wrappers and amount helper | Full hit-effect dispatch, splash enumeration, projectile lifecycle and attack/iscript timing. |

The strength contract used by the complete calls is detailed in [native-unit-ai.md](native-unit-ai.md). Combat selection is in [native-combat-ai.md](native-combat-ai.md), and regional/group decisions in [native-military-ai.md](native-military-ai.md). These separate contracts should be connected to the native damage ordering above when implementing the authoritative Defold simulation.
