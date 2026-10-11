# Native 1.17 general DAT requirements

The general order/technology/upgrade evaluator at `0x407570`, four callers and their built-in requirement tables are recovered from the reviewed StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at ImageBase `0x400000`. See [static tables, switch and actual callees](evidence/native-dat-requirements-static.json), [482 isolated original-code cases](evidence/native-dat-requirements-probes.json), and the [inspector/probe](../../tools/research/inspect_native_dat_requirements_117.py).

All **482 checks passed with no helper stubs**. The probes execute original x86 instructions, including offset initializers, evaluator, wrappers, capability predicates and bitset readers in private Unicorn memory. They execute neither WinMain nor a complete game, Windows API or host file/network operation. Original-recipe tests retain the original bytecode. Synthetic streams and explicitly initialized counts/status/DAT fields discriminate individual rules; a successful fixture does not prove an authentic game-state or campaign outcome.

This evaluator is distinct from the [specialized unit-production gate](native-requirements.md). Copying one token interpreter into both call paths would change native behavior.

## Tables and native callers

The requirements are built into the reviewed EXE. Four wrapper calls independently establish their table bases, offset arrays and capacities:

| Domain | Wrapper | Built-in table | u16 offset array | Capacity | Shipped recipes |
|---|---|---|---|---:|---:|
| Orders | `0x407bb0` | `0x502090` | `0x51bb00` | 189 | 112 |
| Research | `0x407dc0` | `0x501ca0` | `0x51f8c0` | 44 | 26 |
| Technology use | `0x407f90` | `0x501de0` | `0x51f918` | 44 | 33 |
| Upgrades | `0x408140` | `0x501958` | `0x524bd0` | 61 | 51 |

The corresponding lazy offset initializers are `0x407d50`, `0x407f20`, `0x4080d0` and `0x408280`. Each table has a zero leading word, ID-tagged records, `ffff` record terminators and a final `ffff` record-ID sentinel. Offsets are word indices, not byte addresses. An offset value `ffff` triggers initialization; remaining `ffff` array entries become zero. Zero means no recipe and produces error23 through the wrappers. Four native cases compare the entire offset arrays against the independently decoded records.

Upgrade data requires special handling: it contains **83 physical records but 51 real upgrade IDs**, with 32 `ff20`/`ff21` section-marker records. The native upgrade initializer explicitly ignores record IDs>=61 while still advancing through their bodies. Its evaluator can select a section beyond an intermediate `ffff`, so treating each terminator as the end of the complete upgrade definition loses later-level prerequisites. The evidence preserves both physical records and grouped recipes.

The general evaluator takes `ECX=context_ID`, `EDX=player`, stack `[unit, table_base, word_offset]`. The context is the order, technology or upgrade being checked. Some predicates use this context directly; it must not be replaced by a token operand.

## General token switch

The switch at `0x407a78` has 37 direct targets for `ff02..ff26`. Ordinary lower words contribute the completed count at `0x52c0c4 + 4*(unit_ID*12+player)`. The following tokens have dedicated behavior:

| Token | Native condition / effect |
|---|---|
| `ff02 unit_ID` | Match current unit's type; also maintains parent/missing-prerequisite bookkeeping from the completed count. |
| `ff03 unit_ID` | Add completed count plus raw all-unit count (`0x529604`), with u32 wrap. |
| `ff05` | Status bit0x2 at +0xdc must be set; failure0/error7. |
| `ff06` | Status bit0x2 must be clear; failure0/error6. This predicate itself has no building-type check. |
| `ff07` | Queue head empty228, both construction-target predicates false, technology byte +0xc8==44, upgrade byte +0xc9==61; failure0/error5. |
| `ff08` | Both construction-target predicates false; failure0/error5. |
| `ff09` / `ff0a` | Technology byte==44 / upgrade byte==61; failure0/error5. |
| `ff0d` | Linked pointer dword +0xd0==0; failure0/error8. |
| `ff0f` | Context technology researched or cheat0x1000. Context11 with unit type103 succeeds without that research test. |
| `ff11` | Status bit0x10 clear **or unit AI pointer +0x134 nonnull**; otherwise0/error5. |
| `ff12` / `ff13` | Status bit0x2 clear / set; failure0/error19. |
| `ff14` | Native movement-capability predicate succeeds; failure0/error19. |
| `ff15` | Native attack-capability predicate succeeds; failure0/error19. |
| `ff16` | DAT property bit0x8; failure0/error19. |
| `ff17` | DAT property bit0x20; failure0/error19. |
| `ff18` | Native transport predicate contributes success; zero contribution can fail-1/error8. |
| `ff19` | DAT property bit0x800; failure0/error19. |
| `ff1a` | DAT property bit0x10; failure0/error19. |
| `ff1b` | Byte +0xc0 nonzero; failure0/error8. |
| `ff1c` | DAT property bit0x40 contributes success; zero contribution can fail-1/error8. |
| `ff1d` | Native right-click action byte nonzero; failure0/error8. |
| `ff1e` | Remember permission for hallucination status at terminal validation. |
| `ff1f` | Select upgrade-level section as described below. |
| `ff22` | Return-1/error21. |
| `ff23` | Return0/error23. |
| `ff24` | Expansion byte0x536720 nonzero; failure0/error26. |
| `ff26` | Status bit0x10 set; failure0/error5. |

