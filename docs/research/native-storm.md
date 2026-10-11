# Native 1.17 Psi Storm casting, repeated damage and shared target gate

**12 complete cases pass**, covering natural High Templar142 casting, populated stock Bullet/Image525 delivery and recycling, all eight naturally produced target periodic phases, two genuine caster-owned Storms and the final per-target gate's expiry. The guest completes **3,334 original direct invocations / 942,478 instructions**; maximum single entry13,509. Every reached helper executes original instructions; there are no prefixes or callable helper stubs. Invocation counts include repeated regeneration/timer/image services and do not quantify algorithm coverage.

Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. [Static evidence](evidence/native-storm-static.json) preserves the ground-over-time handler `409a30`, **41 instructions**, and exact state4 dispatch wrapper. [Probe evidence](evidence/native-storm-probes.json) records full returns, original calls, stock VM PCs, Bullet impacts, every per-service HP delta, independent integer scheduling models, RNG validation and source/input hashes. [Asset provenance](evidence/native-storm-assets.json) covers **66 image IDs / 50 distinct stock GRP paths** and selected LO resources. Raw retail bytes remain private.

The [inspector](../../tools/research/inspect_native_storm_117.py) reads the exact EXE as data. The [execution probe](../../tools/research/probe_native_storm_117.py) defaults to a plan and requires its explicit flag. Guest execution stays inside reviewed original instruction ranges and private finite Unit/Sprite/Image/Thingy/Bullet pools. There are no Windows imports, process startup, guest host-file/network calls, renderer or complete game/campaign execution. Flat synthetic terrain/regions, visibility, owners/controllers and ambient original context are explicit fixtures. Frontend research/command admission is separate from consuming an issued order. Direct Bullet-constructor cases prove effect scheduling rather than energy admission.

## Natural High Templar casting reaches positive stock Storm

Fresh stock High Templar67 creation/completion yields energy **50×256** and natural direction200. Its actual location is `(600,400)`, with the requested Storm point `(400,400)` in the native angle/range window. The positive case does not overwrite direction, order, status or energy.

**800 actual `43a320` regeneration calls** raise energy to75×256. Actual issuer `42a340` activates142; full generic consumer434460 invokes native target validation, range/readiness, **75-energy debit**, cooldown/RNG and stock cast animation. The original image/VM/Bullet chain is:

```text
434460 → 417220 → stock HighTemplar SpellCast bytecode
418b20 → 41f4a0 → 420030 → 408f00(weapon84)
40a5f0 → stock Image525 Init signal → Bullet state4
       → 409a30 → 4090e0 → original splash callback409590/4095a0
```

Unchanged weapon84 uses behavior3, explosion2, remove-after **60**, damage **14**, ignore-armor damage type4 and all three splash radii **48**. Target Wraith8 has120 HP and no shields. Source and target owner3 demonstrate native friendly damage at this effect layer; the source remains outside the area.

The bounded composition calls caster timer, full primary dispatcher, caster image service, victim timer, Bullet service and LoneThingy service, in that order. It is a declared arrangement of complete original callers, **not a complete accepted-frame oracle**:

| Declared service tick | Native result |
|---|---|
| 4 | Stock caster bytecode creates weapon84 / Image525 at `(400,400)`, initial Bullet state0, remaining60. |
| 8 | Stock projectile Init signal admits state4; remaining starts decrementing. |
| 10–11 | Caster resumes Guard2 then HumanGuard3. |
| 11 / 18 / 25 / 32 | Four successful14-HP hits; remaining56/49/42/35. |
| 39 | An actual explosion call occurs, but the target's shared gate prevents another hit. |
| 46 / 53 / 60 / 67 | Four more successful14-HP hits; remaining21/14/7/0. |
| 68 | Old remaining0 moves the Bullet to wait/death state5. |
| 69 | Stock image termination and native lists recycle the Bullet; target has **8 HP**, caster energy552 raw. |

The caster's idle return, final damage, Bullet state transition, image termination and recycling are distinct events. They must not be collapsed into one synchronous cast effect.

## State4's byte counter and seven-service damage attempts

The tail dispatch table at **40a704** contains wrapper targets. State4 selects **40a6f4**, which jumps to **409a30**; its table entry is not the handler address itself. The main Bullet loop's state4 wrapper40a651 also calls409a30.

`409a30`, ECX=Bullet, first stores the low byte of `oldRemaining−1` to `+61`:

