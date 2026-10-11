# Native Yamato, Lockdown, Optical Flare, Parasite, Broodlings and Consume (1.17)

This batch connects six additional stock ability orders to naturally created casters, original energy regeneration, actual primary-order dispatch, the stock Image VM and actual Bullet effects. It extends [support casts](native-support-casts.md), [Hallucination](native-hallucination.md), [spell casting](native-spell-casting.md), [periodic statuses](native-status-effects.md), [projectiles](native-projectiles.md) and [visible Death](native-unit-death.md). Their published files remain unchanged.

## Evidence boundary and reproduction

The target is the supplied PE **1.17.0.1**, strings **1.17.0**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. All addresses below are preferred virtual addresses in that executable, with ImageBase `0x400000`. Mod projects describing another version do not supply transferable native addresses.

[Static evidence](evidence/native-ability-casts-static.json) contains **10 reviewed ranges, 498 decoded instructions**, original primary mappings and all 25 Bullet explosion-dispatch destinations. [Execution evidence](evidence/native-ability-casts-probes.json) records complete bounded original calls, operand/register observations, VM opcode PCs, actual Bullet initialization/impact events, independent RNG verification, explicit state assertions and SHA-256 for every imported local source. The final run contains **21 passing complete cases, zero prefixes/stubs, 34,662 complete original calls and 6,037,847 executed instructions**; the largest individual call uses **26,643** instructions. It pins **67** imported local sources. Each original call must return with exact stack cleanup and stay within the reviewed allowlist and 100,000-instruction bound. No helper return is stubbed; guest Windows imports, WinMain, the full game, host file/network access and rendering are not executed.

The x86_64 Darwin/Rosetta runtime uses Python 3.9.6, Unicorn 2.1.4, Capstone 5.0.6 and pefile 2024.8.26. Probe SHA-256: `70ffec96263cee6009c8ef631de4c790bc27a2548e8892477e70697b19666bee`; inspector SHA-256: `d684cea6378b767d676c3a3091d2918eea698e25554aa2d81a646347249fa6c6`. The [inspector](../../tools/research/inspect_native_ability_casts_117.py) only reads the PE as data. The [probe](../../tools/research/probe_native_ability_casts_117.py) prints a plan without its explicit execution flag.

The [private image manifest](evidence/native-ability-casts-assets.json) declares **121 Image IDs and 100 distinct GRP paths**, effective archive precedence, file hashes, LO resources and extractor/library hashes. Retail resources remain private. Units/Flingy/Sprites/Images/Weapons/Orders DAT and IScript are loaded at their native descriptor destinations from the campaign manifest. IScript SHA-256 is `cd35e04eaa8ea95cbbccc980bfaf4cae236f4b18b620dfb4a765822ec32c1cd0`. LO kinds 0..4 are bound; the declared shield LO column is not bound, and this batch does not claim positive shield-overlay rendering.

Unit-pool error paths use authentic `rez\network.tbl`, SHA-256 `0a432e596328f1de50a2aaf8b5c0394e13ad4ec1e4bf37dddb74c2e889761733`, bound to original global `0x665de0`. Its [unchanged manifest](evidence/native-hallucination-string-assets.json) and [native loader/getter witnesses](native-hallucination.md) establish that identity. Host extraction/binding is a declared fixture operation; the native archive loader is not executed. Stat_txt is separately bound to `0x6ab4f8`.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_ability_casts_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest /private/tmp/koprulu-campaign-data/manifest.json \
  --assets /private/tmp/koprulu-ability-casts-assets \
  --asset-manifest docs/research/evidence/native-ability-casts-assets.json \
  --string-manifest docs/research/evidence/native-hallucination-string-assets.json \
  --pyms /private/tmp/koprulu-pyms \
  --out docs/research/evidence/native-ability-casts-probes.json \
  --execute-original-code
