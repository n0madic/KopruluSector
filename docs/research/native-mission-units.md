# Native mission UNIT admission, creation and initial state (1.17)

This batch connects actual stock UNIT records to original allocation, populated image/IScript initialization, placement, Show, completion defaults, GuardAI admission and property application. It also establishes the setup-to-runtime player copy, record eligibility, selected creation failures, duplicate-record processing and initial trap state. Relationship cleanup, a normal positive Hatchery creep path and later campaign-special primary orders retain explicit boundaries.

The target is StarCraft **1.17.0.1**, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, PE ImageBase `0x400000`. Every address below is a preferred VA for that exact image; subtract `0x400000` for RVA. The guest executes unchanged original instructions and real callees in Unicorn 2.1.4, x86_64 Python 3.9.6 under Rosetta. No helper substitution, Windows import, WinMain, complete game loop, guest file operation or network operation executes.

## Evidence and coverage

The [static report](evidence/native-mission-units-static.json) contains **24 blocks / 925 decoded instructions**. The [execution report](evidence/native-mission-units-probes.json) contains **588 complete cases, 3 import-boundary prefixes and 3,422 bounded loop fragments**, all passing. These categories remain separate: a fragment reaching its declared decision point is not a completed section loader. The batch records **1,855,257 executed instructions, 5,570 distinct executed PCs and 296,462 guarded guest memory writes**.

The tools are [the static inspector](../../tools/research/inspect_native_mission_units_117.py) and [the probe](../../tools/research/probe_native_mission_units_117.py). Their SHA256 values are `ed98c2ef3f2cbbb4daef252495b1bc9cc51dc88a575244d1976308526b34f500` and `fa04041360fde4823143612eea9059d6a5c916a9b46e4df338f3565a74b27f90`. The report pins the recursively imported research modules, raw DAT inputs, executable, runtime, resource manifests, map hashes, per-record hashes and native case references. The guest-write guard admits declared private buffers and non-executable PE data sections, and rejects writes into original code.

| Cases | Executed contract | Boundary |
|---:|---|---|
| 33 | Actual Marine0, SCV7 and Zergling37 records from the version-59 Terran tutorial | Complete `469460` and `46a420`, with populated stock images and explicit runtime/terrain inputs |
| 3 | Actual Drone41 / Marine0 records from the six-record Zerg tutorial | Complete per-record consumers; the enclosing version-57 map is a raw corpus extra |
| 120 | Marine/Drone × all 12 owners × runtime controllers 1,2,3,4,7 | Complete creation, completion/default-order selection and GuardAI guard/allocation |
| 21 | Every stock type with availability word bit `0x200` | Complete expansion-disabled rejection before allocation or RNG consumption |
| 4 | Empty CUnit pool, empty sprite pool, exact 1700 cap and outside-map point | Complete creation failure and authentic network.tbl notification lookup under a nonlocal viewer |
| 72 | Non-multiple-of36 section lengths, including large unsigned lengths | Complete outer-loader rejection before its allocator |
| 68 + 12 | Full player copy over every actual OWNR input; single-slot copies | Complete pure setup-to-runtime copies with explicitly prepared other record fields |
| 1 | User-select slot flag (`0x673f08`) clear and set bounds (formerly labeled computer-slot bytes; corrected after the function atlas, [F03](atlas/f03-final-fixes.md)) | Complete producer composition; only slots 0–7 become set |
| 1 | Repeated same-owner start locations | Complete consumer composition; later record wins |
| 228 | Stock type IDs through the trap predicate | Complete pure predicate, including negative door/powerup cases |
| 24 | Types203–213 and215–227 through completion-post | Complete original entry on a visible object fixture; special graphics construction and later orders are separate |
| 1 | Third actually created Marine used by relation fixtures | Complete native allocation/completion |
| 3 prefixes | Hatchery creep graphic selection; matched/unmatched Nydus relation disposal | Stop before the excluded import |
| 3,421 + 1 fragments | Deduplicated record-admission tuples; connected duplicate-record processing | Real complete callees, bounded original outer-loop instructions, host-preallocated input buffers |

All **68 physical CHKs / 21,427 UNIT records** are audited. Every map has one UNIT section and no duplicate serial value in its actual records. Every record has case references for two admission profiles: its raw copied OWNR controller and a declared all-human runtime controller. Equal consumed arguments are deduplicated; marker XY remains part of the key. These are input/branch contracts, not 21,427 allocation calls or a campaign walkthrough.