```text
if oldRemaining == 0:
    actual417a50(target, state5), or actual417a10(point, state5)
    actual417220(sprite, animation1, force1)
else if newRemaining % 7 == 0:
    tail into actual4090e0
```

The zero branch therefore writes255 before changing state. The original target-versus-point branches select the real order helper; the Storm fixture uses the point branch. No damage attempt accompanies that old0 transition. For an initial60, the handler attempts damage at new counts **56,49,42,35,28,21,14,7,0**: nine attempts. Stock Image525's initial wait operand5 accounts for its preceding startup delay; the real VM stores wait4 and resumes on the fifth Bullet image service.

## Per-target `+11b` prevents ordinary stacking

The complete splash callback validates the weapon target and actual bounding-box/radius test40a9f0. For weapon84, it then checks target byte **`+11b`**. Nonzero skips damage; zero becomes1 before invoking actual41e290. This byte belongs to the **target**, rather than an individual Storm or caster.

Actual `43a320 → 4351e0` decrements that byte on the unit's **eight-service periodic phase**. Seven-service Bullet attempts and eight-service target clearing are separate clocks. The natural cast therefore has nine explosion attempts but eight successful14-HP hits: **112 total HP**.

Eight independent fixtures generate phases0..7 using actual target timer calls, create a genuine stock weapon84 Bullet through408f00, and then execute real target/Bullet/Image services to full recycling. A separate integer model predicts every per-service HP change from the stock initial wait, remaining60, modulo7 attempts and modulo8 target clearing; each phase still yields eight successful hits and leaves Wraith8 at8 HP. No periodic-counter or Storm-timer field is manually overwritten.

After the natural cast's Bullet is gone at service69, its target still has gate1 and periodic phase5. **Three further original timer calls** clear the gate at phase0 with no additional damage. Effect deletion does not immediately reset the target's independent periodic state.

## Multiple original Storms share the same gate

Two actual High Templars, owners3 and4, create two populated weapon84 Bullets through their original constructors at the same target point. Real source associations, owner fields, pools and stock images are retained. Complete original hit/notification callees execute for both owner paths; no helper response is authored by the harness.

Simultaneous fields leave the same120-HP Wraith at **8 HP**, rather than doubling112 damage. Both Bullet/Image lifetimes complete and allocated Bullet count becomes0. This is a positive cross-caster consumer contract, independent of earlier AI reservation or shared target-selection scores.

A second composition delays the second constructor by **eight actual target/Bullet services** and uses stock Battlecruiser12,500 HP, stock armor3. An independent scheduling model predicts nine successful14-HP hits across the extended combined window; every original per-service HP delta matches. Final Battlecruiser life is **374 HP** and both Bullets recycle. The extra hit comes from extended timing, while the shared gate continues to prevent simultaneous stacking; the unchanged armor3 does not reduce Storm's type4 damage.

## Boundaries and reproduction

The inspected callback permits the source itself only for weapon84 and has no same-owner exclusion for explosion2. The executed positive fixtures establish friendly targets and overlapping fields; source-at-center self-damage, shield-bearing/Matrix targets, immunity/visibility/detection branches, field-edge geometry, target death during ongoing Storm and complete campaign frame interleavings remain additional compositions. The existing [damage](native-damage.md), [status timer](native-attack-orders.md), [natural casting](native-spell-casting.md) and [populated status effects](native-status-effects.md) reports define separate dependency contracts. Native originals supply the behavior; differently versioned modified sources do not replace these exact-version instructions.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/inspect_native_storm_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --out docs/research/evidence/native-storm-static.json
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 tools/research/probe_native_storm_117.py --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe --data /private/tmp/koprulu-campaign-data --manifest /private/tmp/koprulu-campaign-data/manifest.json --assets /private/tmp/koprulu-storm-assets --asset-manifest docs/research/evidence/native-storm-assets.json --pyms /private/tmp/koprulu-pyms --out docs/research/evidence/native-storm-probes.json --execute-original-code
```

Runtime: x86_64 Darwin/Rosetta, Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4. Probe SHA-256 `54a9344bb07bbb6d98a61522a8f80ab41b7e8d78509a997b6ace20b70478849f`; inspector `e76f6043fe418640c319b5c1c70b9260b928301b403b47e2c66be191612c0b90`; asset manifest `883a78e584ac009cab14106577d6d9d905eb02836c184f75f986d80b6d726b53`. JSON publishes60 local dependency hashes, stock input/asset hashes and the100,000-instruction per-entry bound. The results close declared native casting, scheduling, nonstacking and cleanup contracts; they do not establish complete original campaign parity.
