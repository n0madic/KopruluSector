# Original transport boarding, unloading and cargo lifecycle

**Twenty-four complete cases pass through 5,337 original invocations.** Stock Dropships, Shuttles and upgraded Overlords physically approach and load native-created passengers. The original object callbacks unload them, preserve cargo after placement failure, transfer loaded units with their transport, and reclaim transport/passenger deaths. Cargo fields, visibility, motion and pool transitions are produced by original instructions.

The target is StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. Subtract that base for RVAs. This extends [creation](native-image-initialization.md), [object ordering](native-active-iscript.md), [movement](native-movement-fsm.md), [unit death](native-unit-death.md), [ownership](native-trigger-ownership.md) and [Carrier lifecycle](native-carrier.md). Their published sources remain unchanged.

## Evidence and declared inputs

- [Static extractor](../../tools/research/inspect_native_transport_117.py), SHA256 `b3c5974381fdbf6b510d4cfed970db6f90fcb17d7a2a51dc0d74695c78cabe81`, and [static evidence](evidence/native-transport-static.json): **24 ranges / 1,140 decoded instructions**, without undecoded tails. These include a static-only network-table initializer. Positive Bunker and computer transport branches are not executed by this batch.
- [Original-instruction probe](../../tools/research/probe_native_transport_117.py), SHA256 `d931c0dc18211ab6217e42cdbb88633132a2f10d0d7facc80e9361a66d515541`, and [probe evidence](evidence/native-transport-probes.json): **24 complete cases / 5,337 checked returns / 3,643,014 original instructions / 346 visited range starts / 898 reviewed ranges / 71 authenticated source dependencies**. Every invocation reaches the sentinel with exact stack cleanup. There are no prefixes, helper substitutions or executed imports. The independent integer LCG validates final RNG state and native counters.
- **Separate image provenance:** [native-transport-assets.json](evidence/native-transport-assets.json), SHA256 `32b2a52354e88f1ba6e804ddfafd49515d7ee88286c95df9cc31543aaae1d1a7`, authenticates **89 image IDs / 76 distinct GRP paths / 92 selected LO inputs**. Actual stock transport, passenger, turret, death and shield resources populate the image pools. Bunker resources are present for subsequent work; their presence is not a positive Bunker execution claim.
- **Separate string provenance:** [native-transport-string-assets.json](evidence/native-transport-string-assets.json), SHA256 `73f51a759ed0aeaaf32e5e3476174994f2399fde2d374f00e56c9cf8c0a6bc40`, authenticates effective `rez\\network.tbl` from Patch_rt.mpq, 5,713 bytes, SHA256 `0a432e596328f1de50a2aaf8b5c0394e13ad4ec1e4bf37dddb74c2e889761733`.

The fixture binds authenticated **network.tbl at `0x665de0`** and **stat_txt.tbl separately at `0x6ab4f8`**. The original initializer `0x43f0d0` passes `0x4ec2d0=rez\\network.tbl` to loader `0x4c13e0` and stores the result at `0x43f0f0`. That loader is static evidence, not executed initialization. This batch uses the corrected Carrier dependency and both correct table bindings; private retail payloads stay outside the repository.

The engine is x86_64 through Rosetta, Unicorn2.1.4 and pefile2024.8.26, with a 250,000-instruction bound per invocation. Authenticated raw Units/Flingy/Sprites/Images/Weapons/Orders DATs and stock IScript supply original consumers. The normal finite pools contain 16 CUnits, 64 Sprites, 128 Images, 64 Thingies and 32 Orders. Actual creation/completion/show, spatial insertion, image initialization and linked subunit allocation run.

Terrain is a declared clear synthetic 4096×4096-pixel, 128×128-tile map with uniform region1, empty contours, valid eight-player region arrays and explicit visibility. Both the map rectangle `0x543228=[0,0,4096,4096]` and separate dimensions are initialized. Controllers are human and AI pointers null. Prerequisite upgrade24=1 is explicitly supplied for the upgraded Overlord; disabled/timer boundaries explicitly write their tested field. Placement-failure cases explicitly replace the tile-flag plane with unwalkable zeros. These inputs are not a natural campaign terrain or spell-effect producer.

Initial resource/image readiness uses actual creation and 64 original image/Bullet/Thingy services. Passive passengers initially receive actual Nothing23; no authored loaded flag, connected pointer, cargo slot, speed, heading or position simulates boarding. Capacity, guard, ownership and death preparation run selected complete Enter92 handlers at the caller-supplied current-unit context. The principal pickup cases instead issue **only transport94**, then execute full object callbacks which produce the passenger's Enter92 themselves.

Full `0x445070` callbacks receive an externally advanced frame counter `0x52651c`. Unit/Flingy/Bullet contexts must be zero at return. Empty AIScript/resource/auxiliary lists and countdowns starting1000 close their selected guards. These are complete populated **object-callback** calls, including unit/image/Bullet scheduling, rather than the outer accepted-frame, renderer, command receiver or trigger phase.

