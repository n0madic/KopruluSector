# Native 1.17 campaign-special producers and carrier Death

This batch identifies the actual campaign sources of traps, doors and portable objectives, connects their original producers to authenticated records, and closes genuine automatic pickup → carrier Death → Drop → item Show through full original object callbacks. It also establishes a concrete Create→Command cache interaction. It extends [special orders](native-special-orders.md), [powerup lifecycle](native-powerup-lifecycle.md), [trap combat](native-trap-combat.md), [mission UNIT creation](native-mission-units.md), [trigger unit actions](native-trigger-unit-actions.md), [Command/Bring counts](native-trigger-unit-counts.md) and [visible Death/recycling](native-unit-death.md). Their published sources and reports remain unchanged.

The target is PE32 StarCraft **1.17.0.1**, ImageBase `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs; subtract `0x400000` for RVAs. Earlier-version source names provide structural comparisons, not transferable addresses.

## Evidence categories and scope

[The probe report](evidence/native-special-producers-probes.json) contains **33 complete passing cases, 160 bounded original loop fragments and one import-boundary prefix**. These categories are separate. The positive THG2 section loader is not certified as a complete function. All selected original callees are unchanged native code; there are no helper substitutions, guest Windows imports, full-process execution, renderer/audio backend or guest file/network operations.

| Cases | Category | Contract |
|---:|---|---|
| 15 | Complete compositions | All stock portable-item types: actual full-callback pickup, original carrier Death issuance/Drop, full-callback Show and carrier physical recycling |
| 8 | Complete functions | THG2 non-multiple-of10 length rejection before allocation |
| 6 | Complete compositions | All actual campaign special-powerup Create action records, genuine allocation/properties and native Command cache consumers |
| 4 | Complete compositions | All actual special-powerup UNIT records: original creation and property application |
| 158 | Bounded fragments | Every actual unit-like THG2 record: original per-record loop, genuine allocation/completion and disabled-state producer |
| 2 | Bounded fragments | Actual door/trap records under a contrasting melee profile: forced-neutral admission versus nonneutral rejection |
| 1 | Prefix | Positive whole THG2 loader entry, stopped before its first original Storm allocation thunk |

The [inspector](../../tools/research/inspect_native_special_producers_117.py), SHA256 `4872d006bcbdabfebfa768040d32c5b4bb2c6bf5f8f9643038864f1d0612fb4b`, emits [four blocks/488 decoded instructions](evidence/native-special-producers-static.json) and the authenticated source inventory. The [probe](../../tools/research/probe_native_special_producers_117.py) SHA256 is `89d32ba52da13759a1f1b19861c31ac9ba07268aff8bcc93002e49b116c85142`. Its report pins96 source-module hashes and records746 complete original calls (including320 pure preparation calls for the fragments), 6,179,477 original instructions, 7,434 distinct executed PCs and2,928,600 guarded guest writes. It preserves runtime, code/write guards, DAT/resources, all68 map hashes, per-record/action hashes, actual calls, traces and assertions. Runtime is x86_64 Python3.9.6 under Rosetta, Unicorn2.1.4 and pefile2024.8.26, bounded at250,000 instructions per invocation.

The guest receives effective stock Units/Flingy/Sprites/Images/Weapons/Orders DAT columns copied using the executable's54/7/6/14/24/19 descriptors, original IScript and populated GRP/LO inputs, finite private pools, explicit controller/force/current-player state, visible tiles, flat walkable terrain and empty contours. Actual archive/DAT loading, lobby/runtime-player normalization, map UNIS/UNIx overrides and complete campaign terrain setup are not executed. Supplying stock data directly does not prove those preceding producers.

Full-callback carrier cases use a64×64 prepared map; actual record cases use a256×256 prepared map covering the original coordinates. Genuine `425610` and `40a0e0` initialize acquisition/Bullet state. Eight valid regional AI arrays, self-alliance1/cross-alliance0, auxiliary countdowns2000 and empty strategic lists permit the real full `445070` callback while avoiding positive slow AI, creep-disappearance and periodic fog branches. The host increments the native frame counter as declared caller input; [the outer accepted-frame caller](native-frame.md), pause/clock/turn readiness and whole-game cadence remain outside the composition.

## Actual 68-CHK source inventory

The authenticated physical corpus has68 CHKs/21,427 UNIT records. It contains **2,445 THG2 records**:2,287 with sprite flag`0x1000` and158 unit-like records. All158 unit-like records belong to the selected trap/door set; none is a portable item. The source inventory includes65 nonempty THG2 sections. Physical resources include64 supported-version maps and four version57 extras; selected THG2 records occur in supported-version maps. Counts are physical source coverage, not campaign progression or runtime reachability.

| Unit type | Source kind | Actual count |
|---:|---|---:|
| 203 Floor Missile Trap | THG2 | 13 |
| 209 Floor Gun Trap | THG2 | 51 |
| 210 Wall Missile Trap | THG2 | 3 |
| 211 Wall Flame Trap | THG2 | 11 |
| 212 Right Wall Missile Trap | THG2 | 4 |
| 213 Right Wall Flame Trap | THG2 | 2 |
| 205 Upper Level Door | THG2 | 21 |
| 206 Right Upper Level Door | THG2 | 21 |
| 207 Pit Door | THG2 | 16 |
| 208 Right Pit Door | THG2 | 16 |
| 128 Uraj Crystal | UNIT | 1 |
| 129 Khalis Crystal | UNIT | 1 |
| 216 Young Chrysalis | UNIT | 2 |
| 217 Psi Emitter | TRIG Create | 2 |
| 218 Data Disk | TRIG Create | 2 |
| 219 Khaydarin Crystal | TRIG Create | 2 |

The84 traps and74 doors are in `expterran/terran07` (64 records), `protoss/protoss06` (27), `terran/terran04` (26) and `zerg/zerg05` (41). Nineteen records carry disabled flag`0x8000`. Native source refs preserve raw owners, XY, flags and record hashes; they are not rewritten into editor-library defaults. Names agree with [pinned GPTP UnitId definitions](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/enumerations/UnitId.h).

Uraj's UNIT record is in expansion Protoss04, Khalis in expansion Protoss06, and Chrysalis records in original Zerg01/02. The six Create refs occur in expansion Zerg02 and original Terran04/08, Zerg05/09/10. None has ignore bit2 or an earlier enabled None action. This is raw action reachability within the stored action list; conditions, owner-trigger activation and mission timing are not proven by the inventory. No raw selected Create action targets a trap/door; that statement is restricted to normal TRIG actions11/44, not AI bytecode or all possible runtime producers.

## Original THG2 callback and allocation boundary

The exact native descriptor tables select callback`468ba0` for THG2. The loader takes stack=[section reader,byte length,third argument], `ret12`. It computes unsigned count=`length/10` using `mul 0xcccccccd`, `shr edx,3`, then rejects a nonzero remainder before allocation. The complete native rejection matrix includes lengths1,9,11,19,21,2147483647,4294967294 and4294967295. It makes no imported call or CUnit allocation.

A valid length reaches original Storm allocation thunk`4d001c` at call`468bd5`, then original bounded copy`4c12b0`. Its successful record loop starts`468c0f`; cleanup calls Storm free thunk`4d0022` at`468cc3`. The positive10-byte prefix stops **before** entering`4d001c`. It does not manufacture an allocator return or execute an import.

The158 source-record cases enter the original inner loop at`468c0f` with a host-preallocated exact10-byte record and declared EBP/EBX/EDI caller state, and stop at`468cb6` before cleanup. This is a bounded fragment of the loader. Original functions within it perform actual CUnit allocation, populated image initialization, completion, placement and disabled-state changes. The fragment verifies caller stack/EBP preservation, exhausted EDI count, expected allocated objects and zero spatial-query depth. The original allocation/copy/free lifecycle remains excluded; these fragment results must not be counted as158 whole section loads.

The actual record loop branches in this order:

1. Flags WORD+8 bit`0x1000` selects original Thingy creation`417b80`, using id/XY/owner from the record. The2,287 sprite-flag source rows are inventoried but not executed in this batch.
2. For a unit-like record, types205–208 rewrite **the copied record's owner byte+6 to11** before mode admission.
3. Original UMS predicate`447db0(0)` admits ordinary owners; under the explicit melee profile only owner11 is admitted.
4. Accepted records call actual `469460(type,x,y,owner)`. A null return skips the disabled branch.
5. Flags bit`0x8000` invokes original `428f70` on a successfully created object.

The all-record UMS fixture produces one genuine CUnit for each of the158 records. All74 doors have native owner11 regardless of their raw owner; trap ownership retains its original record byte. Contrasting actual door205/owner0 and trap212/owner0 records under a melee-profile fixture produce one forced-neutral door and zero traps. The runtime controllers are explicit human2 inputs; this does not establish the original campaign controller-normalization producer.

Complete helper`428f70` tests disabled/timer gates, sets status`0x400`, forces stock Disable animation15, sets NoCollide`0x200000`, and sets elevation1 for floor traps and doors. For all six trap IDs it additionally calls `41d4c0(unit,3)`, writes saved field+`e4=0x80000000` and clears byte+`91`. The19 flag-bearing original records genuinely reach this helper; the observed disabled bit matches each source flag. Later animation, enable/disable, acquisition and combat are separate compositions in [special orders](native-special-orders.md) and [trap combat](native-trap-combat.md).

The reference [OpenBW THG2 parser](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L21375) agrees structurally with stride10, sprite flag and forced-neutral doors, but its disabled-unit branch explicitly reports an error. It is insufficient for the original disabled-doodad producer; local`428f70` and original-record fragments supply that contract. Malformed sprite IDs, all Thingy rows and full loader failure/cleanup are separate boundaries.

## Actual campaign powerup Create and UNIT consumers

All six source actions run complete native wrapper`4b64d0`, selector`4b7020` and successful full producer`4261f0`, including real allocation, placement, completion and stock images. Raw action bytes remain unchanged. Actual MRGN and UPRP bytes are bound to their native buffers. Runtime forces use declared one-based bytes derived from the actual FORC source, all runtime controllers are human2 and Current Player is explicitly0. This fixture exercises the original selector logic; it does not execute a whole lobby/player/profile producer or prove the mission chooses that Current Player at runtime.

| Map | Type | Action | Player selector | Original resulting owner |
|---|---:|---:|---:|---:|
| Expansion Zerg02 | 217 | 44 | 6 | 6 |
| Terran04 | 218 | 11 | 0 | 0 |
| Terran08 | 217 | 11 | 13 Current | 0 |
| Zerg05 | 218 | 11 | 13 Current | 0 |
| Zerg09 | 219 | 11 | 18 Force1 | 3 |
| Zerg10 | 219 | 11 | 18 Force1 | 3 |

Each actual action produces one native object. Stored quantity0 becomes one attempt in the wrapper; quantity1 remains one. Property index0 skips property application; valid nonzero indices use genuine `46a420` and the actual selected UPRP bytes. These positive calls use stock availability and genuine finite pools, with no unavailable-type/cap shortcut and no host unit/type/HP/order substitution. They return success, preserve the separate auxiliary dword`673934` through original save/restore, and leave spatial depth0. This is not a claim that the main simulation RNG dword`67393c` is restored.

The four actual special UNIT records also run complete `469460` then complete `46a420(record+12)`, consuming genuine coordinates/owners/property bytes. They allocate one populated object of the original type and owner, with Powerup order97 and zero spatial depth. Stock DAT is used without actual mission UNIS/UNIx overrides; complete UNIT section admission, relationships and map orchestration retain the boundaries described in [mission UNIT creation](native-mission-units.md).

## Positive creation leaves a primed Command cache stale

The six Create cases deliberately prime the original global Command cache before creation. Original `4b8ed0` Command exactly0 for that action's type/player returns1 and sets key byte`6ca6d0=1`. The genuine Create then allocates/completes the unit while leaving key1 unchanged.

An immediate original Command exactly1 returns0, reading the previously copied zero count. Calling real invalidator`4b9820`, then repeating the same original condition returns1 from updated native source data. This sequence is asserted for direct selectors0/6, Current13 and Force18, and the actual217/218/219 producer inputs. No host counter rewrite supplies the result.

This connects a real gameplay producer to the [previously isolated cache contract](native-trigger-unit-counts.md#cache-layout-and-global-copy): positive Create does not refresh a matching global cache on these paths. A trigger implementation that always invalidates Command counts after Create would change this observed native sequence. The synthetic Command query sequence is not the full condition/action VM or a claim that these campaign triggers contain those exact query conditions. Bring/location caches, other creation types/failure branches and the trigger scheduler's next-cycle invalidation remain separate consumers.

## Genuine automatic pickup, carrier Death and item reappearance

All fifteen portable-item types run independently: Uraj128, Khalis129 and215–227. Genuine `469460` creates an SCV7 at request`[768,768]`, owner3, and the item at`[784,768]`, owner11. A full `445070` callback automatically runs the item's Powerup97 query/pickup; no selected direct pickup call supplies the carry state. Every profile picks up on the first callback, producing carrier+`c0`→item, resource byte4, item+`d4`→carrier, hidden item sprite and the original carry overlay.

Complete issuer`42a4d0` calls original Drop`4272c0` **before** clearing the queue, appending/activating Death0 and removing AI. The native issuer clears reciprocal carry pointers and resource byte, removes the original carry image and performs actual drop placement. Flag215 receives order17; other items receive97. The item is still hidden immediately after issuance. No host HP, type, signal, order, heading, location or post-creation unit stage is written, and no external relocation request occurs.

Subsequent full callbacks dispatch genuine carrier Death and stock SCV Death IScript, its explosion/image/sprite lifecycle, pending-body cleanup and CUnit return. The powerup genuinely Shows on the first post-issuance callback at observed`[768,768]`. It does not immediately reattach to the dying worker. The SCV owner list is already empty during the pending animation; allocated count remains2 until actual recycling. At the18th post-issuance callback, the SCV sprite pointer/pending head are null and allocated count is1, leaving only the visible, uncarried powerup with the expected17/97 order and zero spatial-query depth.

The15 scenes execute285 complete full-object callbacks. These callback counts include one automatic-pickup step plus18 post-issuance steps for each item. They are selected finite-state observations, not a universal elapsed-time death duration. [Visible Death](native-unit-death.md) documents the separate SCV explosion/wait/recycler algorithms; this batch adds their positive carried-item producer/consumer composition. Flag order17's longer337-call origin-return contract remains separately established in [powerup lifecycle](native-powerup-lifecycle.md).

## Resources, reproduction and remaining branches

This batch needs no new retail payload or resource manifest. It reuses frozen [trap resource provenance](evidence/native-trap-combat-assets.json), SHA256 `d49d43ecfe0b0b898462076d09efb38fd1c310757dc2a92b6a8efcd245c21f09`, with126 image IDs/111 GRP paths and the relevant LO inputs, plus [mission network.tbl provenance](evidence/native-mission-notification-assets.json). Bytes remain private in `/private/tmp/koprulu-trap-combat-assets` and `/private/tmp/koprulu-mission-unit-assets`. The probe uses frozen Nydus full-object dependencies, SHA256 `e155fdda32cc16946f77cedf7fbe2599b6a1ea859b8c32e704b7e220fbb0dae7`; [Nydus](native-nydus.md), [visible Death](native-unit-death.md) and the preceding frozen reports remain authoritative for their individual phases.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_special_producers_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --out /private/tmp/special-producers-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_special_producers_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-trap-combat-assets \
  --asset-manifest docs/research/evidence/native-trap-combat-assets.json \
  --notification-assets /private/tmp/koprulu-mission-unit-assets \
  --notification-manifest docs/research/evidence/native-mission-notification-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --out /private/tmp/special-producers-probes.json \
  --execute-original-code
```

Without the execution flag, the probe prints its plan. `--case-group death/thg2/trigger/unit` isolates the respective batch. [General reproduction](reproduction.md) covers authenticated extraction and runtime setup. Original executables, AI/IScript payloads and image resources remain private.

Positive CTF delivery/scoring and its message/audio/UI consumers are not executed; pickup/return runs the actual non-CTF mode0 guards. Carried-item destruction, duplicate pickup replacement and original delivery/campaign-goal consumers remain separate from carrier Death→Drop. Complete THG2 allocation/free, sprite-flag rows, full actual map setup/terrain/overrides, creation failures and whole mission trigger conditions/timing retain explicit boundaries. [Pinned PyMS THG2](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/FileFormats/CHK/Sections/CHKSectionTHG2.py) supplies the structural record schema; [GPTP powerup source](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/powerup.cpp) is a structural comparison. The local native instructions, original inputs and complete/fragment/prefix observations define the proved contracts here.
