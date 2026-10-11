# Native 1.17 natural caster initialization and populated Dark Swarm protection

**Ten complete cases pass**, covering natural Arbiter initialization, Defiler Dark Swarm casting through stock IScript and Bullet delivery, naturally regenerated Recall energy, and positive ranged protection followed by genuine field expiry. The guest executes **5,886 complete original direct invocations / 1,108,822 instructions**, with no prefixes or callable helper stubs. Repeated energy/timed-life services account for most direct invocations; these counts are evidence bookkeeping, not a measure of algorithm coverage.

Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. [Static evidence](evidence/native-spell-casting-static.json) preserves seven new reviewed witnesses / 340 instructions and the InitArbiter dispatch route. [Probe evidence](evidence/native-spell-casting-probes.json) records original calls, state, stock VM PCs, Bullet impacts, independent RNG validation and 54 local dependency hashes. [Asset provenance](evidence/native-spell-casting-assets.json) identifies 36 stock image IDs / 31 distinct GRP paths and selected LO inputs. Retail bytes remain in private scratch storage.

The [inspector](../../tools/research/inspect_native_spell_casting_117.py) reads the EXE as data. The [execution probe](../../tools/research/probe_native_spell_casting_117.py) defaults to a plan and requires its explicit execution flag. Maximum single entry: **13,509 instructions**, below the **100,000** limit. The guest performs no Windows imports, process startup, full-game execution, host-file/network calls or renderer operations. Private finite pools, flat synthetic terrain/region data, player3/controller2, availability/visibility gates and ambient original unit/IScript context are declared inputs. Frontend player-command admission and prerequisite research are separate from an already-issued order's consumption.

## InitArbiter reaches readiness through actual original instructions

Fresh stock type71 creation/completion `424900 → 424da0` yields primary **130**, secondary **23**, status gate `1000` set and stock energy **50×256**. Positive cases preserve those fields. No host instruction clears the gate or assigns a ready order.

InitArbiter is in the **second** stage of `439420`: byte table `439b18[130−4]` gives case25; target table `439a84[25]` gives wrapper `4394bf`, which calls **`42f510`**. The third-stage table alone is insufficient to find this handler.

`42f510`, ECX=CUnit, performs:

1. If the primary queue head `+74` is null, read the stock type71 idle order at `520f67` and invoke actual issuer `42a2c0` with order2. The initial gate can keep it queued.
2. For a non-Hallucination unit, invoke actual secondary changer `42a1e0` with **131** (CloakNearbyUnits).
3. Tail into `429d00`, which clears `1000` and advances the real primary queue through `42a5a0`.

The direct complete Init handler ends at **Guard2 / secondary131 / gate clear / queue empty**. The complete `439420` visit continues ordinary dispatch and ends at **HumanGuard3 / secondary131 / gate clear / queue empty**. These are different entry contracts. This batch closes readiness; secondary131's complete cloak-aura service remains a separate effect consumer.

## Defiler119: natural energy, cast animation, projectile and field

Stock Defiler46 creation/completion also begins at energy **50×256**. **1,600 actual `43a320` calls**, using its real `438e20` energy service, raise that energy to **100×256**. Issuer `42a340(unit,119,400,400)` activates Dark Swarm. The caster remains at `(200,200)`, with natural direction104; the requested point's direction is96, within the actual stock attack-angle gate. No direction, status, order or energy override establishes the positive cast.

Complete generic order **`434460`** reaches actual target validation `434e70`, technology cost/readiness, range/movement helpers **`435020 → 435530`**, energy debit `435300`, cooldown production `41fb70`, RNG source49 jitter and stock animation invocation `417220`. Stock order119 maps to technology14, **100 energy**, weapon59 and animation7. The initial successful call spends all100 energy and sets state2 while no Bullet exists yet. Order acceptance is not synchronous effect creation.

The actual stock Defiler image VM later invokes:

```text
418b20 → stock SpellCast IScript → 41f4a0 → 420030 → 408f00
40a5f0 → stock Bullet/Image503 signal → 4090e0 → 43dc10
```

`41f4a0(unit,weapon)` invokes the point weapon launcher once, or twice for a stock damage-factor byte equal to2. `420030` distinguishes weapon behavior2's target-derived spawn point, behavior4's order point, and the other behavior path's direction/forward/upward offsets. Weapon59's behavior5 with stock zero offsets spawns its Bullet at caster `(200,200)` while preserving the requested order point `(400,400)` for explosion type13.

The composed loop executes, in its declared order, caster timer `43a320`, full primary dispatcher `439420`, caster image service `418b20`, a field timer if already allocated, Bullet service `40a5f0` and LoneThingy service `418990`. This is a bounded composition of real callers, **not a complete native frame**:

| Declared service tick | Actual native result |
|---|---|
| 1–3 | Generic state2; no Bullet; caster gains8 raw energy units per timer call. |
| 4 | Stock cast IScript sets signal2 and creates weapon59 Bullet/Image503 at `(200,200)`. |
| 5–8 | Bullet remains alive with its original animation wait countdown. |
| 9 | Actual explosion13 creates populated type202 at `(400,400)`, owner11, life900. |
| 10 | Primary dispatcher consumes signal2 and returns caster to Guard2; Bullet is recycled; field timer becomes899; caster energy80 raw. |