## Original entry contracts

| Entry | Native caller contract and role |
|---|---|
| `0x437440` | ECX=passenger, stack=[transport], `ret4`; completed/enabled transport, owner, passenger burrow, building-race and free-space predicate. |
| `0x4376d0` | ECX=passenger; main Enter92. Validates target transport, Hallucination and Overlord upgrade, follows it and calls actual load on contact. |
| `0x4377a0` | ECX=transport; main Pickup94. Follows passenger, stops in range, issues passenger92 and activates its own next order. |
| `0x437b50` | ECX=transport; idle93 consumer. |
| `0x437890` | ECX=transport; main MoveUnload112. Actual travel toward goal followed by queued Unload111 activation. |
| `0x437ba0` | ECX=transport; main Unload111. Applies pacing, picks live cargo and finishes or handles placement failure. |
| `0x437c70` | ECX=passenger, stack=[transport], `ret4`; writes original packed cargo and connected/status fields, hides passenger and handles building-specific reset. |
| `0x437e00` | ECX=passenger; actual unload, returning0/1. Relocates the hidden child before placement, then detaches/shows and selects idle. |
| `0x437320` | ECX=passenger, stack=[death flag], `ret4`; packed-slot detach and linked status/visibility cleanup. The death flag1 is reached naturally by death callbacks. |
| `0x438180` | Original transport/passenger unload-position caller adapter to `0x47dc30`. |
| `0x438350 / 0x4382f0` | ECX=transport; actual used-space and any-space-remaining helpers. |
| `0x428bd0 / 0x428b50` | Original generation/index encoder and packed cargo decoder. |
| `0x43a0a0` | ECX=moving unit, stack=[target], `ret4`; actual target-follow and movement waypoint adapter. |
| `0x42c7a0` | ECX=passenger; loaded movement reset, actual zero speed/stop destination and action-bit cleanup. |
| `0x4249e0` | ECX=unit; restores CUnit status bit `0x20000` from DAT property bit `0x08000000`. The field is not treated as a generic “is building” predicate. |
| `0x4b81a0` | stack=[subject,new owner], `ret8`; actual Give frontend. Its live-cargo iterator `0x437da0` invokes `0x4b82c0 → 0x428df0` for passengers. |

Stock Dropship11, Shuttle69 and Overlord42 each provide8 capacity units and return to idle93. Marine0 requires1, Vulture2 requires2 and Tank5/Reaver83 require4. Tank creation also produces its genuine turret6. Capacity units and physical cargo slots are separate limits.

## Packed cargo and admission

The original uses **eight WORD slots at transport `+0xb0`**, not an unbounded linked cargo list. A nonzero slot encodes the unit-pool index in the low11 bits and generation in the remaining bits. The native decoder resolves the pool slot, compares CUnit generation `+0xa5`, and rejects a missing Sprite or Death0/state1 unit. A naturally loaded second pool unit with generation1 has packed value2050. Stale/dead entries therefore do not provide a live passenger merely because the WORD is nonzero.

The private pool's first usable CUnit is `0x5da3a0`. Its packed-index reconstruction uses that native getter result plus `(low11−1)*0x150`; the spatial-border pool uses its separate reserved base `0x5da250`. Conflating those bases produces incorrect cargo identities. Actual active and hidden heads are `0x665ae0` and `0x665af8` respectively.

`0x437440` requires a completed transport and rejects transport status `0x400` and nonzero timer bytes `+0x117/+0x119/+0x124`. Passenger burrow bit `0x10` rejects; passenger and transport owners must match. For DAT building types it additionally requires Terran race1 and required space≤1. For every accepted type it computes stock provided space minus the required space of live cargo.

Hallucination rejection, nonzero SpaceProvided and **Overlord Ventral Sacs upgrade24** are the higher Enter92 caller's contract. Calling only the free-space predicate would omit those gates. Native-created enemy-owner passengers and an unupgraded Overlord are rejected by complete Enter92; the same stock Overlord with explicit upgrade1 accepts.

Four capacity profiles genuinely create and load 8 Marines, 4 Vultures, 2 Tanks, and 8 Marines in an upgraded Overlord. Each totals8 used capacity. The next passenger is rejected, any-space returns0, its status stays unloaded and every existing packed slot remains unchanged. These are complete original command-handler loads, not host-injected cargo records.

## Load, hidden state and physical pickup

Actual load writes passenger `+0x80=transport`, sets status `0x40`, encodes its slot, hides it, and forces its walking animation to idle. A mobile transport load **preserves the passenger's pre-load pixel position while hidden**. Building-specific loading additionally clears `0x20000`, sets `0x20`, deletes movement/path state and places the passenger at the building; that positive branch belongs to the subsequent Bunker batch.

