# Native 1.17 trap acquisition, combat and natural re-hiding

This batch connects all six stock trap types to genuine enemy acquisition, TowerAttack19, populated stock IScript, original Bullet allocation and damage, and re-hiding through **complete original object callbacks at `445070`**. It also observes the stock Civilian's native damage reaction and ground retreat, and each trap's subsequent acquisition of a newly created second enemy. It extends [special orders](native-special-orders.md), [computer combat](native-computer-combat.md), [projectiles](native-projectiles.md), [damage](native-damage.md), [movement](native-movement-fsm.md) and [Bunker object-callback compositions](native-bunker.md); those publications remain unchanged.

The target is PE32 StarCraft `1.17.0.1`, ImageBase `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. All addresses below are preferred VAs; subtract `0x400000` for RVAs. Source descriptions from earlier versions are structural comparisons, not transferred native addresses.

## Evidence and declared execution state

[The native report](evidence/native-trap-combat-probes.json) contains **20 complete passing cases, zero prefixes, zero bounded fragments and zero helper stubs**. It records 1,604 complete original calls, including1,536 full `445070` callbacks, 11,615,298 original instructions, 14,819 distinct executed instruction addresses and 1,881,344 guarded guest writes. Runtime is x86_64 Python3.9.6 under Rosetta, Unicorn2.1.4 and pefile2024.8.26, with a 250,000-instruction bound per selected call. Every call reaches its sentinel and validates stack cleanup. All guest instructions and writes remain inside reviewed original code and declared writable memory. No Windows import, full process, renderer, sound backend or host file/network operation executes in the guest.

| Cases | Complete connected contract |
|---:|---|
| 6 | Actual creation, automatic acquisition, stock projectile damage, requested native enemy withdrawal and trap re-hiding |
| 6 | The same genuinely re-hidden trap acquires and damages a second enemy created by the original allocator |
| 2 | Right-wall variants reject an enemy behind their actual native facing, across72 callbacks |
| 6 | Independent acquisition and damage, followed solely by the original Civilian reaction/movement and natural trap re-hiding |

[The probe](../../tools/research/probe_native_trap_combat_117.py) SHA256 is `bc946fc4c666fa2549a17ab8267c55e213327521fbc8fc7ea55bca6705eb8f16`. [The inspector](../../tools/research/inspect_native_trap_combat_117.py) SHA256 is `d02c490df115e19ca5672c8267eae4bb60913b7c1df88855acf0cbd05fc9b1f4`; [its static report](evidence/native-trap-combat-static.json) adds three complete instruction blocks/67 decoded instructions and four stage3 dispatch witnesses. The probe pins85 imported source hashes, including the unchanged selected retreat helpers in [the Nuke inspector](../../tools/research/inspect_native_nuke_117.py), SHA256 `3e4f7295c88b71ff1d1e4b530e81c7fed84351a6837164646f0a9504447b1515`.

Private stock resources comprise effective Units/Flingy/Sprites/Images/Weapons/Orders DAT and IScript, GRP and LO inputs. All54/7/6/14/24/19 DAT columns use the actual executable's descriptors. Genuine `425610` initializes acquisition globals and `40a0e0` initializes the Bullet pool. The scene provides16 CUnits,32 Sprites,128 Images, finite Order/Bullet/Thingy pools, valid map bounds, one prepared walkable region, empty contour sets, eight valid private regional AI arrays and self-alliance1/cross-alliance0. Both players3/4 have controller2; the trap and Civilian have no attached AI record.

The visible tile plane `0x1ff00`, terrain/region arrays, controller inputs and initial auxiliary countdowns2000 are explicit fixtures. The full callback executes its real scheduler guards, but these countdowns and empty strategic lists avoid positive slow AI, creep-disappearance and periodic fog service. It genuinely executes active units, main orders, motion, IScript, Bullet progression and selected Thingy effects. This is a complete `445070` call under the declared state, not complete coverage of all branches in that routine. [The outer accepted-frame caller](native-frame.md) is outside the batch: the host supplies a monotonically incremented native frame counter before each callback. Reported counts below are callback invocations, not measured elapsed seconds.

Every trap and enemy is produced by original `469460` creation/initialization and the populated stock allocator/completion chain. Each trap first receives12 complete callbacks before its enemy exists. After creation the host never writes a CUnit/sprite position, heading, signal, target or order. The six requested-withdrawal cases call actual `43a270` with a destination; original code performs that relocation. The six independent natural-aftermath cases make no external relocation request after enemy creation.

The probe separately rechecks all68 physical campaign CHK hashes and21,427 raw UNIT records. None of those UNIT records has type203/209/210/211/212/213. This does not establish absence from scenario THG2/doodad producers or trigger creation. The20 scenarios are synthetic stock-resource execution states, not actual mission tuple coverage, campaign progression or a whole CHK loader run.

## Native acquisition and ordered reveal/attack/hide sequence

The exact stage3 table at `439d5c`, indexed by order minus1, selects destinations through `439bcc`:

| Primary order | Adapter | Complete consumer |
|---|---|---|
| 19 TowerAttack | `43994d` | `41f100` |
| 167 Trap | `4397cd` | `428490` |
| 170 HideTrap | `4397d5` | `4285a0` |
| 171 RevealTrap | `4397dd` | `4288a0` |

Unlike the earlier selected-handler tests, these probes reach those adapters through actual `439420` dispatch inside full `445070`. The order numbers/names agree with [pinned Teippi order definitions](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/order.h); the local table and complete calls establish this version's behavior.

Initial Trap state0 sets NoCollide`0x200000`, floor203/209 elevation0, visibility status bits`0x300` through `41d4c0(unit,3)`, saved field+`e4=0x80000000`, byte+`91=0`, and state1. State1 returns while disabled bit`0x400`, timers+`117/+119/+124`, or primary timer+`54` are nonzero. When eligible, instruction`4284dd` writes timer15 before calling actual `432060 -> 431b70` acquisition. Thus a failed search still produces its original retry timer.

A found enemy takes the exact path at`4284ec`: clear NoCollide, run floor predicate`429130`, restore the floor sprite's DAT elevation where applicable, call `41d4c0(unit,0)`, clear byte+`91`, issue Reveal171 through `42a2c0`, append the unit's DAT attack order with that target through `429f10`, then append Hide170 through `429eb0`. No host target or queue insertion supplies the observed attack.

The new complete wrapper`429f10` has ECX=source unit and stack=[order,target,queued-flag], `ret12`. With a nonnull target it packs the target Sprite WORD coordinates; with null target it packs zero. It calls original `429f60` with that point, target, FOW unit228 and the requested queue flag. Actual finite Order allocation, activation and linked-list changes run in the composition. For all six stock rows the attack order is19.

The candidate callback`432280` rejects self, missing visibility for the source owner, failed target validity/hostility/range gates and failed terrain/group acceptance. Its facing check at`43230d` tests CUnit status bit`0x10000`; when the bit is clear it invokes real `41fe60` with the stock weapon and candidate point. This static path explains why a target visible and within nominal acquisition range can still be rejected. The positive cases observe the real query and ensure scratch depth`5d6e48` returns to zero; they do not exhaust all candidate buckets or target-ranking combinations.

## TowerAttack and genuine stock Bullet consumers

Complete TowerAttack`41f100` checks the disabled/status timers, then calls original Hold attack attempt`420bd0`. A successful attempt first calls `43a0d0` with the trap's own Sprite position, then `40cbd0` with its actual order target point+`58/+5a`. A failed attempt tail-calls `42a5a0`, advancing the queued continuation. It does not use the ordinary AttackUnit10 consumer merely because the target is a unit.

The successful scene reaches stock attack animation and actual IScript firing, original Bullet allocation/initialization and weapon-specific Bullet progression. Event observations at`409e15` record the actual Bullet weapon/spawner; observations at`4090e0` record its actual source/target/impact. Assertions require an impact from the genuine trap to its genuine enemy, stock weapon identity and reduced raw HP. The host does not inject damage or manufacture a Bullet. The full object composition includes native body turning and sprite/image propagation; a heading-dependent stock script must produce its own signal before Reveal/Hide progresses.

| Unit type | Stock name | Ground weapon | Acquisition field | First damage callback | Victim HP after first damage |
|---:|---|---:|---:|---:|---:|
| 203 | Floor Missile Trap | 99 | 5 | 49 | 35 |
| 209 | Floor Gun Trap | 96 | 5 | 29 | 30 |
| 210 | Wall Missile Trap | 97 | 5 | 36 | 35 |
| 211 | Wall Flame Trap | 98 | 2 | 33 | 38 |
| 212 | Right Wall Missile Trap | 97 | 5 | 36 | 35 |
| 213 | Right Wall Flame Trap | 98 | 2 | 33 | 38 |

The effective Units DAT rows have no air weapon (130). The names agree with [pinned GPTP UnitId definitions](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/enumerations/UnitId.h). The acquisition field values are decoded stock metadata; these six scenes are not an independent full range-boundary matrix. The target is a stock40HP Civilian15. Raw HP uses256 units per displayed HP; the table reports the observed first callback with a decrease. These damage outcomes are selected weapon/target states; [native damage](native-damage.md) and [weapon effects](native-weapon-effects.md) contain the separate arithmetic/splash contracts.

The native initial wall headings differ: left variants have32 and right variants160. Front requests are`[816,768]` for203/209/210/211 and`[720,768]` for212/213, with the trap requested at`[768,768]`. The right-wall negative cases instead create an east-side Civilian at`[816,768]`. Across72 callbacks and at least three actual acquisition calls, both right traps retain167, null target and empty queue, and emit no Bullet; Civilian HP stays40. No host heading correction is made to obtain either result.

## Re-hiding, natural Civilian retreat and new targets

In the six requested-withdrawal cases, after the first native damage callback, actual `43a270` relocates the enemy to`[1408,1408]`. The trap completes Hide170, returns to167 and naturally restores status mask`0x200300`; its queue is empty and target null. The required callback counts after withdrawal are43/42/25/37/25/37 for203/209/210/211/212/213. A second Civilian is then allocated by original `469460` at the corresponding front request. Each re-hidden trap acquires that newly returned CUnit pointer itself, reaches19, emits a stock Bullet and damages the new target. First damage occurs after50/30/37/34/37/34 callbacks, respectively. The second cases record their preparation-case dependency rather than recounting its already recorded executions.

The independent natural-aftermath scenes continue only full callbacks after enemy creation. The actual unarmed Civilian damage reaction reaches `420190 -> 4b2720`'s null-AI branch, native retreat-point helper`421180`, and original Move6 issuance through `41fa00/42a3e0`. Its genuine movement state, ground path and physical position changes continue inside the callback. Eventually the trap no longer retains the enemy and completes its queued Hide; the enemy remains alive. No externally requested movement, heading, signal or location change drives this result.

| Type | First damage callback | Natural re-hide callback | Civilian HP at re-hide |
|---:|---:|---:|---:|
| 203 | 49 | 135 | 10 |
| 209 | 29 | 94 | 20 |
| 210 | 36 | 104 | 10 |
| 211 | 33 | 70 | 38 |
| 212 | 36 | 104 | 10 |
| 213 | 33 | 70 | 38 |

The callback indices start after enemy creation, following the twelve trap-only preparation callbacks. They are fixture observations, not intrinsic constant trap durations. The report retains actual order/signal/heading/status/target/queue/timer/HP/Civilian-position/Bullet timelines. All natural cases assert original Civilian Move6, physical departure, nonzero remaining HP, null final trap target and zero spatial-query depth. The native Hide/Reveal signal and original turning contracts are described separately in [special orders](native-special-orders.md#trap-state-and-genuine-turn-dependent-signals); no host signal is substituted here.

## Resource provenance and reproduction

[New resource provenance](evidence/native-trap-combat-assets.json), SHA256 `d49d43ecfe0b0b898462076d09efb38fd1c310757dc2a92b6a8efcd245c21f09`, records126 image IDs,111 distinct GRP paths and126 LO bindings, totaling132 unique GRP/LO paths. It includes the previous special-order inputs plus stock Civilian and weapon-effect resources. This resource inventory is not coverage of every listed image consumer. Archive/hash provenance comes from the unchanged [image resource reader](../../tools/research/inspect_native_image_assets_117.py), SHA256 `4e82f06230a943c7d225c751aac8db7d1ac9c7c47386a95f9915102a7c11a909`. Retail bytes stay private at`/private/tmp/koprulu-trap-combat-assets`; the repository contains only scripts, decoded metadata, provenance and execution observations.

The probe reuses unchanged [mission notification provenance](evidence/native-mission-notification-assets.json) and its private`rez/network.tbl`, bound to native`665de0`. Sound is disabled by a declared fixture, and positive external notification/audio consumers are outside the composition. [General reproduction](reproduction.md) specifies the authenticated executable, pinned PyMS and private data preparation.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_trap_combat_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out /private/tmp/trap-combat-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_trap_combat_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-trap-combat-assets \
  --asset-manifest docs/research/evidence/native-trap-combat-assets.json \
  --notification-assets /private/tmp/koprulu-mission-unit-assets \
  --notification-manifest docs/research/evidence/native-mission-notification-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out /private/tmp/trap-combat-probes.json --execute-original-code
```

The pure resource reader can reproduce the new private inputs using`--image-ids` with the comma-separated IDs from the provenance's`images[].image_id`, authenticated MPQs and the same effective DAT/TBL precedence. No old resource manifest needs replacement.

## Remaining campaign and general algorithm boundaries

The selected compositions close actual trap acquisition→attack→Bullet→damage→Hide and second-target acquisition, including a natural original enemy reaction. They do not certify all targets, alliances, disable/status timers, air-target rejection, ammunition/upgrade/armor combinations, disconnected/obstructed terrain, alternate vision/cloak conditions, multi-enemy ranking or positive strategic AI/periodic creep/fog branches. Natural enemy Death and retargeting after a dead target are separate from these surviving-escape/new-enemy cases. Actual mission THG2 trap production, trigger creation/ownership changes and all-campaign frame orchestration also remain separate consumers. [Pinned GPTP doodad orders](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/doodad_orders.cpp) corroborate the structural sequence; the exact local instructions and genuine stock-resource callbacks supply this batch's behavioral evidence.