The [field report](native-spell-fields.md) independently closes creator bounds, neutral ownership, stock Image337 creation, timed-life Death and pool return. This new chain closes its upstream **119 order → energy debit → original cast bytecode → original projectile → positive field** contract. It does not prove arbitrary blocked chase, frontend tech admission, complete-frame timing or every computer-AI reservation-release branch.

## Naturally initialized Arbiter consumes Recall

A fresh stock Arbiter first executes complete `439420` to reach the readiness state above. **3,200 actual `43a320` calls** raise natural50 energy to150. Real issuer `42a340` activates137 aimed at a stock Marine at `(600,600)`. Complete Recall consumer `42fc40` debits150 energy and sets its22-unit timer; **22 actual timer calls** then permit the second complete order call to invoke the actual spatial callback/teleport chain.

The result is caster **Guard2 / secondary131**, energy **176 raw** from22 regeneration calls, shared Recall flag1, Marine relocated to **`(304,328)`**, and **two populated Image391 effects**. The destination reflects original occupied-unit fit/ring placement, not guaranteed preservation of the caster's exact center. The [field/Recall report](native-spell-fields.md) documents fit failures, exemptions, target clearing and positive Recall effect expiry; [unit creation](native-unit-creation.md) documents the placement ring contract. This new case removes the earlier ready-caster fixture's manual `1000` clear from the positive upstream composition.

## Genuine Dark Swarm blocks ground ranged damage and expires

Four independent fixtures create stock Marine0 as attacker and either stock Marine0 or Wraith8 as target, with and without a genuine creator-produced type202 field over the target. Actual `41fd10 → 41fa00` follows stock target bounds, neutral completed counts and the original populated spatial query `432070 → 443110 → 4224c0 → 432410`. All targets retain their stock DAT properties and populated sprites/images.

Actual Marine AttackAnimation2 runs its stock bytecode, emits weapon0 through the original weapon/Bullet constructors, and reaches complete hit dispatch `4090e0`. Both units use fixture owner3 and an explicit target reference; this tests the mechanical weapon/field consumer, rather than legal enemy-command admission.

| Stock target | Field absent | Genuine field present |
|---|---|---|
| Marine0, ground, initial40 HP | Miss threshold0; impact flag1 clear; **34 HP** after the first impact. | Threshold **255**; Bullet Miss flag1; **40 HP remains**. |
| Wraith8, airborne, initial120 HP | Threshold0; **114 HP**. | Threshold0; **114 HP**. |

The miss RNG consumer compares the low byte **inclusively** with the threshold.255 therefore forces the covered ground miss for every low-byte result; this is not an approximate chance. An airborne target bypasses that cover protection. The prior [projectile](native-projectiles.md) and [spell-family](native-spell-families.md) reports preserve the inclusive draw, target-type and type-only overlap contracts.

The protected Marine fixture then executes **all900 original field timed-life calls**, original Death dispatch `439420`, selected pending-image/Bullet/LoneThingy services, and actual pending CUnit recycler `4256b0`. This avoids introducing unrelated live-Marine Guard decisions into a claimed complete active-list frame. Field sprite becomes null and completed neutral202 count becomes0. A second actual Marine animation/weapon0 impact now has threshold0 and flag1 clear, reducing the same target **40→34 HP**. The real field's lifecycle therefore removes its positive ranged protection; an exhausted pool or a supplied count does not stand in for successful effect removal.

## Reproduction and remaining contracts

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/inspect_native_spell_casting_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --out docs/research/evidence/native-spell-casting-static.json
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_spell_casting_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --data /private/tmp/koprulu-campaign-data --manifest /private/tmp/koprulu-campaign-data/manifest.json --assets /private/tmp/koprulu-spell-casting-assets --asset-manifest docs/research/evidence/native-spell-casting-assets.json --pyms /private/tmp/koprulu-pyms --out docs/research/evidence/native-spell-casting-probes.json --execute-original-code
```

Runtime: x86_64 Darwin/Rosetta, Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4. Probe SHA-256: `27d8f7178b6ffda363c667e54dcbd8869972b825ed9741ee449e6e08bed4b834`; inspector `2124a4bbc5c211490e1c3d2d7361be29c688cc8fe0401ff0ce8ca363dede58da`; asset manifest `3b64707e88780ff4ce191c3dc3fea4bdf4199624cfd1ddc1fe3daaa888cd93cd`. Stock DAT/IScript/TBL and every selected GRP/LO hash are checked before execution.

Remaining distinct spell contracts include blocked caster movement/facing and full command admission, CloakNearbyUnits execution, other spell-specific casting/weapon/effect families, genuinely populated status overlays and expiry, all reservation-mask writers, field-boundary compositions for other weapons, and authentic complete campaign/frame interleavings. These are explicit follow-on consumers; the positive original chains above do not imply complete campaign parity.