`ff04`, `ff0b`, `ff0c`, `ff0e`, `ff10`, `ff20`, `ff21` and `ff25` use the ordinary completed-count fallback if dispatched directly. The generic evaluator therefore does not implement the specialized unit gate's attached-addon/hangar/nuke/explicit-technology tokens. Section markers `ff20`/`ff21` are located by `ff1f`, rather than treated as independent requirement predicates. Unsupported token streams can address outside the nominal unit-count table; no safety guarantee for arbitrary malformed/modded streams is inferred.

### Groups, errors and exceptions

`ff01` continues an OR group without resetting its contribution accumulator. Sequential groups act as AND. The generic evaluator checks producer mismatch at group completion and returns **0/error25**. A missing group with its prerequisite flag set returns **-1/error8**. Dedicated predicates can return immediately before the OR separator is processed. These details matter for retry/error policy, not only whether a requirement eventually succeeds.

The general evaluator does **not** apply cheat0x2000 to ordinary completed-unit prerequisites: a native test with zero completed count still returns-1/error8. The specialized unit gate does apply that cheat. Both use cheat0x1000 in their technology predicates, through different tokens and contexts.

The native `ff11` AI-pointer exception permits a status-bit0x10 unit that would fail the same token without AI. It must not be replaced by an unconditional “not burrowed” requirement. Likewise, general `ff0f` uses the **caller context technology**, with an explicit unit103/context11 exception, rather than consuming a following technology ID. Numeric labels correspond to Lurker and Burrowing in the pinned reference schema. [Teippi unit IDs](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/unit.h), [technology IDs](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/tech.h).

At `ffff`, hallucination status bit `0x40000000` returns0/error22 unless an `ff1e` token was encountered; otherwise success returns1. The direct evaluator does not clear the error global at entry. Its wrappers do, through actual setter `0x4b38b0`, so callers cannot assume a stand-alone successful evaluator clears a previously stored error.

### Upgrade section selection

At `ff1f`, helper `0x49a470(player, context_upgrade_ID)` provides the current level. Level1 selects marker `ff20`; level2 selects `ff21`; every other level selects `ff1f`. The evaluator scans words until that marker without treating intermediate `ffff` as final termination. It then continues the selected section. Native synthetic cases distinguish levels0/1/2 and level3's fallback to the first section. The wrapper normally rejects a level already at its allowed maximum before this selection, so the level3 direct-evaluator case is a width/control probe, not a claim about a legal next upgrade.

Original upgrade recipes were executed at current levels0/1/2. Blank definitions remain blank and produce0/error23 under those fixtures; they were not converted into artificial successes.

## Capability callees

Movement predicate `0x427e60` rejects status bit0x2. Otherwise it obtains right-click action twice and accepts an action that is neither0 nor3. Right-click helper `0x422d90` returns3 for type103 with status bit0x10. Otherwise it reads DAT byte `0x521e78 + type`; if that byte is zero and status bit0x2 is set, helper `0x429250` may change it to2. That helper's exact type set is `{106,111,113,114,130,131,132,133,154,160,167,155}`, consistent with rally-capable production buildings in the reference schema. The type list itself is the local evidence.

Attack-capability predicate `0x41d9c0` accepts a non-130 ground weapon byte at `0x5211d8+type`, except type103 requires status bit0x10 for its ground weapon. It then checks non-130 air weapon at `0x5213a8+type`; Carrier types72/82 can succeed when the byte sum at +0xc8/+0xc9 is nonzero, and Reaver types83/81 when +0xc8 is nonzero. Finally it can use a linked subunit at +0x70 and that subunit's weapons, with the same type103 ground exception. This is the capacity to attack, not range, target, cooldown or successful attack execution.

