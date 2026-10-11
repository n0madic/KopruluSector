# Complete physical movement and Move-order composition

**42 complete cases /5,085 original function invocations pass.** Native-created Marine0, Zealot65 and Wraith8 physically reach authored goals through the original movement dispatcher, speed/heading producers, populated stock images and real spatial-index updates. No host assignment advances their position, Sprite, velocity or movement state. Three cases also execute actual Move6 issuance and complete original object callbacks through arrival, path release and idle-order progression.

The target is StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. [Static witnesses](evidence/native-movement-fsm-static.json) contain12 dedicated ranges /1,126 decoded instructions without tails. The composed [probe evidence](evidence/native-movement-fsm-probes.json) records4,908,812 original instructions and268 visited function starts under764 reviewed ranges. Maximum single invocation is25,240 instructions, below the100,000 bound. Every top-level call returns with checked stack cleanup; there are no prefixes, helper replacements or guest imports.

Sources: [inspector](../../tools/research/inspect_native_movement_fsm_117.py), SHA256 `bdaaf390c670d42647318b24470aae2252e1c81dc9dd0298571e60c750b93074`; [probe](../../tools/research/probe_native_movement_fsm_117.py), SHA256 `05d7247e8cbb7bc5d3874d13fd78ced072cb94085aa1952c757839c69c32e360`. All60 source dependencies are authenticated. [Separate asset manifest](evidence/native-movement-fsm-assets.json), SHA256 `c113f5e97697f5a3e5fd93228b282f15b73b6a6b939981a05a451f0b0af00b31`, contains33 stock image IDs /29 GRP paths, extending the attack assets with images245/246 needed by Wraith movement. Retail payloads remain in private scratch.

## Native object and world inputs

Unchanged Units/Flingy/Sprites/Images/Weapons/Orders DAT columns are bound through the executable's own descriptor triples. Original creation`424900`, completion/show`424da0`, image/IScript initialization, visible lists and spatial insertion execute. Stock acquisition normalization`425610` executes before creation. Native-selected initial headings are retained; the tests do not prepare a favorable direction. RNG seed starts12345 and the independent integer LCG check covers the total counter and all52 source slots, including movement source51 and Guard source29.

The private map has4096-pixel sides and128-tile sides. Its declared graph has regions0/1, uniform raster1, walkable class0/group1 and no terrain contours. Original index records contain the actual created unit, rather than authored candidate-return arrays. Exactly one128-byte path record is available. Resource copying supplies native descriptor state and private GRP/LO/IScript resources; it does not execute the archive/resource loader.

In the three object-callback cases, owner3 is controller2, there are no other units or AI records, and AIScript/resource-area/auxiliary-Flingy lists are empty. Creep, two slow-AI and FOW countdowns start1000. Eight genuine private arrays supply region records through`6b1a74[0..7]`; their contents start zero. This permits the original all-player-zero enemy-strength reset when the fast-AI cursor wraps, rather than suppressing or replacing that callee. Populated computer regions, nonzero hostile cached strengths and regional initialization are separate.

These fixtures establish complete selected physical consumers. They are not an authenticated mission-world initialization or accepted full campaign frame.

## Complete dispatcher and point-to-position sequence

`42ad30`, ECX=CUnit, dispatches byte`+97` through36 entries. A handler's nonzero return repeats dispatch in the same invocation. The probe records every state selected inside that loop, not only the final byte after return.

Ground positive routes compose actual initialization/rest/legal-position paths, [waypoint production](native-path-progress.md), state25 follow, state26 scout, arrival11 and rest9/10. State22 can appear in the same-call initialization sequence; recording only each outer return would miss it. Wraith follows its original state7 instead of using a ground path record. Selected private air-density state is zero; positive congestion/repulsion fields remain separate.

In state25, `42b8d0` first calls`42ac20(unit,1)` for target/path/status changes. If it continues, the full`48eb90` consumer must supply/reuse a waypoint; failure selects22 and returns1. Actual preparation`40bcd0` computes target direction, speed, walking-animation gates and prospective fixed/pixel position. Unit collision`42da30` and terrain/bounds`42d990` then run before physical publication.

For the clear positive path, commit`42ab10 → 40bdb0` writes CUnit pixel/fixed coordinates, updates the real Sprite row/position/direction through`417330/417190`, and calls`419820` to move sorted unit borders. Original populated image update`418b20` executes after each standalone dispatcher call, allowing stock Walking IScript to control subsequent movement. Frame/image ordering therefore affects motion; a nominal Flingy speed alone is insufficient for IScript-controlled units.

After a successful move, source/goal equality selects state11. Otherwise the real path delay poll`48e950` decrements a positive old timer; an old zero sets delay30 and state26. `42c030` queries moving neighbors with`42e1c0 → 421dc0`, releases the original spatial scratch list and chooses25 or27. This batch's sole-unit query finds no positive moving neighbor; the static forecast helper`42e6c0` is reviewed but its positive-neighbor branch is not claimed executed.

