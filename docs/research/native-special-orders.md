# Native 1.17 doors, traps, powerup pickup and rescue

This batch connects stock campaign-special allocation to selected original order consumers. It establishes door animation signals and state changes, trap initialization and genuine hide/reveal animation completion, worker pickup of every stock powerup type including the Uraj and Khalis crystals, and rescuable-player transfer. It extends [special initialization](native-special-initialization.md), [mission UNIT loading](native-mission-units.md), [ownership](native-trigger-ownership.md), [native image initialization](native-image-initialization.md) and [movement](native-movement-fsm.md). Their published scripts and reports are unchanged.

The exact executable is PE32 `1.17.0.1`, preferred base `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses below are preferred VAs; subtract `0x400000` for RVAs. No address from 1.16.1 is transferred.

## Evidence and input coverage

[The native report](evidence/native-special-orders-probes.json) contains **105 complete passing cases, zero prefixes, zero bounded fragments and zero helper stubs**. The cases execute 1,174 complete original calls and 1,269,266 original instructions, visit 6,602 distinct instruction addresses and record 203,945 guarded guest writes. The runtime is x86_64 Python 3.9.6 under Rosetta, Unicorn 2.1.4 and pefile 2024.8.26. No Windows import, full process, game loop, sound backend or rendering backend executes.

| Cases | Native-created input and selected consumer |
|---:|---|
| 40 | Four door types, Open/Close handlers and five explicitly supplied signal-byte values |
| 8 | Those four doors, actual Enable/Disable Doodad issuance, stock animation signal production and followup Open/Close completion |
| 15 | Genuine SCV pickup of Uraj128, Khalis129 and powerup types215–227; actual query, Hide, carry image and lists |
| 9 | Complete worker/powerup predicate with declared status/cargo/owner alternatives |
| 2 | Complete idle query rejecting a non-worker and a worker's own Flag |
| 6 | Every floor/wall trap's initial state and genuine self-only, no-enemy retry |
| 12 | Every floor/wall trap's Hide and Reveal, including stock IScript signal production and original turning |
| 12 | Rescue with four candidate controller values and three timer values |
| 1 | Completed Command Center rescue transferring its entire owner list, including a remote Marine |

Every selected object is allocated through actual `469460`, with real stock DAT, GRP, LO and IScript resources, populated initialization, native placement and completion. The predicate alternatives explicitly supply incomplete, burrowed, hallucinated or occupied-cargo states after native creation; those gameplay state producers are not claimed here. Private terrain is one flat walkable region with empty contour sets, zero shared vision and declared finite unit/sprite/image/order pools. Player controllers and the local viewer are caller inputs.

The script independently audits **all 68 physical CHKs and 21,427 UNIT records**, verifies their hashes, records section/owner/dimension/relation inventories and rejects truncated UNIT records. These 105 execution cases use synthetic scenarios and stock resources; they are not all actual mission UNIT tuples and do not load actual campaign terrain or determine campaign progression. [Special initialization](native-special-initialization.md#evidence-and-scope) separately executes all 173 matching actual mission records in its selected type set.

The [probe](../../tools/research/probe_native_special_orders_117.py) SHA256 is `1c0b345662a1bd9ebacdd16a2b389eb543b3557d664d69e359d0e3c4502b72b6`. The [inspector](../../tools/research/inspect_native_special_orders_117.py) SHA256 is `ea3a36944a66f738c062759e06cb06da0dfb8ae2a3165673357980a754e0e6e7`; [its static report](evidence/native-special-orders-static.json) contains 28 blocks and 1,046 decoded instructions, plus selected dispatch-table witnesses. The probe pins 69 imported source files, executable and resource inputs, manifest hashes, guarded code ranges, observed callees and exact script opcode offsets.

## Dispatch and caller cadence

The third primary-order dispatch at `439665` loads CUnit+`4d`, subtracts one, bounds the result by186, indexes byte mapping `439d5c`, then jumps through the 100-entry destination table `439bcc`. The inspector extracts the actual selected case instructions and tail destinations:

| Order | Case VA | Original handler |
|---:|---:|---:|
| 97 PowerupIdle | `4398bd` | `427290` |
| 161 RescuePassive | `4398e5` | `48d120` |
| 167 Trap | `4397cd` | `428490` |
| 168 OpenDoor | `43976d` | `4281a0` |
| 169 CloseDoor | `439775` | `428150` |
| 170 HideTrap | `4397d5` | `4285a0` |
| 171 RevealTrap | `4397dd` | `4288a0` |
| 172 EnableDoodad | `439785` | `4281f0` |
| 173 DisableDoodad | `43977d` | `428010` |

Immediately before this stage, `439640` decrements CUnit+`85`; its old nonzero value skips the stage. An old zero reloads8 and calls `4af2c0` before dispatch. This is a static dispatcher/cadence witness. The probes execute actual order issuance and complete selected handlers, not the full primary dispatcher or that preceding helper. Animation counts below are explicit native image-update compositions, not wall-clock durations or a claim that every handler executes each game tick.

[Pinned Teippi order definitions](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/order.h) and [GPTP's order table](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/0_orders/orders_root.cpp) provide structural/numeric naming leads. The executable-backed table above is the 1.17 evidence. These source names do not convert their 1.16.1 addresses into 1.17 anchors.

## Door animation, collision and elevation state

`428150` CloseDoor does nothing without signal bit1. With that bit, it clears only bit1, clears status `0x200000`, restores the stock DAT elevation, issues the stock idle order, unlocks/finishes the primary order through `429d00`, and calls real overlap processing `42e600`. The direct signal matrix preserves other signal bits, including the upper bits of255.

`4281a0` OpenDoor requires the same signal. It sets status `0x200000`, clears signal bit1, sets sprite elevation1, issues the stock idle, sets status `0x400`, and calls `48dd40`, which marks the AI-region change byte `69b560 = 1`. The direct matrix starts from freshly created closed doors; it does not assert that arbitrary previous disabled states are equivalent to this initial profile.

The positive animation composition uses actual `42a2c0` issuance of DisableDoodad173 or EnableDoodad172 followed by complete `428010` or `4281f0`. The wrapper starts stock animation24 or27 and prepends the corresponding Open168 or Close169. Actual `418b20` image progression executes the stock IScript `sigorder` producer; the subsequent original handler consumes its bit. Caller IScript contexts `543300` and `543304` both point to the genuine CUnit. Sound is disabled through its actual guard; no sound helper is replaced.

All four door variants205–208 complete both directions. In this explicit image-update composition, Disable takes18 updates for left variants and16 for right variants; Enable takes18 for each. The final stock idle is23. Original image/sprite mirroring and geometry exist; this is state/list/animation evidence, not rendered door pixels or a full spatial collision/frame test. The [GPTP doodad implementation](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/doodad_orders.cpp) is a structural comparison, with 1.17 instructions and actual calls deciding this contract.

## Trap state and genuine turn-dependent signals

The trap IDs are203,209,210,211,212,213. At state0, `428490` sets NoCollide status `0x200000`, sets floor203/209 elevation0, applies visibility status bits `0x300` through `41d4c0(unit,3)`, stores CUnit+`e4 = 0x80000000`, clears byte+`91`, and moves to state1. At state1 the disabled bit`0x400`, nonzero timers+`117`/+`119`/+`124`, or nonzero primary timer+`54` gate acquisition. With those gates clear, it sets timer+`54 = 15` and executes real `432060 -> 431b70` acquisition. The tested query contains only the genuinely created trap, rejected by original candidate callback `432280`; the query cache depth returns to zero.

Hide `4285a0` with no acquired enemy starts stock SpecialState2 animation14, clears signal bit1 and enters state1. Reveal `4288a0` starts stock SpecialState1 animation13 with the same signal wait. At state1, the signal is cleared and, with an empty queue, stock idle167 is issued through original helpers. All six traps complete both paths with actual stock script signal production.

Floor Missile Trap203 demonstrates why only stepping an image is insufficient. Its Hide script at offset31961 uses opcode35 with operand20 to request a heading-derived movement waypoint, then waits for a current-direction range through opcode60. Native `40cf00` writes the waypoint, not the body heading. `40cf50` calculates the desired heading; the actual `40bee0` composition copies velocity heading, calls `40d1a0` to advance body heading, then `417190` to propagate sprite/image directions. Only then can ordinary `418b20` progression reach the genuine `sigorder`. The probes never supply a final heading or signal in these twelve cases.

| Trap type | Hide updates | Reveal updates |
|---:|---:|---:|
| 203 | 24 | 20 |
| 209 | 16 | 18 |
| 210/212 | 8 | 10 |
| 211/213 | 18 | 18 |

These are native target-direction/heading-commit/image-update compositions on the initial stock profile, not full Flingy movement or original scheduler elapsed times. Positive enemy acquisition, attack-order queue composition, firing, repeated target switching and re-hiding after combat remain separate consumers. The current report does not count the static positive-enemy branch as executed.

## Powerup predicate, query, Hide and carry image

`431980` accepts a candidate only when stock unit DAT property bit8 identifies a worker, status Completed bit1 is present, Burrowed bit`0x10` and Hallucination bit`0x40000000` are absent, and worker+`c0` is null. A Flag215 additionally rejects a worker with the same owner. Other powerups allow a same-owner worker. The nine complete leaf cases distinguish every gate using genuine native-created objects and declared state alternatives.

`427290` PowerupIdle builds sprite-XY bounds of±24, then calls actual `431c20 -> 442b20 -> 442b60` and the original predicate. On a matching SCV it executes `4270f0`: native `422ba0` hides/removes the powerup from the spatial/active lists, the original order helper issues Nothing23, and powerup+`d4` records the carrier. The carrier stores the powerup at+`c0` and ORs resource type+`e0` with4. The real query cache depth returns to zero. Separate full-idle cases reject a Marine and a worker's own Flag without hiding or carrying it.

Original `4151f0` adds the genuine carry image to the worker sprite. Ordinary types215–227 use image`type + 177`; Uraj128 uses958 and Khalis129 uses959. `4116d0` applies the powerup owner when draw method14 requires owner remapping. The image gets bit`0x80` and actual `410380` follows the stock special LO offset. All15 pickups assert the resulting carry-image ID, that flag, and offset`[16,-8]` on the genuine stock SCV profile. They also assert the reciprocal pointers, hidden powerup, active worker, hidden-list membership and released spatial-query cache.

Flag pickup calls the original CTF message guard `421760 -> 447cb0`. Declared game mode `672945 = 0` makes this a complete non-CTF guard return. Positive CTF mode4, network message display, cargo replacement, dropping/destruction of a carried item, flag return timer/order and delivery goals are not executed here. [GPTP's powerup implementation](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/powerup.cpp) supplies the structural comparison; the original 1.17 stock resources and calls supply the positive pickup proof.

## Rescue controller, lists and depot-wide transfer

`48d120` only scans with timer+`54 = 0`; it then sets that timer to1 and builds sprite-XY bounds of±64. Actual `422330` supplies the native sorted-border candidate list. The routine selects the first candidate whose runtime player controller is2. The tested candidate controllers1/3/7 do not rescue; a nonzero timer1 or255 skips the scan. Controller2 with a zero timer transfers the unit through actual `426ed0(unit,new_owner,1)`, issues stock human idle through real order helpers and releases query scratch through `4222b0`.

The ordinary Marine matrix verifies both owner lists and stock live/completed counters after transfer. The CUnit owner changes3→0, while its sprite allegiance remains3 at this immediate core-return boundary. Rescue calls the core transfer directly; it is not the trigger Give wrapper `428df0` that additionally updates sprite ownership. Later frame/color producers must not be silently folded into this immediate postcondition.

The depot branch requires completion (with the native Hive/Lair/remorphing-Hatchery alternatives), grounded-building status and stock ResourceDepot property`0x1000`. The positive case creates a real completed Command Center106, a distant Marine owned by the same rescuable player, and a nearby human Marine. Full `48d120` transfers both old-owner objects, preserves the linked traversal and leaves player0's list `[depot, remote Marine, human Marine]`, with player3's list empty. The candidate's requested position is changed by genuine placement to`[784,824]`; the report retains that actual position, rather than treating its requested input as the final world state.

The local viewer is9, so genuine `446b30` avoids local rescue audio/selection feedback. Real UI dirty flags execute; an external audio backend does not. Hive/Lair/remorphing-Hatchery alternatives, InBuilding-to-BunkerGuard issuance, multiple eligible human tie order, dead/sprite-less members in a depot's list, and full local rescue feedback remain explicit additional profiles. [GPTP's rescuable order](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/rescuable_order.cpp) provides a structural comparison, not the exact executable anchor.

## Private resources and reproduction

[The dedicated asset provenance](evidence/native-special-order-assets.json), SHA256 `f94be36f3b6a494609a19548375f84ec04140d1056f0d9874f321930665a3a42`, contains66 image IDs and56 distinct GRP paths, their LO dependencies, archive priority and hashes. Original bytes remain private in `/private/tmp/koprulu-special-order-assets`. The unchanged [image reader](../../tools/research/inspect_native_image_assets_117.py) extracts those IDs from the user's original MPQs. Notification data reuses [the mission provenance](evidence/native-mission-notification-assets.json) and private `rez/network.tbl` in `/private/tmp/koprulu-mission-unit-assets`. No DAT, GRP, LO, CHK or script payload is published.

After extracting the image IDs listed in this provenance:

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_special_orders_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out /private/tmp/special-orders-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_special_orders_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-special-order-assets \
  --asset-manifest docs/research/evidence/native-special-order-assets.json \
  --notification-assets /private/tmp/koprulu-mission-unit-assets \
  --notification-manifest docs/research/evidence/native-mission-notification-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out /private/tmp/special-orders-probes.json --execute-original-code
```

The guard rejects undeclared code execution, all code writes and guest writes outside declared PE data/private buffers. Finite private pools and stock resource bytes are explicit inputs; resource loading, actual CHK terrain/controller producers, cached combat-strength generation, THG2-to-unit creation, full-frame dispatch/cadence, imports and full visuals remain separate. These contracts advance R03/R37 without certifying complete special-unit gameplay or campaign parity.