Transport predicate `0x4381f0` rejects hallucination status. Type42 also requires upgrade24 to be nonzero; other types proceed directly. Its final test is DAT space-provided byte `0x523fe0+type != 0`. The general token uses this result as a group contribution. Numeric Overlord/Ventral Sacs names are schema correlations; upgrade and property reads are native.

`ff07`/`ff08` use the actual construction-target predicates recovered in [production counting](native-production.md). Every callee above is included in the native allowlist; none returns a fixture-success stub.

## Wrapper legality checks

All wrappers reject IDs outside their capacities and producer-owner mismatch, then establish their domain-specific state before invoking the shared evaluator.

**Orders `0x407bb0`:** reject ID>=189 with error15. Require completed status except order IDs39/40. Reject disabled state (error10). Status bit0x10 rejects with error11 except unit type103 or order118. A type7 producer in primary order0x21 is rejected with error8 except requested order1. Ghost-class types `{1,16,100,99,104}` in primary order0x81 are rejected; types68/63 in primary order0x6a are also rejected. Missing recipe returns0/error23; then the general evaluator determines the rest. Eleven native cases confirm completion/burrow/busy exceptions using synthetic empty requirement streams.

**Research `0x407dc0`:** reject technology ID>=44 with error17, incomplete status with20, and disabled state with10. Availability helper `0x49a400` must be nonzero unless cheat0x1000 is set. A research-in-progress bit produces0/error12. A researched byte **or cheat0x1000** then produces0/error13; the cheat therefore bypasses availability but also takes the already-researched rejection branch. Missing recipe produces0/error23; otherwise invoke the shared evaluator.

**Technology use `0x407f90`:** the same ID/owner/completed/disabled checks apply, plus an explicit hallucination rejection with error22 **before** general tokens could grant permission. Availability is required unless cheat0x1000. This wrapper does not independently require a researched byte; its recipe, commonly `ff0f`, performs the context-research test. Energy expenditure, cooldown and eventual ability/order execution are outside this requirements wrapper.

**Upgrades `0x408140`:** reject upgrade ID>=61 with error18, incomplete status with20, disabled state with10. Read current level through `0x49a470` and allowed maximum through `0x49a4a0`; `current>=maximum` returns0/error14. An in-progress bit returns0/error12. Missing recipe returns0/error23; otherwise invoke the shared evaluator including section selection. Allowed maximum is a byte at `0x534368 + player*46+ID` for IDs<46, or `0x53652a + player*15+ID` otherwise.

Research and upgrade in-progress state comes from bitsets, not a unit-list search in these wrappers. Getters `0x4a0560`/`0x4a0590` read:

```text
research: byte[0x536510 + player*6 + (technology_ID>>3)] & (1<<(ID&7))
upgrade:  byte[0x5366c0 + player*8 + (upgrade_ID>>3)] & (1<<(ID&7))
```

They return the selected bit mask, not normalized1. Fifty-two native cases cover byte boundaries, expansion IDs, players0/7 and set/clear bits. This differs from the town satisfaction consumer's scan for units currently in Research/Upgrade orders; both mechanisms must be retained and their writers connected to production order lifecycle.

## Reproduction and evidence boundary

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_dat_requirements_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-dat-requirements-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-dat-requirements-probes.json
```

The inspector defaults to static reads, gates the exact EXE hash before decoding, and never executes retail code without the explicit flag. Results contain probe-script SHA-256, runtime versions/architecture, instruction counts, no-stub status and exact reviewed VA ranges. The 482 passing cases include all four offset arrays, **324 original recipe-wrapper checks** (three upgrade levels), token distinctions, bitsets and wrapper rejection/exception branches.

AISE's generic evaluator implements a broader portable abstraction and some modified behavior, including an unconditional hold-position predicate; it cannot replace this native switch or the AI-specific burrow exception. [AISE requirement evaluator, pinned revision](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/src/datreq.rs#L203). Samase provides primary structural recovery clues for the evaluator and cheat flags, independently of this exact version. [Samase requirements analysis](https://github.com/neivv/samase_scarf/blob/f1681dbac8c7d1ac184dc84918ab7dd5771061ed/src/requirements.rs#L117).

This closes the general requirement tables, specialized token/control differences, wrappers and their reviewed leaf predicates. Complete production/ability/order legality also needs the callers' resource/supply checks, bitset writers/clearers, queuing, resource reservation, completion/cancellation, and execution state machines. Those transitions are not inferred merely from a passing requirements gate.