The physical set contains **31 version59, 33 version205 and four version57 CHKs**. The descriptor-support distinction established in [the briefing/loader report](native-briefing-loader.md) still applies: 64 supported-version inputs, four raw extras. In particular, `campaign\zerg\tutorial\staredit\scenario.chk` is version57. Its six records are useful exact byte inputs but do not establish playable campaign progression or acceptance of that whole map. The smallest supported-version input used for connected records is `campaign\terran\tutorial\staredit\scenario.chk`, version59, with 46 UNIT records. The 33 tested mobile records use stock UNIS default flags; its 13 other records are not claimed as completed allocations here.

## Resource and caller state

[Image provenance](evidence/native-mission-unit-assets.json), SHA256 `6803290fd93639c550b5d84109da995111e40f6fe9b6c639efa5ae37f97dc490`, covers **32 image IDs / 26 distinct GRP paths**, their original archive and hash, and selected LO files. The unchanged [image asset reader](../../tools/research/inspect_native_image_assets_117.py) prepares private inputs. The new selected resources include Hatchery shadow71 and construction images104–106. Native building initialization actually replaces Hatchery70/71 with construction104/106 before the creep prefix.

[Notification provenance](evidence/native-mission-notification-assets.json), SHA256 `73f51a759ed0aeaaf32e5e3476174994f2399fde2d374f00e56c9cf8c0a6bc40`, covers original `rez\network.tbl`. Its role at `0x665de0` is independently traced in [the audiovisual report](native-audiovisual.md). It is distinct from map STR and stat_txt. The execution fixture binds the hash-verified original table, and actual `43f0c0 → 4c13b0` retrieves error strings 61/62.

Stock Units/Flingy/Sprites/Images/Weapons DAT columns are copied to the exact native descriptor destinations. Stock Orders columns, iscript.bin and GRP/LO inputs are hash checked. This is host preparation of original inputs, not execution of DAT/MPQ loading. The preceding successful allocation and image contracts are documented in [image initialization](native-image-initialization.md), [unit creation](native-unit-creation.md) and [populated lifecycle](native-populated-lifecycle.md).

Finite CUnit/sprite/image/order/GuardAI pools, seed12345 with enabled RNG, explicit owner/controller/viewer arrays, zero upgrades, zero shared vision, one region, flat walkable terrain, empty contour sets and prepared bitmap/occupancy arrays are caller fixtures. The 33 supported-map cases consume original UNIT bytes, but do not load that map's actual MTXM/terrain, briefing or full player setup. General custom UNIS/UNIx composition is separate; [map overrides](native-map-overrides.md) and [unit properties](native-unit-properties.md) establish the preceding field-level contracts. Completion-post cases start with a declared visible synthetic object and empty image list; their complete returns do not prove native allocation of every special type.

## Ordered section processing and admission

Outer entry `0x468f30` accepts stack `[section source, byte length, third argument]`, pops12, and returns success/failure. It computes the unsigned count using `mul 0x38e38e39`, `shr edx,3`, then checks `length-count*36`. A nonzero remainder returns0 before allocation. Length 0 and valid positive multiples reach Storm allocation; this batch does not replace that imported allocator. The successful path allocates/copies the record buffer, allocates32 start-location bytes and enters the record loop at `0x468fc5`.

Within the loop, `ESI` points at record+16 and `EDI` tracks remaining records. The original branch order is:

1. `46a280` handles type 214 before ordinary owner/controller admission.
2. For owner <8, controller 0 is rejected. Controllers1–4 and7 are admitted; other values are rejected. Owners8–11 bypass this controller check.
3. `469600` applies profile byte `0x672949`. When nonzero it admits only owner 11 and types 176–178,188,89,90,93–96.
4. `447db0(0)` reports UMS only when profile bytes+`75`,+`79`,+`7f` are all zero. In UMS, a slot flagged in `0x673f08` additionally needs unit group byte `0x523c40[type] & 0x80`.
5. Admitted records call `469460(type,x,y,owner)`. A null result skips property/relation work and continues to the next record.
6. A created unit receives `46a420(unit,record+12)`, then relation-record helpers receive its serial at+0 and related serial at+32. The next record is36 bytes later.

**The mode byte aliases UMS profile+79:** `0x6728d0 + 0x79 = 0x672949`. A fixture cannot independently set mode 1 and UMS true. The discriminating matrix preserves this coupling instead of treating two views of the same byte as independent switches.

The admission fragments start at `0x468fd3` and stop before `0x469038` (creation) or `0x469085` (skip/next). Their actual predicate callees return normally. The connected duplicate-record fragment starts at `0x468fc5` with explicitly prepared buffer/count/frame inputs, runs creation/properties/relation guards and start-location copying, then stops before Storm disposal at `0x4d0022`. These entry fragments exclude the loader's preceding allocation/copy stage and its subsequent cleanup/finalization.

## Setup copy, markers and duplicate records

