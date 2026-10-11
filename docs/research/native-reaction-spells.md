# Native 1.17 reaction spell permission, targeting and issuance

**586 complete original-code cases pass**, closing weapon-target flags, the technology-target wrapper and selected full reaction decisions through actual target reservation and primary order issuance. The complete decisions cover Science Vessel Matrix, High Templar Hallucination and Defiler Plague, including a reproducible cross-caster shared-score dependency. They do not execute the subsequent spell order's movement, energy debit or effect.

Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`. Evidence: [14 new static witnesses / 449 instructions](evidence/native-reaction-spells-static.json), [586 complete calls](evidence/native-reaction-spells-probes.json), and [inspector/probe](../../tools/research/inspect_native_reaction_spells_117.py). No callable helper replacements, prefixes, complete game, WinMain, Windows APIs or guest host-file/network calls occur. Each entry is bounded by100000 instructions; the longest individual final call executes8254 instructions. Composed cases retain preceding original-call traces separately.

Private fixtures supply declared CUnit/sprite records, spatial intervals, order pools, diplomacy, availability/researched bytes, completed prerequisites and unit properties. The weapon flags, order-to-tech mapping, energy costs and primary-order flags are unchanged effective retail DAT fields. Technology permission uses the unchanged packed table in the EXE and its actual lazy initializer/evaluator. This distinction prevents permissive input state from being mistaken for replacement permission code or an authentic campaign snapshot.

## Weapon-target validity

`0x41dbf0` receives ECX=caster and stack`[weapon_argument,target]`. The weapon argument is truncated to its **low byte**. Byte IDs≥130 return0. With a null target it returns the weapon flag's actual terrain mask`0x40`, not normalized1; thus argument256 addresses weapon0.

For a nonnull target, status`0x04000000` rejects it first. Otherwise the helper requires all applicable predicates from uint16 `weapons.dat.target_flags` at`0x5251a8`:

| Flag | Required target predicate |
|---:|---|
| `01` / `02` | Air when target status bit4 is set, ground otherwise |
| `04` | DAT properties contain Mechanical`40000000` |
| `08` | DAT properties contain Organic`00010000` |
| `10` | DAT Building property bit1 is absent |
| `20` | DAT Robotic property`00004000` is absent |
| `80` | At least one Organic/Mechanical property is present |
| `100` | Target owner equals caster owner |

The flags are combined constraints, not alternatives apart from the explicit air/ground and Organic-or-Mechanical choices. This function does not inspect current research, caster energy, target visibility or diplomacy. Those belong to its callers. **260 native calls** cover every unchanged stock weapon's flags with ground/air targets;96 controlled combinations exercise the individual predicates and their combinations, including ownership and untargetable status. Null/invalid/truncated weapon arguments are separate cases.

Pinned [GPTP `canWeaponTargetUnit`](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/api.cpp#L98) corroborates the property interpretation. The local1.17 instructions and return values, rather than GPTP's1.16.1 addresses, define this contract. The unchanged binary field offsets follow [PyMS WeaponsDAT](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/DAT/WeaponsDAT.py#L243).

## Technology-target validity is separate from area scoring

`0x434130` receives ECX=caster and six stack arguments`[technology,target,fog_type,x,y,error_output]`. Technology and fog type are uint16 inputs. A supplied error pointer receives an error only on a relevant rejection; successful calls leave its previous word unchanged.

Technology IDs **2,4,14,15,17,19,21,22,25,31** return1 immediately, before validating the object or fog type. Complete calls confirm that this includes a null target/fog type228 and a supplied untargetable object. This early success is one layer of targeting; a caller's weapon validity and score checks can still reject the object.

Technology3 uses actual `0x413220(x,y,[0x410000,0])`. The native tile index uses signed low16 pixel coordinates divided by32 with truncation toward zero, and map width from`0x5264b4`; the tile flag pointer is`0x666454`. At least one bit of`0x410000` must be set. Failure returns0 and writes error`386`. The tile lookup itself performs no bounds check here. This branch does not require a nonnull object.

The remaining object path rejects a missing target with fog type228 using error`36e`. A supplied target with status`04000000`, or any supplied fog type other than228, enters the fog-type path: actual `0x420a90` requires type<228 and no DAT Invincible property`20000000`, then `0x435320` supplies the type-only error. Its Infestation12 case accepts type106; Matrix6/Irradiate7 return`36d`, Consume16 returns`381`, and other reviewed type-only cases return`36c`.

An ordinary target with fog type228 reaches actual `0x435380`. Its first gate is target stasis byte`+119`: nonzero returns`384` even before the technology switch. Important complete branches are:

| Technology | Native object check / error on failure |
|---|---|
| Matrix6, Irradiate7, Restoration24, Optical Flare30 | Reject Building DAT property /`36d` |
| Hallucination20 | Reject Interceptor73 /`36c`, then Building /`36d` |
| Feedback29 | Reject Building /`36d`; actual`4118d0` requires Spellcaster property`200000` and no Hallucination status`40000000`, else`532` |
| Mind Control27 | Same owner /`52f`, then Building /`36d`; exact excluded IDs13,35,36,59,73,85,97 /`36c` |
| Consume16 | Require nonbuilding, same owner, actual race helper`428d60` yielding Zerg0, and ID other than Larva35; failure /`381` |
| Infestation12 | Actual`41fed0` requires Completed status and Command Center106 with native rounded life percentage **<50**; failure /`366` |
| Other IDs in the reviewed switch | `36c` |

The Infestation percentage uses DAT maximum HP from`0x5208b0`; a zero maximum falls back to nonzero rounded current HP or divisor1. Thus49/100 passes and50/100 fails; a deliberately artificial0/0 fixture returns success. That last case is a bounded arithmetic witness, not a claim about shipped campaign reachability.

This closes the disputed helper`434130`; it is a technology/object/position check, **not a neighborhood score calculation**. The [GPTP technology-target hook](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/tech_target_check.cpp#L14) is useful corroboration for most branches but gives a different Consume failure label; preserve local1.17 error`381` rather than copying that replacement's label. [Teippi targeting](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/targeting.cpp#L473) separately invokes `CanTargetSpell` after identifying the order's technology.

## Readiness consumes the actual permission recipe

`0x48e580(ECX=caster,EDX=order,[technology])` reads stock order-to-tech byte`51b800`, obtains the stock uint16 energy cost at`51f868` through`4265e0`, compares energy through actual`434b50`, then requires **exactly1** from original`407f90`. Its technology argument is supplied separately from the order's cost mapping; this batch keeps them matched to the stock spell pair.

Thirty-five compositions vary energy one raw unit below/equal/above cost, availability, research, disabled state and hallucination for Matrix, Yamato, Storm, Hallucination and Plague. Costs and permission are separate gates. Energy equal to cost passes the comparison. Permission can still fail afterward. The original lazy sentinel`ffff` is decoded by actual`4080d0`, then the original general evaluator`407570` consumes the EXE's packed recipes.

**Matrix6 has no researched-token requirement**: its original recipe selects Science Vessel9 or hero22. Clearing Matrix's researched byte does not prevent readiness; clearing availability does. The other selected ordinary-caster recipes require their researched technology. This explains why a blanket “every spell must be researched” rule is incorrect. [DAT requirements](native-dat-requirements.md) specifies the complete wrapper and token semantics.

## Complete positive decisions and reservation limits

The following execute complete original`48d3a0` decisions, actual nearest spatial searches and callbacks, original permission, reservation`48e5c0`, and the real primary order pool/activation chain:

| Decision fixture | Observed result |
|---|---|
| Science Vessel9, EMP/Irradiate unavailable, allied Marine attacking, current HP50/max100 | Matrix order141, target pointer, reservation bit1 |
| High Templar67, Storm unavailable, energy high byte200, own Carrier72 | Hallucination order148, target pointer, bit1 |
| Defiler46, Dark Swarm unavailable, qualifying Plague neighborhood | Plague order144, target pointer, bit1 |

Matrix callback`4328d0` requires rounded current HP≤75, max HP≥100, a current attack target, non-enemy relation, no Matrix word`+112`, and no status`100`, then invokes the now-complete target technology6 helper. Hallucination callback`433260` requires the same owner, the technology20 helper, then exact IDs72/70/71/68. The full Hallucination fallback requires the current-energy high byte to equal actual maximum energy;199 fails when the maximum is200. This is distinct from simply having enough energy to cast.

Reservation masks1 and4 are independently tested. Existing matching order returns1 without retargeting or setting a new reservation bit. The dispatcher first handles same-order/**same-target** by returning0 from the whole attempt; a same-order/**different-target** can therefore report1 while retaining the old target and leaving the new target's bit clear.

For a different order, `48e5c0` calls actual`42a380` and then ORs the target's reservation mask **without checking whether the requested order became active**. Real empty-pool calls return1 and set the bit while leaving order1/target0 unchanged. This also happens in the full Vessel decision, which reports1 despite the absent Matrix order. With an older unchanged stock HarvestGas83 queue head, actual issuance activates83; Matrix is left queued with a20-node pool, or absent when the only node was consumed by that head. Reservation still reports1 and sets the bit. See [primary order contracts](native-primary-orders.md) for the underlying pool and retained-head behavior.

Energy remains200×256 in these decision/issue cases. They select and issue; they do not execute the later cast or debit energy. The mask lifetime remains tied to main-order processing, not an invented unconditional per-frame clear.

## Cross-caster shared score input is now executed

Storm selector`4330a0` writes supplied reactive input to global`665d30`. Plague selector`433060` does **not** overwrite it. Both actual callbacks perform96-pixel neighborhood scores through native spatial enumeration. Enemy life must be strictly greater than twice allied life; when the shared input is zero, an additional enemy-score threshold≥250 applies.

Seven complete Storm searches establish249/250, equality at twice allied life, and strict greater-than boundaries. Controlled spatial records place two casters outside the96-pixel score window, with enemy MarineHP100 and alliedHP40 inside it. Sequential original calls with **different caster pointers** give:

| Previous Storm reactive input | Following Plague's supplied input | Plague selection |
|---:|---:|---|
| 1 | 0 | Select enemyHP100; shared input remains1 |
| 0 | 1 | Reject enemyHP100; shared input remains0 |
| 1 | 1 | Select |
| 0 | 0 | Reject |

Two full Defiler decisions then use the same reactive argument0 and differ only in the preceding other-caster Storm input. Previous1 yields Plague144/target/bit1; previous0 yields no action. The result is not explained by a replacement callback or injected return. It follows the actual shared memory reads and writes across complete native calls.

Plague enemy scoring excludes shields while its allied scoring includes enabled shields. Two compositions add200 enemy shields and retain Plague enemy score100/allied40. The static JSON records references to`665d30` in the reviewed combat blocks; its duplicate Broodlings reference reflects overlapping old witness slices, not an additional writer. This batch proves the selected cross-caster sequence, not all possible whole-game interleavings or all writers outside these reviewed functions.

## Reproduction and remaining consumers

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_reaction_spells_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --weapons-dat /private/tmp/koprulu-campaign-data/arr/weapons.dat \
  --techdata-dat /private/tmp/koprulu-campaign-data/arr/techdata.dat \
  --out docs/research/evidence/native-reaction-spells-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-reaction-spells-probes.json
```

Default mode only validates/disassembles and decodes data. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4, x86_64 Darwin/Rosetta. Probe script SHA-256:`32ee023076f5c4c951f18be99d71da54e0506c3eea664544c809dbe42810542c`. Static/probe JSON records dependency script hashes and unchanged DAT hashes; weapons SHA-256:`80de165015519f9bed46ea8858195c2d2d0120b65b9e55e1c0a50b19135f4b62`, techdata:`98f9805ebc8efc707492b7406871298d39d6e94931458aca1f82feaf18a271af`.

Remaining concrete compositions include the other positive caster chains, Nuke availability/score special predicates, Recall regional permission, Dark Swarm's multi-stage selection, selector-specific cargo/reachability predicates, and the eventual spell orders/effects. The full per-unit timer/cache-refresh cadence belongs to active-unit simulation. This batch replaces selected uncertainties in [combat AI](native-combat-ai.md); it does not claim all reaction spells or campaign parity are complete.