Arrival handler`42afe0` releases the actual path through`48e880`, restores the movement waypoint to the final target, and selects the original collision-status-dependent rest/legal-position branch. The selected clear ground routes settle in state10 with zero speed and null path; the free head returns to the same supplied record. Wraith settles at the goal with zero speed in state7 and never consumes the ground record.

## Physical routes and independent per-step checks

The39 standalone cases use three stock types, one same-position target per type, four cardinal directions at64/256 pixels, and four diagonals at256 pixels on both axes. Source is`(512,512)`. They call the actual destination setter`43a0d0`, then paired complete movement/image steps until five settled calls, with a300-step ceiling.

Each step independently requires:

- CUnit pixel position equals the nonnegative fixed-point coordinates shifted right8, and equals the actual Sprite position.
- The native endpoint values indexed by CUnit`+13c/+140/+144/+148` equal position minus left/up and plus right/down stock extents.
- Both complete native axis arrays remain sorted.
- Final position equals the requested goal, speed is zero, path is null and the finite record returns to the free head.

The evidence retains every speed, heading, velocity heading, movement state, waypoint timer, position, native index slot and original-call trace. These invariants and exact output traces are not an independent reimplementation of every speed/turn equation or proof of shortest routes.

For east256 in the standalone fixture:

| Unit | Native initial heading | First position change | First exact goal | Fifth settled call |
|---|---:|---:|---:|---:|
| Marine0 |200 |6 |69 |74 |
| Zealot65 |72 |4 |67 |72 |
| Wraith8 |220 |1 |61 |67 |

The units may turn before useful translation. Wraith initially travels slightly away from the destination while accelerating/turning. Its first speed values are67,134,201,268,335,402 in raw1/256-pixel units. These dates belong to the declared setter, initial heading and paired-step order; they must not become universal movement timing constants.

## Actual Move6 through the object callback

Three additional cases issue the position through`42a340(unit,6,768,512)` and execute complete`445070` callbacks. The authentic main Move entry is **`428660`**, reached through the throttled dispatcher. It has order-state0/1 paths. State0 uses`43a070` for a coordinate target, which calls both the destination setter and`40cbd0` target-waypoint setter; a nonnull unit target instead uses`43a0a0`. Positive setup enters state1. State1 waits for Sprite/movement-target equality before actual`42a5a0` completion.

The object callback executes movement, native unit timers, primary orders, stock images, empty Bullet-list processing and lone sprites with their original ordering. It supplies/restores the genuine unit/Flingy context; all three context globals are zero on return. The accepted-frame counter, command/trigger phases and external main loop are outside this callback.

| Unit | First exact goal | Main order6→2 | Guard2→PlayerGuard3 | Fifth settled callback |
|---|---:|---:|---:|---:|
| Marine0 |69 |74 |75 |79 |
| Zealot65 |67 |74 |75 |79 |
| Wraith8 |62 |65 |66 |70 |

Position arrival, path release and main-order completion are separate events. `42a5a0` chooses stock ReturnToIdle **2** when the queue is empty and AI pointer is null. On its later original dispatch, `41e700` draws the source29 range0..15 wait and, for this human controller, changes Guard2 to **PlayerGuard3**. The next selected PlayerGuard handler is`41ee10`; its ordinary timer/weapon/acquisition code executes with no enemy candidate. A remake must preserve this intermediate Guard state and RNG consumption, not substitute the raw human-idle DAT column or immediately change Move to PlayerGuard at coordinate arrival. [Primary-order completion](native-primary-orders.md), [Guard controller rules](native-unit-ai.md).

All four scheduler countdowns decrease exactly once per callback. The fast-AI region cursor and genuine empty enemy-strength reset execute across the longer timelines. This supplies a selected object-phase composition, rather than proof of every AI/frame subsystem.

## Reproduction and remaining navigation

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_movement_fsm_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-movement-fsm-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_movement_fsm_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-movement-assets \
  --asset-manifest docs/research/evidence/native-movement-fsm-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --execute-original-code \
  --out docs/research/evidence/native-movement-fsm-probes.json
```

Without the execution flag the probe prints its plan. Runtime is Rosetta x86_64 Python3.9.6, Unicorn2.1.4, pefile2024.8.26. Unknown hashes, unreviewed instructions, instruction exhaustion, wrong stack cleanup or any failed invariant stop the run; only the completed passing all-case report is published.

Remaining compositions include authentic-map repeated physical travel, terrain/CUnit obstruction and half/quarter-velocity collision responses, slides/repath/failure/congestion, positive moving-neighbor forecasts, moving attack targets, populated repulsion fields, subunits and native command/frontend admission. The prior [positive indexed blockers](native-path-blockers.md) establish local-route geometry; coupling it to this naturally maintained index and physical consumer is still required. Complete campaigns and rendered gameplay have not run.