```

## Genuine producer and declared service order

Actual `0x424900 → 0x424da0` creates and completes each caster and target, with finite private CUnit/Sprite/Image/Thingy pools and initialized flat terrain/regions. Player 3 owns the caster. Player 4 owns the target for five offensive families; Consume targets player 3. Both player controllers are human type 2, so the selected reaction helper's controller gate returns through original instructions. Positive computer retaliation and strategic reaction are separate [AI contracts](native-combat-ai.md).

No energy, facing, spell timer, parasite/blind mask, target pointer or created-child field is overwritten to make a cast succeed. Stock caster energy starts at 50. Real `0x43a320` services regenerate eight raw energy units per invocation: 3,200 calls reach 150 points, 1,600 reach 100, and 800 reach 75. Source and target positions are declared fixture inputs. Positions matching the initial native facing allow the cast to proceed without manufacturing a movement or turning result.

Actual `0x42a380` receives ECX=caster and stack `[order_ID, target_pointer]`. A complete `0x439420` dispatch follows. All six order IDs map through stage 3 byte table `0x439d5c`, bias 1, case 59, destination table `0x439bcc`, wrapper `0x439985 → 0x434460`. The preceding native gate decrements caster byte `+0x85`; only old zero resets it to 8 and performs that slow-order dispatch. Optical Flare 185 exceeds the earlier stage tables' bounds and reaches this same stage directly. A generic spell order must not be described as running on every primary-dispatch invocation.

After the initial dispatch, each declared logical service executes caster `43a320`, caster `439420`, caster Image update `418b20`, target `43a320`, Bullet step `40a5f0`, then Thingy step `418990`. The proper ambient unit/VM context is supplied for caster work and cleared before Bullet/Thingy work. The first-effect and cleanup counts below refer to this composition, not an observed complete campaign frame or milliseconds. Target movement/orders and the global active-unit dispatcher are not advanced here.

| Family | Stock caster / target | Order / weapon / technology | Energy | Positions; natural facing | First effect / Bullet cleanup |
|---|---|---|---:|---|---:|
| Yamato | Battlecruiser 12 / Battlecruiser 12 | 113 / 30 / 8 | 150 | `(600,400) → (800,450)`; 72 | 50 / 78 |
| Lockdown | Ghost 1 / Wraith 8 | 115 / 32 / 1 | 100 | `(600,400) → (400,360)`; 200 | 19 / 39 |
| Optical Flare | Medic 34 / Wraith 8 | 185 / 107 / 30 | 75 | same as Lockdown; 200 | 14 / 24 |
| Parasite | Queen 45 / Wraith 8 | 120 / 56 / 18 | 75 | `(200,200) → (250,400)`; 120 | 15 / 16 |
| Spawn Broodlings | Queen 45 / Ultralisk 39 | 121 / 57 / 13 | 150 | same as Parasite; 120 | 15 / 16 |
| Consume | Defiler 46 / Zergling 37 | 145 / 61 / 16 | 0 | `(200,200) → (216,224)`; 104 | 36 / 40 |

Energy after cleanup is respectively 624, 312, 192, 128, 128 and 25,920 raw units. These values include the declared subsequent regeneration calls. Five casters finish at PlayerGuard order 3; Medic finishes at native Medic idle 175. The selected Medic idle calls execute `0x4361b0 → 0x431ce0 → 0x407f90` and return at the unavailable-Heal permission gate. Positive heal selection/execution is not inferred from this return.

## Yamato damage and prelaunch interruption

Stock weapon 30 has damage 260, weapon type 1, explosion 15, behavior 0 and maximum range 320. Original explosion table entry 15 selects `0x409171`, which passes the actual Bullet to `0x41e290` with divisor 1. Against the naturally created large Battlecruiser with armor 3, one successful impact changes HP from 500 to **243**: 257 points of damage. The stock Bullet has Image 541, trail/launch resources 542/543 and hit resource 544. This populated hit does not establish every size, upgrade, shield, miss or damage-attribution variant; those calculations have separate [damage evidence](native-damage.md).

A different target point `(400,360)` leaves the natural facing-72 caster in order 113, state 1, energy 150 and zero Bullets after the complete initial primary dispatch. No turning/travel service is supplied, so this is the exact initial waiting contract.

The aligned initial dispatch already debits all 150 energy before creating a Bullet. Issuing actual Death through `0x42a4d0` at this prelaunch point, followed by actual primary Death, stock body VM and selected pending recycling, removes the caster. The target remains at 500 HP and no Bullet appears. This closes selected **prelaunch Death interruption**, without assigning damage/kill credit to an explicitly issued Death or claiming interruption after a projectile is already flying.

## Lockdown duration, disable and overlay expiry

Explosion 4 selects `0x409276`: a nonnull, non-dying target receives `0x4389c0(target, 131)`. That effect creates Image `361 + size_class` only if target byte `+0x117` was zero, assigns the maximum of the old timer and the low byte of the argument, then executes actual `0x434c60`. The Wraith receives medium Image 362 and button set 228. Its main status is unchanged in this fixture; the routine's subunit status-bit-400 write must not be confused with an unconditional write to the main unit.

After Bullet cleanup, timer 129 and native periodic phase 7 remain. **1,025** further real `43a320` calls reach the eight-service expiry boundary: `4351e0 → 4386e0`, timer zero, phase zero and Wraith button set 8. `4386e0` restores type buttons only when independent status 400, Stasis timer `+0x119` and Maelstrom timer `+0x124` do not block that restoration. It then selects the Lockdown overlay range 361..363 for death animation. Image 362 is still linked with animation 1 immediately after expiry; 60 actual target Image-VM calls reclaim it. Cross-effect disable combinations, repeated cast refresh, subunits and all size variants remain separate branches.

## Permanent masks and actual Restoration effect

Parasite explosion 6 selects `0x4092c9`: a non-dying target gains `target+0x121 |= low_byte(1 << Bullet.owner)`, followed by the original invalidation/helper path and a reaction call if a source remains. Optical Flare explosion 21 selects `0x4094e8 → 0x438a20`; a normal target gains the same owner bit in `+0x123`, receives `976 + size_class`, calls `4a7ed0`, and writes global `0x6663e4 = 1`. A Hallucination follows the distinct Death branch instead; that producer/consumer is not positively exercised here.

Player 3 produces mask **8** in each genuine target. The mask survives **1,024** further actual unit timer services and 100 stock target Image updates. Optical Flare's medium Image 977 disappears, while its blind mask persists; visual lifetime and gameplay-state lifetime differ.

Complete actual Restoration effect `0x4383e0` then clears both `+0x121` and `+0x123`, creates the size-dependent Restoration hit overlay, performs actual invalidation and returns. One hundred actual target Image updates reclaim that temporary hit overlay. This is a complete **effect consumer applied to naturally produced masks**, not a claimed upstream Medic Restoration cast. Static code also handles Ensnare, Plague, Irradiate, Lockdown, Maelstrom and Acid Spores; their simultaneous positive Restoration branches are not executed by these mask cases.

## Broodling creation, life, pool failure and credit

Explosion 7 selects `0x409323`, performs the controller-gated reaction, then calls `0x43e120(source, target)`. A normal target produces two placement attempts beginning at target center minus 2 on each axis, with 4 added to both axes after each attempt. The original footprint, coordinate clamp, creation `424900`, completion `424da0` and timed-life helper `434a90` execute with stock data. The flat successful footprint path yields genuine Broodlings 40 at **`(248,398)` and `(252,402)`**, each player 3, **30 HP**, ordinary live/completed accounting and **WORD life 1800**. The alternative bounded placement search with actual caster argument `47dc30`, obstacles and failed fit are explicit untested paths in this batch; see [placement](native-unit-creation.md).

After both attempts, original `414080` records the target's death scores, `42a4d0` issues its Death, and `41fc40` increments the source's saturating kill count. With a player-4 Ultralisk and player-3 Queen, per-type target-loss and killer-kill counters each increase by one, and the Queen kill count becomes one. The target's HP remains 400 until Death consumption: this effect issues removal rather than reducing HP to zero.

Separate complete effect calls with **zero or one free CUnit** create zero or one Broodling, but still issue target Death and record those same scores and source kill. The target's slot is not recycled before the spawn attempts. Unit-pool error handling executes the original network-string lookup/nonlocal notification guard against authentic network.tbl. These are full original effect contracts; an upstream Queen cast/energy debit is not asserted for these additional pool cases.

For the two naturally cast children, 1,800 actual timer calls per child, serviced sequentially, decrement their real life to zero and issue Death order 0. Actual primary Death, populated stock body VM and `4256b0` return the Ultralisk and both children to the private CUnit pool. The Ultralisk body needs 21 actual Image updates; each Broodling body needs one. Corpse/impact Thingys are subsequently advanced through **250** actual `418990` calls until their list is empty, under the declared bounded service composition. This selected reclamation does not run the full global `438ef0` dispatcher or imply simultaneous child/target campaign timing.

## Consume permission, energy cap and deletion

Explosion 14 selects `0x409438 → 0x43db70` only for an existing source and non-dying target. The actual effect rechecks technology-16 target permission through `434130`; success executes `414080` death accounting and `42a4d0` removal. A normal target grants **50 × 256 raw energy**, capped by actual `434940`. The Hallucination branch skips this grant; its positive producer remains outside this batch.

The natural friendly Zergling cast starts at Defiler energy 50 and costs zero. Its positive effect occurs at service 36; cleanup at 40 leaves **25,920 raw energy**, exactly the original 50, gained 50 and 40 actual regenerations. The Zergling receives Death with HP still 35. No source kill-count increment occurs. Its selected original Death/body/CUnit consumers complete after 15 body Image updates; **250** actual Thingy services reclaim the Zergling remnants.

A direct complete effect case first reaches Defiler maximum **200 energy** through 4,800 real regeneration calls; the normal friendly target is consumed and energy remains capped at 200. The same genuine source and a player-4 Zergling are rejected: target retains Guard order 2, no target-loss count is recorded and source energy remains 200. These distinguish the actual rechecked target permission and cap; they do not establish all target classes, hallucinated targets or invincibility combinations.

## Corroboration and remaining ability inventory

Pinned [GPTP Lockdown/weapon-hit reconstruction](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/weapons/wpnspellhit.cpp#L210), [Consume](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/weapons/wpnspellhit.cpp#L420) and [Broodlings](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/weapons/wpnspellhit.cpp#L516) provide names and useful comparison. Their commentary, modified hooks and version-specific addresses are not evidence of local 1.17 execution. All conclusions above come from the exact local bytes/stock inputs and bounded original returns.

| Ability family / order | Current positive evidence boundary | Remaining concrete work |
|---|---|---|
| Yamato 113; Lockdown 115; Parasite 120; Broodlings 121; Consume 145; Optical Flare 185 | Selected populated casts and the contracts above | Moving/turning casters; other target classes/owners/immunity; subunits/cargo; repeated cast and postlaunch interruption; AI-to-cast composition. |
| Matrix 141; Irradiate 143; EMP 122; Ensnare 146; Stasis 147 | [Natural support casts](native-support-casts.md) | Other overlapping statuses, caster/source destruction, immunity/cargo and all attribution variants. |
| Dark Swarm 119; Recall 137; Storm 142; Hallucination 148 | [Casting](native-spell-casting.md), [field transport](native-spell-fields.md), [Storm](native-storm.md), [Hallucination](native-hallucination.md) | Remaining movement/overlap/shield/source/kill cases specified in those reports. |
| Plague 144; Acid Spores from weapon attack | [Natural Defiler144 cast, stock VM/Bullet delivery and original effect/periodic contracts](native-status-effects.md) | Plague hostile-reaction composition, Building/Burrowed/Invincible/shield-bearing targets and recast interleavings; additional Acid Spore producers/cross-status cleanup. |
| Restoration 180 | Full actual effect on genuine Parasite/Flare masks | Full natural Medic cast, all simultaneous harmful-status combinations. |
| Disruption Web 181; Mind Control 182; Feedback 184; Maelstrom 186 | Stock order/weapon/tech inventory and earlier selected AI decisions | Natural populated order-to-effect, energy/owner/target state, duration, pool/cleanup and interruption consumers. |
| Nuclear Strike family 123..129 | Earlier [native AI decision/reservation](native-spell-families.md) | Paid missile production/linkage, natural Ghost/Silo/missile orders, launch/designation/impact damage and cleanup/interruption. |
| Heal; Stim; cloak/decloak; burrow/unburrow; Siege transforms; Archon/Dark Archon meld; transport/load/unload; recharge/repair | Separate order/economy/combat reports cover selected constituents | Review each family's complete producer, state transitions and consumers in the completion ledger; the six-family batch does not cover them by analogy. |

This inventory preserves the broader remaining scope. Complete returns and passing assertions close the listed original contracts; they do not certify every ability family or a running campaign.
