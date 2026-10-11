# Native 1.17 populated status effects, periodic damage and expiry

**23 complete cases pass**, with zero prefixes or callable helper stubs: **8,471 complete original direct invocations / 1,704,867 instructions**, maximum single entry13,509 below the100,000 instruction bound. This batch closes selected original **Plague, Ensnare, Stasis and Acid Spore effect applications and positive overlay cleanup**, including natural Defiler144 casting through stock bytecode/projectile delivery. Repeated regeneration/timer services account for most invocations; counts do not quantify algorithm coverage. All cases use actual callees, finite populated Unit/Sprite/Image/Thingy pools and unchanged stock resources. The previously published [spell effects](native-spell-effects.md), [natural casting](native-spell-casting.md) and [attack timer service](native-attack-orders.md) remain separate evidence.

Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. [Static evidence](evidence/native-status-effects-static.json) preserves 20 new reviewed instruction ranges / 576 decoded instructions. [Probe evidence](evidence/native-status-effects-probes.json) records complete returns, actual calls, stock VM PCs, Bullet impact events, state assertions, independent RNG checks and exact dependency hashes. [Asset provenance](evidence/native-status-effects-assets.json) contains 62 image IDs / 47 distinct stock GRP paths with selected LO data; raw retail bytes remain private.

The [inspector](../../tools/research/inspect_native_status_effects_117.py) reads the EXE as data. The [probe](../../tools/research/probe_native_status_effects_117.py) requires its explicit execution flag; without it, only a plan is printed. No callable helper stubs, Windows imports, host-file/network calls, renderer, process startup, full running game or campaign execute. Flat synthetic terrain/region and visibility inputs, controller/owner state, ambient unit/IScript globals and finite pools are declared fixtures. Area calls define the complete effect-entry contract, rather than proving every spell's frontend research/energy/command admission.

## The periodic contract uses eight actual unit services

`43a320` increments the unit's byte `+122` when Completed or visible; when it reaches8, the caller resets it to0 and invokes **`4351e0`** at `43a3d7`. These probes start with genuinely created/completed units and the original initialized phase. They do not directly overwrite the status timers or periodic counter to force positive expiry.

Consequently, duration75 requires **600 actual unit-timer calls**, duration131 requires1048, and an Acid Spore timer150 requires1200. These counts are **unit-service invocations**, not a independently measured number of accepted campaign frames. Image/LoneThingy services are separately declared. The distinction matters because clearing a status timer and removing its animation need not happen at the same instruction.

## Plague: complete casting, integer damage and nonlethal boundary

Stock Defiler46 begins with energy50×256. **3,200 actual `43a320` regeneration calls** raise it to150×256 without an energy-field override. Real issuer `42a340` activates144 at `(400,400)`. Complete generic order434460 spends150 energy, sets state2 and initiates stock SpellCast animation. The natural caster direction104 already satisfies the selected point's angle gate.

Actual caster image/IScript and Bullet services then compose:

```text
434460 → 417220 → stock Defiler SpellCast IScript
418b20 → 41f4a0 → 420030 → 408f00(weapon60)
40a5f0 → stock Image387 bytecode → 4090e0(explosion11)
       → 43db50 → 442b20 → 43de10 → 43e0a0
```

The declared loop calls caster timer, full primary dispatcher, caster image, victim timer, Bullet and LoneThingy services in that order. It is not a complete native frame. The Bullet appears at the requested point, matching weapon60 behavior4. Caster return to an idle order precedes final projectile-image recycling; successful energy debit is neither an immediate effect nor proof of synchronous delivery.

| Declared service tick | Actual native result |
|---|---|
| 4 | Stock IScript creates weapon60 / Image387 at `(400,400)`. |
| 10 | Caster becomes Guard2 while the Bullet remains populated. |
| 12 | Caster is HumanGuard3; explosion11 sets victim Plague75 and creates Image388. |
| 32 | Bullet is recycled; victim timer72, rawHP7210 after three periodic damage calls; caster energy256 raw. |