The previously executed OWNR loader writes `0x673d50 + 36*player`. Pure `0x479bf0` copies all 12 complete36-byte records from `0x673d48` to runtime base `0x6726c8`; `0x479c40` copies the slot indexed by CL. Both preserve the controller byte rather than normalizing it. The real raw OWNR corpus contains controller values0,3,5,6,7. Therefore this copy alone cannot establish the later computer/human runtime values1/2. Lobby/campaign assignment and the full profile producer remain dependencies; a copied raw 6 is deliberately a different admission input from a prepared 2.

`0x479c80` clears exactly eight slot bytes at `0x673f08`. `0x478a20` sets one byte only when CL <8; `0x479610` returns0 for IDs ≥8. The sole setter's caller `0x46b5c0` flags a slot whose race byte is 5 (User Select) and rewrites that race to 6 (Random), so `0x673f08` holds **user-select slot flags**, not computer-slot bytes. Corrected after the function atlas ([P23](atlas/p23.md), applied in [P21 `0x45dfc0`](atlas/p21.md)); the evidence keys `computer_slot_*` keep the earlier name. The slot table's full lifecycle is not inferred from the runtime controller byte alone.

`46a280` writes marker XY to `start_buffer + owner*4`; a later marker for the same owner overwrites it. For a matching local owner, camera tiles become `max(0,(x>>5)-10)` and `max(0,(y>>5)-6)`. This leaf has no owner-range validation; the 32-byte outer allocation must not be generalized to arbitrary malformed owner IDs. The tested duplicate uses owner3.

The connected duplicate-record fixture uses an actual stock marker and two copies of an actual Marine record, with explicitly changed XY. Both copies have the same serial and create distinct CUnits/sprites in record order; allocated count becomes2 and the spatial border count 4. Serial does not deduplicate ordinary creation. Both positions remain the declared point under this fixture, so the case does not establish arbitrary overlap resolution or pathfinding parity. The actual 68-map corpus has no duplicate serial values; the duplicate case is synthetic compatibility evidence.

## Complete per-record creation and defaults

`469460` uses ECX=unsigned low16 type, EDX=x, stack `[y,owner]`, pops8. If availability word `0x524708[type]` has bit `0x200` and expansion byte `0x536720` is0, it returns0 before RNG/allocation. All21 stock types carrying that bit are tested on this rejection path.

The positive chain is:

```text
469460
  → 424900 parent/subunit allocation and populated stock Init
  → optional 4c0790 / 4c0aa0 creep preparation
  → 424db0 completion
       → 424f40 prefinish/full HP and shields when build timer is nonzero
       → 426600 / 47dc30 placement and 43a270 relocation
       → 424de0 completed accounting, Show and initial order
  → 4b2010 guard admission/allocation
then outer loop → 46a420 original record properties
```

Completion-post `424de0` sets completion bit 1, performs native completion/supply accounting and Shows an ordinary hidden body. Its controller-based initial order is161 for controller 3,162 for controller 7, the stock computer idle byte `0x520d50[type]` for controller 1, otherwise stock human idle byte `0x520e38[type]`. A subsequent DAT property-bit `0x800` branch stores sprite XY in unit+`d0`/+`d2` and issues the stock human idle again. The 120 connected Marine/Drone cases verify all 12 owners and the five selected controller values with real primary-order code. `4b2010` can assign a genuine private GuardAI for eligible controller 1 objects; other controller values return through its unchanged guard.

The supported tutorial's33 actual mobile records run original property application after creation. The assertions cover stock/default HP arithmetic, owner/type, completed/invincible bits, final initial order and cleared hidden sprite bit. Source valid-property/ability bytes are consumed unchanged; this is more than testing an independently prepared inert CUnit. It still excludes the map's13 nonmobile/start/resource records from complete allocation coverage.

Failure wrappers are also complete for the declared nonlocal viewer. Empty CUnit or sprite pools produce error 62; the exact 1700 cap produces61. Invalid bounds produce no error string. `424900` consumes source 14 before these failures; each case retains the free CUnit head/count, makes exactly one source 14 draw and has the independently expected LCG state. `425020` uses original network.tbl lookup and `46ae50` suppresses local presentation because owner 6 differs from viewer 0, then clears the error word. A local notification proceeds to imported string copying and a time/UI dependency; that path is not claimed as complete here. Placement failure inside `469460` has another formatting/message/removal path and remains separate.

## Traps, doors, powerups and relationships

The complete predicate `429160` returns1 only for **203,209,210,211,212,213**: floor/wall trap types. Types204–208 (hatch/door variants) and215–227 (powerups) return0. In completion-post, those six trap types call `41d4c0(unit,3)`, which replaces status bits `0x300` with`0x300`, then store unit+`e4 = 0x80000000` and byte+`91 = 0`. The 24 completion-post cases verify these differences with actual stock DAT and the declared already-visible fixture. They do not execute trap targeting/firing, door opening/closing, powerup pickup/drop or rescue order161/162's later behavior.

