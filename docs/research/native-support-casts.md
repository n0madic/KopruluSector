# Native 1.17 natural support casting and populated expiry

This batch closes selected complete original **Arbiter Stasis147, Queen Ensnare146 and Science Vessel Matrix141 / Irradiate143 / EMP122** compositions. Stock creation/completion, natural caster readiness and regeneration, actual primary-order issuance/dispatch, stock IScript, populated projectile delivery, target overlays and selected periodic cleanup execute with real original callees. A separate Matrix capacity-consumer case creates a populated impact overlay. All earlier [spell effects](native-spell-effects.md), [natural casting](native-spell-casting.md), [status effects](native-status-effects.md) and [Psi Storm](native-storm.md) publications remain separate evidence.

**10 complete cases pass**, with zero prefixes/stubs: **11,202 complete original direct invocations / 2,156,733 instructions**, maximum single call13,509. Repeated regeneration and timer services account for most invocations; counts do not quantify algorithm coverage.

Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. [Static evidence](evidence/native-support-casts-static.json) preserves six reviewed ranges / 218 decoded instructions. [Probe evidence](evidence/native-support-casts-probes.json) records complete returns, original call/VM/impact traces, unchanged input hashes, independent state assertions and RNG validation. [Asset provenance](evidence/native-support-casts-assets.json) contains 91 image IDs / 71 distinct stock GRP paths plus selected LO resources; raw retail data stays private.

The [inspector](../../tools/research/inspect_native_support_casts_117.py) reads the exact EXE as data. The [probe](../../tools/research/probe_native_support_casts_117.py) prints only its plan unless `--execute-original-code` is supplied. The guest uses finite populated Unit/Sprite/Image/Thingy/Bullet pools and reviewed original instruction ranges, with a 100,000-instruction bound per direct entry. There are no callable helper replacements, Windows imports, process startup, guest host-file/network access, renderer or complete game/campaign execution. Flat terrain/regions, visibility, owner3 and human controller2 are declared fixtures. The earlier AI selection/reservation reports establish separate preceding contracts; these casts start at the original issuer rather than reproducing a frontend command or strategic reservation scene.

## Natural setup and actual primary dispatch

Actual `424900 → 424da0` creates and completes every caster and target with stock DAT/IScript/GRP/LO resources. Caster energy begins at **50×256**. Actual `43a320` calls supply regeneration: 800 calls reach75 energy, 1,600 reach100, without an energy-field override. No facing, spell-state, order-state or duration-field override manufactures these positive paths.

Arbiter71 additionally executes actual main dispatcher **439420**, reaching InitArbiter42f510 and clearing readiness status1000 through original order/initialization consumers. The EMP target Arbiter also runs this genuine Init path. The stock Queen's natural direction120 faces the selected `(200,200) → (250,400)` cast. The Science Vessel's genuine nested Images262/263 alter its initialization's RNG usage: its natural direction is72 in this fixture, facing `(600,400) → (800,450)`. These positions select an already compatible positive range/facing path; unrestricted movement/turning is a separate composition.

Point spells use actual issuer **42a340**, stack `[order,x,y]`. Matrix/Irradiate use actual **42a380**, stack `[order,target]`; the target pointer is not an EDX argument. Every initial cast executes **439420** and the trace verifies its real destination: **438560** for141, **434460** for147/146/143/122. Matrix debits/applies/resumes inside that call. The other four handlers debit energy and enter state2, then stock caster animation/VM delivers the projectile and its effect.

After that initial dispatch the explicit service loop is:

```text
43a320 caster → 439420 caster → 418b20 caster images
43a320 target → 40a5f0 Bullets → 418990 lone sprites
```

These are selected service invocations, **not an independently measured native campaign frame**. Ambient unit/Flingy/Bullet context is declared and checked; target image updates for expiry are a separate later loop. Sound consumers take the real disabled-backend guard, without a sound-function stub.