Plague area entry **`43db50`**, ECX=x, EDX=y, stack[source], queries a64-pixel rectangle through actual442b20. Candidate **`43de10`** excludes the source, status `04000000` and Burrowed bit10. It has **no DAT Building-property exclusion**, unlike Ensnare and Stasis. For an accepted candidate it invokes apply **`43e0a0`**, then the real attack notification4203d0 when source is nonnull. In the positive same-owner area fixture, that notification returns through its original owner-equality guard; a hostile reaction chain is an additional branch.

Apply43e0a0 allocates the stock size-class overlay **388..390** only when the existing Plague timer is0 and the target is not Burrowed, then stores duration **75**. Reapplication therefore resets the timer without duplicating an already-present overlay. The ordinary Marine fixture has a genuine populated Image388, not an artificial image pointer or exhausted-pool stand-in.

`4351e0` decrements the timer and **always calls damage43ddb0 while the original timer was nonzero**, including its1→0 transition; it then calls clear43ddf0 when the result is0. Stock weapon60 damage300 is converted to raw periodic damage:

```text
periodicDamage = floor(300 × 256 / 76) = 1010
if !Invincible && currentRawHP > periodicDamage:
    actual41e150(currentRawHP − periodicDamage)
```

The original multiply/shift constant at43ddb0 implements divisor76, while the status duration is75. Preserve those distinct constants. A stock40-HP Marine receives ten decrements of1010 and ends at **rawHP140 = 0.546875 HP**. The code does **not** set a fixed minimum of256 raw units. Three boundary cases first use actual damage entry41e150 to establish HP1009/1010/1011; no HP field is directly overwritten. The subsequent original Plague periodic service leaves1009 and1010 intact and reduces1011 to **1 raw unit**. The comparison is strictly greater.

After600 native timer services, timer0 and the actual overlay removal traversal release populated Image388. Target remains alive with rawHP140. Remaining distinct Plague branches include hostile reaction composition, Building targets, Burrowed/Invincible paths, shield-bearing targets, all recast interleavings and malformed/modified damage values.

## Ensnare: populated overlay, real speed change and cleanup

Complete **`43dc90`**, ECX=x, EDX=y, stack[source], creates a genuine LoneThingy using sprite364/Image383, assigns elevation19 and updates visibility. It then queries a64-pixel rectangle through442b20 and callback **`43df10`**. The callback rejects the source, DAT Building property1, status04000000 and Burrowed bit10; owners otherwise do not gate the effect.

Actual apply **`43e0e0`** creates overlay **384..386** when the timer was0 and the target is not Burrowed, stores duration **75**, then tails into real speed recalculation43aea0. This is the positive populated effect missing from the prior absent-overlay expiry cases.

The Marine case creates Image384 and completely expires it after600 real unit-timer services. The aircraft composition uses genuine stock Wraith8 and Image385: native maximum speed **1707 → 853** while Ensnared, then **853 → 1707** after original expiry. No speed or timer field override creates this result. Clear **43dee0** performs populated overlay removal422e70, stores timer0, and invokes actual43aea0. Its stock Flingy speed/acceleration/turn callees execute.

Selected real target-image/LoneThingy services subsequently remove the visual area effect and leave no live Thingys. Their explicit service count is a fixture bound, not a claim that the visual effect lasts the entire75-tick status duration.

## Stasis: disablement and deferred overlay animation

Complete **`430880`**, ECX=x, EDX=y, stack[source], creates genuine sprite361/Image364, assigns elevation17 and visibility, then queries a **48-pixel** rectangle with callback **430920**. Source, DAT buildings, status04000000 and Burrowed targets are rejected.

Accepted candidates call **`431090(unit,131)`**. If the Stasis timer is0, the helper creates a stock size-class overlay **365..367** and sets status **04000000**. It stores the maximum of the existing timer and the low-byte duration argument, then calls actual disable routine **434c60**. The Marine fixture reaches native stop43a490, stock order-target retention/clearing logic, image/order animation handling420910, button-set disable422f00(228) and sight invalidation4a7ed0. Subunit, Ghost-family decloak and Carrier-family interruptions are distinct branches beyond this selected ordinary Marine path.

After **1048 original unit-timer calls**, `4351e0` reaches **4310f0**. It clears the timer; if Disabled bit400, Lockdown and Maelstrom do not block restoration, it restores the stock button set and invalidates sight. It clears04000000 unless DAT property20000000 requires it, then invokes actual overlay completion **4343b0(365,367)**.