Scanner Sweep33 and Map Revealer101 instead take a static completion-post branch that moves them from the hidden list to the special list `0x665b00/04`, then calls `423260`. Positive allocation and this connected special-list path are not executed by the present batch.

The corpus has **464 records with nonzero related serials**, including 14 type 134 Nydus records and 450 Terran building/addon records. The static original relation-record helpers allocate 20-byte nodes through Storm: serial+0, related serial+4, unit pointer+8, previous node+12, next node+16. `4693d0` admits type 134 with nonzero relation; `469360` admits race-group1/Terran building state with nonzero relation. The field at UNIT+10 is not consulted by these reviewed loop/helpers; converter labels alone must not become an invented native gate.

The Nydus resolver `4698e0` searches earlier nodes for `earlier.related_serial == current.serial`. On a match it writes both unit+`d0` pointers, then repairs the remaining list before imported disposal. Two original prefixes execute this matched/unmatched algorithm over declared relation nodes and actually created Marines: the resolver itself does not inspect the unit types. With two possible earlier matches it selects the nearest prior node; the farther object's field remains unchanged. These cases establish the pointer/search operations before the first free import, not completed Nydus creation, node disposal or later transport.

The addon resolver `469810` additionally classifies the DAT addon bit, checks the native addon/parent type relationship through `4b3850`, and calls `4359c0` on a match. Positive pairing, list disposal and a complete all 464-record relation pass remain dependencies. Native post-load `438c20` visibility/fog finalization and `42ffc0` matrix invalidation are statically present after outer cleanup; this batch does not execute that whole post-load pass.

## Reproduction

Private retail paths below are examples. The report contains provenance and hashes, never the retail payload. See [reproduction.md](reproduction.md) for the pinned runtime and initial MPQ/CHK extraction.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_image_assets_117.py \
  --app /private/tmp/koprulu-retail.jJ8D4z/extracted/app \
  --stormlib /private/tmp/koprulu-pyms-campaign/PyMS/FileFormats/MPQ/StormLib.dylib \
  --scratch /private/tmp/koprulu-mission-unit-assets \
  --retail-manifest docs/research/evidence/retail-manifest.json \
  --image-ids 17,18,38,39,54,55,70,71,104,105,106,151,152,179,234,235,236,237,239,240,243,244,247,248,250,251,252,253,256,257,266,275 \
  --out /private/tmp/mission-unit-assets.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_pixel_assets_117.py \
  --app /private/tmp/koprulu-retail.jJ8D4z/extracted/app \
  --stormlib /private/tmp/koprulu-pyms-campaign/PyMS/FileFormats/MPQ/StormLib.dylib \
  --scratch /private/tmp/koprulu-mission-unit-assets \
  --retail-manifest docs/research/evidence/retail-manifest.json \
  --paths 'rez\network.tbl' --out /private/tmp/mission-notification-assets.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_mission_units_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out /private/tmp/mission-units-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_mission_units_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-mission-unit-assets \
  --asset-manifest /private/tmp/mission-unit-assets.json \
  --notification-manifest /private/tmp/mission-notification-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out /private/tmp/mission-units-probes.json --execute-original-code
```

The creation probe defaults to printing its plan. Actual original-code execution requires `--execute-original-code` and the declared output path. No complete executable startup occurs.

## Primary comparisons and remaining scope

Pinned [PyMS UNIT decoding](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/CHK/Sections/CHKSectionUNIT.py#L44) supplies the 36-byte field schema. Its link-bit names describe editor data, while the native witnesses above establish the actual gates. Pinned [OpenBW UNIT consumption](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L21561-L21672) provides a structural comparison for ordered record creation, owner admission and properties; it does not replace this image's native profile checks or prove arbitrary equivalence. [BWAPI special type names](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/include/BWAPI/UnitType.h#L231-L255) supply names for the numeric trap/door/powerup IDs. No 1.16.1 address is transferred into the 1.17 report.

Remaining connected work includes full profile/lobby/campaign owner/controller assignment; native section allocation/copy/duplicate-section/cleanup composition; custom map override-to-allocation composition beyond the default-flag sample; remaining resource/building/special allocations; normal Hatchery creep graphics requiring excluded CRT context imports; positive addon/Nydus loader pairing and disposal; post-load visibility/fog initialization; active special states, rescue, door/trap and powerup order behavior. The present contracts improve R03/R37 but do not close those requirements or certify a mission load, full campaign or visual parity.