| Natural cast | Genuine target | First effect service | Caster idle / Bullet recycled | State at that completion |
|---|---|---:|---:|---|
| Arbiter147, cost100 | Wraith8 | 6 | 9 | Stasis130, Image366, status04000000. |
| Queen146, cost75 | Wraith8 | 15 | 16 | Ensnare74, Image385, speed853. |
| Vessel141, cost100 | Battlecruiser12 | 0, inside initial dispatch | 0 | Matrix capacity64000 / timer168, Images373+376. |
| Vessel143, cost75 | Ultralisk39 | 8 | 9 | Irradiate75, Image382, source=caster, credited owner3. |
| Vessel122, cost100 | initialized Arbiter71 | 21 | 31 | Target energy80 raw / shields70 raw after ten post-impact regeneration services. |

The four projectile casts execute actual stock Bullet allocation, its VM/image list and original `4090e0` explosion dispatcher. Ensnare's genuine Image516 emits child Image512; EMP's Image554 emits populated Images556/555. These resources are extracted and hashed rather than bypassed by empty allocation pools. Bullet recycling and source return to an idle order are both tested. Completion energy on the caster equals eight raw units per elapsed caster regeneration service, after the exact initial depletion.

## Stasis and Ensnare through populated cleanup

Stasis weapon83 / explosion12 reaches actual **430880 → 430920 → 431090**, using stock area effect and Wraith-sized Image366. The preceding target timer service at tick6 has already advanced phase `+122` to6; the spell applies after that service. Tick8 performs the first duration decrement, leaving130 at cast completion tick9 / phase1.

The remaining **1,039 actual43a320 target calls** expire that native state. Every call independently checks duration, phase and HP. Final **4310f0** clears timer/status04000000 and requests the overlay's Death animation through4343b0/411730. Image366 remains linked at the exact gameplay-expiry boundary, then actual target image/stock VM services release it. This preserves the distinction between status removal and animation completion already established for Marine Stasis in the earlier status report.

Ensnare weapon58 / explosion10 reaches **43dc90 → 43df10 → 43e0e0 → 43aea0**, creating Image385 and recomputing genuine Wraith speed **1707 → 853**. Arrival at15 precedes tick16's periodic decrement, yielding74 / phase0 at cast completion. **592 additional actual43a320 calls** reach clear43dee0, which removes the populated overlay and recomputes speed back to1707. The target stays at its stock120 HP throughout. Actual Image/LoneThingy services execute separately after the timer services; their selected count100 is a cleanup bound, not the visual effect's campaign duration.

## Matrix: synchronous effect, front/back/impact lifetimes

Actual **438560** composes target permission, range/facing, debit435300, apply438900 and original order resumption. On the genuine Battlecruiser12, original size class2 selects front **373** and back **376**; stock cast Image539 is also created on the Vessel. The target receives capacity **250×256 =64000** and duration **168**, with no Bullet. The caster immediately resumes Guard2. The Vessel's own cast-image lifetime is not included in the target-expiry claim.

A separate complete **4384c0**, ECX=target, stack `[1 raw damage]`, consumes one raw capacity unit: **64000 → 63999**, timer stays168 and actual populated impact Image**379** is created. This is a direct original capacity-consumer contract; the one-unit debit is not relabelled as an independently executed weapon attack.

The same naturally cast target then receives **1,344 actual43a320 calls**. On the last periodic transition168→0, actual4384c0 consumes the remaining capacity, removes populated front373 and back376 through the original unit/sprite image traversals and returns them to the real Image pool. Remaining impact-animation cleanup executes through actual target Image/VM services. Final capacity/timer are0 and no371..379 target overlay remains. Battlecruiser HP stays500×256. Capacity depletion and timer expiry are therefore separate original paths sharing the real cleanup consumer.