Three physical pickup cases start transport at(200,200), passive Marine at(260,200), then issue only transport Pickup94. Original motion and command propagation produce successful boarding on callback38. Dropship and Shuttle are at(241,199); Overlord at(227,200). The child moves from active to hidden, becomes Nothing23 and receives the actual generation2050 slot and connected pointer. All movement headings, speeds and passenger orders are original outputs.

After actual Unload111 issuance, the selected dispatch reaches unloading after10 object callbacks. Dropship/Shuttle passenger appears at(254,199); Overlord passenger at(231,200). Transport becomes idle93 with timer15; passenger becomes stock Guard2 with ground/air cooldown15 and spell cooldown30. CUnit count stays2 throughout.

## Unload placement, cooldowns and failure

The complete unload helper first rejects a mobile transport with nonzero main-order timer, then status `0x400` and the three disable timers. Each guard is tested after real boarding; cargo, connected pointer and original hidden position remain unchanged.

After those guards it **moves the hidden passenger to the transport's Sprite center before calling `0x47dc30` for placement**. With the whole tile plane unwalkable, original placement returns0. Cargo/status/connected fields remain loaded, but the child's position changes from(220,200) to(200,200). Failure is not a complete state rollback. Restoring the explicit walkability plane and retrying the same actual unload helper succeeds without modifying passenger fields.

Successful unloading sets parent timer15, moves the passenger and subunit to the chosen original point, idles IScript, detaches the packed slot, shows the passenger, resets movement, restores status from DAT and activates queued/default idle. For a **mobile** transport, a normal armed passenger receives actual upgraded ground/air cooldowns and spell cooldown30; Reaver83 instead receives main-order timer30. These are caller-boundary outputs, followed by normal decrement on later callbacks.

A loaded Shuttle issued MoveUnload112 toward(400,260) physically reaches(389,256) and unloads on callback93. Native Marine cooldowns are15/15/30; native Reaver main timer is30. The near-goal original range test queues and activates111; the host does not teleport either object.

With two genuine Marine loads, full-object Unload111 releases passengers on callbacks **1 and20** in the declared initial phase. The parent timer is15 after the first release, but main-order throttling determines the next dispatch; a rewrite that always releases the next passenger exactly15 callbacks later changes this trace. No timer fields are overridden in this case.

Human mobile **Unload111 placement failure finishes to idle93 while retaining cargo**. A full object callback with the unwalkable plane proves this behavior. A direct unload helper returning0 and the higher-level command's decision are separate contracts.

## Ownership and death

Actual Give frontend `0x4b81a0` transfers a genuinely loaded Dropship and Marine from owner3 to4. Its original cargo callback transfers the passenger rather than leaving a foreign-owner hidden child. Packed generation slot, connected pointer and loaded bit remain intact. Live/completed counters for both types change `[owner3=1,owner4=0] → [0,1]`; Terran raw used supply changes `[6,0] → [0,6]`; original owner lists move both entries. No ownership, count, supply or list field is assigned by the host.

Real Death issuance followed by full object callbacks closes both selected lifecycle profiles. Transport Death destroys its cargo, clears packed slots and moves both units to pending deletion on the first callback. The passenger is recycled by callback2 and the Dropship by18; all16 CUnit slots are free. Passenger-only Death clears its slot/connected link, recycles the hidden passenger by2 and leaves the Dropship alive with15 free slots. Actual image termination and pending CUnit recycling run; no direct recycler call substitutes for scheduling.

## Reconstruction comparisons and limits

The pinned [Teippi cargo implementation](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L1685) intentionally replaces original packed slots with a linked list and acknowledges different replacement ordering. Its mobile-unload cooldown branch agrees with the authenticated1.17 caller, but its failed human Unload rewrite leaves the command pending. The original complete failure case instead selects idle93. The pinned [GPTP unload hook](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/load_unload_proc.cpp) applies its cooldown block under GroundedBuilding; the1.17 instructions apply it when that bit is absent. These reconstruction choices are not transferred as original specifications. GPTP1.16.1 supplies structural leads, never unverified1.17 addresses.

This batch closes ordinary human mobile transport capacity, selected complete boarding handlers, physical command-produced pickup, unloading travel/pacing, disabled/placement failures, cargo Give and natural death/recycling. Explicit prerequisite/disable/terrain inputs remain fixtures. Computer transport planning, loaded worker resource cargo, stale-generation exhaustive cases, transport command reception, full outer frames, Bunker weapons and Nydus pairing/travel remain separate. Static Bunker ranges and populated Bunker assets here do not close those behaviors.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_transport_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-transport-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_transport_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-transport-assets \
  --asset-manifest docs/research/evidence/native-transport-assets.json \
  --string-manifest docs/research/evidence/native-transport-string-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --execute-original-code \
  --out docs/research/evidence/native-transport-probes.json
```