Unlike Plague/Ensnare's immediate removal422e70,4343b0 requests the existing overlay's **animation1** through411730. At the exact final timer call, the populated Image365 remains linked even though the timer and invulnerability bit are already0. Subsequent actual target image/stock VM services end that overlay and return its Image to the pool. The report therefore distinguishes gameplay status expiry from later visual lifetime. Real LoneThingy services also expire the positive area effect Image364.

## Acid Spores: nine slots, overlay groups and shortest-slot refresh

Complete **`43dbf0`**, ECX=x, EDX=y, stack[attacking_owner,target], invokes442b20 with radius64 and callback **43de50**. The callback uses the first argument; the second stack argument is consumed by the wrapper's `ret8` without being read for the search. It excludes the attacking owner and DAT buildings, requires airborne status4, rejects04000000 and applies the cloaked/detected-owner mask gate for status300.

Actual **43dce0**:

1. Increment byte `+126` only below **9**.
2. Compute size class from unchanged DAT overlay flags, then choose overlay `986 + 4×sizeClass + min(count>>1,3)`.
3. If that exact overlay is absent, remove old986..997 overlays through actual422e70 and allocate the correct populated replacement.
4. Scan nine timer bytes `+127..+12f`: use the first zero slot immediately; otherwise refresh the slot with the smallest timer to **150**. The strict comparison in the original scan preserves the earliest slot for a minimum tie; this tie rule is a static instruction fact.

Genuine Wraith8 has native overlay size class1. Complete repeated-application cases for1/2/3/8/9/10 effects produce counts **1/2/3/8/9/9**, timers150 in occupied slots, and populated Images **990/991/991/993/993/993**. This is independent of DAT combat unit-size3. A saturated recast with naturally staggered timers `[142,143,…,150]`, generated by original applications and eight timer calls between them, preserves count9 and refreshes only the first/shortest timer to150.

Four complete owner/aircraft boundary cases use genuinely created Marine0 or Wraith8, owned by0 or3, with attacking owner0. Only the airborne Wraith owned by3 receives a spore; no status/property override manufactures these boundaries. Source-owner rejection is not a diplomacy check.

Actual periodic tail **`43e280`** decrements each nonzero timer and subtracts one from count for each timer that reaches0. It updates the populated overlay when the group changes and removes the existing986..997 overlay range when the old count was nonzero and the new count becomes0. Three genuine spores with all timers150 completely expire after **1200 actual43a320 calls**: count0, all nine timers0 and no spore overlays. Damage/armor consumers, staggered expiration groups, cloaked detection and the positive Devourer attack/projectile upstream chain remain distinct compositions.

## Reproduction and evidence boundary

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/inspect_native_status_effects_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --out docs/research/evidence/native-status-effects-static.json
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_status_effects_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --data /private/tmp/koprulu-campaign-data --manifest /private/tmp/koprulu-campaign-data/manifest.json --assets /private/tmp/koprulu-status-effect-assets --asset-manifest docs/research/evidence/native-status-effects-assets.json --pyms /private/tmp/koprulu-pyms --out docs/research/evidence/native-status-effects-probes.json --execute-original-code
```

Runtime: x86_64 Darwin/Rosetta, Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4. Probe SHA-256 `b1c1ff99571e708b6319adc66ec59465eaa97c6dce5e4c656af6adf40b2ea2ba`; inspector `4dd94c2f2009e9908a1f5a2a00e0cbe505f2cd8950479cac1c35001f7bb45d14`; asset manifest `7140cd7305ebbe8c8f58d02b949408b58eaae61a5188b895f25a782068902293`. JSON records57 imported local source hashes and unchanged stock DAT/IScript/TBL and asset hashes. Native originals establish the contracts; differently versioned modified-source names are not substituted for native effect behavior.

Remaining status/spell families include positive Psi Storm ground-over-time execution, Lockdown/Maelstrom, Restoration/Flare/Feedback/MindControl/Hallucination, and other projectile/status interleavings. This batch closes its declared populated applications, periodic consumers and selected cleanup paths. It does not establish all caster orders, every immunity/reaction branch or complete original campaign parity.