These operation shapes corroborate [pinned GPTP Matrix creation/order code](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/spells/defense_matrix.cpp#L92); exact1.17 instructions and populated execution establish this batch's addresses, values and lifetimes.

## Irradiate: genuine source credit, final damage and four-raw regeneration

Weapon34 / explosion9 applies actual **438960** to genuine Ultralisk39. It creates large overlay **382**, stores duration75 from unchanged weapon34 cooldown, source pointer `+11c`=Vessel and owner byte `+120`=3. The cast completes at tick9 with duration75 / periodic phase1 and the target's original400 HP. Source credit and timer are produced by the real Bullet effect, not installed fixture fields.

The remaining **599 actual43a320 calls** execute 75 original Irradiate damage pulses through **4351e0 → 4385d0 → 442b20 → 438650 → 41dfc0**. Stock arithmetic yields **floor(250×256/75)=853 raw damage** per qualifying self pulse. The final1→0 pulse damages before cleanup438620 clears source, sets owner sentinel8 and removes populated Image382.

The same native43a320 then processes Zerg regeneration: **43a40c → 41b900 → 41bb50** adds **four raw HP units**, capped at the stock maximum, with real strength-cache refresh. Seven raw units apply to shield regeneration in an earlier part of the timer service; those values must not be interchanged. Damage precedes organic healing in the same invocation.

An independent integer model checks the actual HP, duration and phase **after every one of the599 services**. From tick16's first damage through tick608's last damage,593 services heal four raw units, giving:

```text
400×256 − 75×853 + 593×4 = 40797 raw HP
```

Final source/timer/credit are **0 / 0 / 8**, target remains alive, and no380..382 overlay remains after actual cleanup. The chosen Vessel lies outside the affected target's nearby area; no cargo or hostile reaction composition is inferred. [Pinned GPTP Irradiate application](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/weapons/wpnspellhit.cpp#L38) corroborates the source/owner/cooldown writes; original1.17 execution supplies the populated result.

## EMP: impact reset followed by genuine recovery

Actual weapon33 / explosion8 reaches **433fc0** at service21. The target's naturally initialized Arbiter has stock shields and energy. At that exact impact they both become **zero**, even with the same owner as the Vessel. The target is outside Stasis and distinct from the source/subunit, selecting the ordinary permitted branch.

EMP neither creates a persistent status timer nor stops future regeneration. Under the declared target-before-Bullet ordering, each subsequent target timer service adds seven raw shields and eight raw energy. The projectile's child animations remain populated until service31, so at its complete recycling boundary the target has **70 raw shields / 80 raw energy**. The probe independently checks that post-impact arithmetic. Reporting those values as failed EMP depletion, or reporting permanent zero values after impact, would confuse effect application with later service work.

## Reproduction and remaining distinct contracts

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/inspect_native_support_casts_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --out docs/research/evidence/native-support-casts-static.json
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_support_casts_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --data /private/tmp/koprulu-campaign-data --manifest /private/tmp/koprulu-campaign-data/manifest.json --assets /private/tmp/koprulu-support-cast-assets --asset-manifest docs/research/evidence/native-support-casts-assets.json --pyms /private/tmp/koprulu-pyms --out docs/research/evidence/native-support-casts-probes.json --execute-original-code
```

The final published run uses all five families together; restricted development runs are not merged. Runtime: x86_64 Darwin/Rosetta, Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4. Probe SHA-256 `fe7f3903b9b8d19b734933157b31826b1372f2c05e46417a0b484f45fcc71575`; inspector `559ea54f241b66d8aa3036f6aa1a5b2d954e5156bc558a421a51a69a5af42f01`; asset manifest `828c1a22c2d6a8dbb96b85ac8f4e7536000019f53c0a9d54ca88bce3a8cd38bf`. JSON records62 imported local source hashes and all stock input hashes. Counts are complete bounded original contracts, not a percentage of game coverage.

Remaining distinct work includes source death/recycling while Irradiate remains active, killing irradiated targets and credit consequences, hostile reaction/kill branches, moving/turning/out-of-range casters, target death before projectile delivery, interruptions/recasts/immunity boundaries, positive cargo enumeration, EMP removal of genuinely cast hallucinations, frontend research/command admission and full accepted-frame/campaign interleavings. Hallucination creation and the remaining spell families are subsequent dedicated compositions. This batch closes its declared natural positive caster-to-effect paths and populated periodic cleanup without treating them as complete original campaign parity.
