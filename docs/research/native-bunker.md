# Bunker boarding, hidden weapons and destruction

**Twenty-one complete cases pass through 4,233 original invocations.** A real Bunker Pickup95 command produces passenger Enter92, original hidden BunkerGuard5 acquires an enemy, and populated stock IScript produces actual Bullets and damage. The passenger receives kill credit. Original unloading and destruction distinguish a usable exit from a blocked exit; all selected CUnit reclamation runs through complete object callbacks.

The target is StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. Subtract that base for RVAs. This extends [ordinary transports](native-transport.md), [creation](native-image-initialization.md), [attack orders](native-attack-orders.md), [projectiles](native-projectiles.md), [weapon effects](native-weapon-effects.md), [death](native-unit-death.md) and [ownership](native-trigger-ownership.md). Published dependencies remain unchanged.

## Evidence and fixture boundaries

- [Static extractor](../../tools/research/inspect_native_bunker_117.py), SHA256 `6947483127dd20174ff380c088f9f0a4a8b57dbc5c73c48003f619f31ad23feb`, and [static evidence](evidence/native-bunker-static.json): **6 ranges / 340 decoded instructions**, without undecoded tails. The network initializer is static-only; weapon-range switch targets and type-map bytes are also recorded.
- [Original-instruction probe](../../tools/research/probe_native_bunker_117.py), SHA256 `af6f028822e1203eb391387b99ef43bca27fcf7f6fe1343a7b4cb29ab0df7b88`, and [probe evidence](evidence/native-bunker-probes.json): **21 complete cases / 4,233 checked returns / 3,327,648 original instructions / 458 visited range starts / 905 reviewed ranges / 74 authenticated source dependencies**. All source hashes were checked fresh after the final combined run. Every invocation reaches the sentinel with exact stack cleanup; no prefixes, helper substitutions or imports execute. An independent integer LCG checks native RNG state/counters.
- **Separate image provenance:** [native-bunker-assets.json](evidence/native-bunker-assets.json), SHA256 `14a187418d764315568e4e1b085080f4c4d89a881e410ab5928a7b96d4f488cf`, authenticates **102 image IDs / 88 distinct GRP paths / 104 selected LO inputs**. This includes Bunker304–306, authentic Terran construction330/331, destruction334/rubble335, Marine/Ghost/Firebat resources, Bullet resources and the Bunker attack LO `unit\\terran\\pillbox.loa`. Private retail payloads stay outside the repository.
- **Separate string provenance:** [native-bunker-string-assets.json](evidence/native-bunker-string-assets.json), SHA256 `73f51a759ed0aeaaf32e5e3476174994f2399fde2d374f00e56c9cf8c0a6bc40`, authenticates effective `rez\\network.tbl`, SHA256 `0a432e596328f1de50a2aaf8b5c0394e13ad4ec1e4bf37dddb74c2e889761733`. It binds to `0x665de0`; authenticated stat_txt.tbl binds separately to `0x6ab4f8`. The original network loader is statically witnessed, not executed.

The engine is x86_64 through Rosetta, Unicorn2.1.4 and pefile2024.8.26, with a 250,000-instruction bound per invocation. The finite private pools normally contain16 CUnits,64 Sprites,128 Images,64 Thingies and32 Orders. One muzzle-failure case starts with a genuinely empty Thingy pool. Authenticated raw DATs, actual live acquisition-range normalization, IScript, GRP and LO data supply original consumers.

Bunker125 is created by actual `0x424900` and completed by `0x424da0`; the initial building image replacement `0x423330 → 0x415590` executes authentic construction330/331 resources. This is a complete creation/completion composition, not a paid SCV construction producer. Passengers and passive enemy targets are likewise originally created and shown. Nothing23 initially keeps them passive; combat begins from the original Pickup95 command. Capacity/death/unload/Give preparation uses complete direct Enter92 handlers, distinctly from the full command-produced boarding cases.

The clear synthetic4096×4096 map has128×128 tiles, uniform region1, empty contours, native spatial insertion and eight valid player-region arrays. Owners3/4 are human, self-allied and mutually hostile; AI pointers are null. Visibility and auxiliary countdowns are declared fixtures. The blocked-exit death case replaces the tile plane with unwalkable zeros after genuine loading. No loaded flag, connection, cargo slot, child heading, speed, target, damage, HP or kill count is assigned to simulate the successful combat sequences.

The physical approach case starts its passenger at(300,240); the other combat cases start at(220,200), already within contact range of the building. Sixty-four actual image/Bullet/Thingy services prepare initial images. Complete `0x445070` calls receive an externally advanced frame counter and return with zero current-unit/Flingy/Bullet contexts. They execute populated object scheduling, without the outer accepted-frame, renderer, trigger, command receiver or campaign runtime.

## Native entry contracts

| Entry | Original role |
|---|---|
| `0x437850` | ECX=Bunker; Pickup95. Validates the passenger through `0x437440`, issues its Enter92, then resumes the building's queued/default order. |
| `0x4376d0 → 0x437c70` | ECX=passenger; Enter92 then real cargo load. Writes packed generation slot/connected pointer and hides the unit. |
| `0x439e20` | ECX=hidden unit; hidden primary dispatcher. The `0x439ecb` adapter transfers BunkerGuard5 to `0x41e760`. |
| `0x41e760` | ECX=passenger; BunkerGuard5. Enables its full-auto status, attempts existing-target fire and periodically acquires a new target. |
| `0x420bd0` | ECX=shooter; actual target validation/range/angle/cooldown and attack-animation producer. Its loaded branch calls the Bunker muzzle producer. |
| `0x420520` | ECX=shooter; authentic LO-based muzzle Thingy creation, direction/elevation and visibility initialization. Allocation failure is guarded. |
| `0x420f00` | ECX=shooter, stack=[weapon], `ret4`; original maximum weapon range, including loaded-building and type/upgrade additions. |
| `0x421060` | ECX=shooter, stack=[target], `ret4`; complete native in-weapon-range predicate using stock extents and rectangle distance. |
| `0x437ba0 / 0x437e00` | Original Unload111 and passenger placement/detach/show consumers. A grounded building bypasses the mobile pacing gate. |
| `0x42a4d0 → 0x41e630 → 0x4253a0` | Death issuance and real unit teardown. Grounded cargo attempts actual unload before deciding whether a passenger must die. |
| `0x4b81a0 → 0x437da0 → 0x4b82c0` | Actual Give frontend and live packed-cargo owner transfer. |

Stock Bunker125 provides4 capacity units. The shared admission predicate requires a completed/enabled same-owner transport and, for DAT building types, **Terran race1 and required space≤1**. The higher Enter92 still performs its own transport/Hallucination checks. Marines0, Ghosts1 and Firebats32 each genuinely fill four slots; a fifth is rejected without disturbing existing cargo. Native Vulture2, Zealot65 and Zergling37 are rejected. The predicate is not an infantry-name whitelist; these six types are the executed admission profiles.

## Loading and hidden BunkerGuard

Real grounded load sets passenger status `0x40|0x20`, clears status `0x20000`, sets `+0x80` to the Bunker, writes its packed generation slot and hides it. It deletes/reset movement and places the passenger at the building's center. This differs from mobile transport loading, which can retain the hidden child's original pixel position.

The hidden dispatcher converts the loaded passenger to BunkerGuard5. The original `0x41e760` explicitly recognizes types **0,20,1,16,100,99,104,32,10** before enabling status `0x08000000` on the shooter and any linked subunit. These recognized hero/type branches are static evidence; the executed shooters here are0/1/32. Other loadable types must not be assumed to fire merely because admission succeeded.

The handler first calls `0x420bd0` on an existing target. If that permits continuation, it updates the native waypoint from the order target. Otherwise a zero main-order timer becomes15, and actual `0x432060` acquisition supplies the new target. A successful acquisition clears byte `+0x85` for the next dispatch. The probe does not inject that pointer or dispatch phase.

The three contact-range cases issue only Bunker Pickup95. Original hidden acquisition, animation, Bullet creation and impact deliver the first hit on callbacks **7/7/12** for Marine/Ghost/Firebat. Damage arguments are respectively1536/2560/2048 raw, weapons0/2/25, divisor1, attacker=the loaded passenger and attacking player3. Stock Firebat IScript produces three weapon25 Bullets; the first native effect is the recorded stopping boundary, not an assertion that every flame segment contributes the same final total.

A separate Marine begins at(300,240), physically approaches through the complete original movement/object chain, loads on callback22 and first hits a target at(360,200) on29. Its final pixel, fixed-point and Sprite positions agree at the Bunker center(200,200). No host-produced movement or favorable heading substitutes for that approach.

## Range, muzzle position and finite graphics capacity

The original maximum-range helper adds **64 pixels whenever status `0x20` is set**, before its type/upgrade branches. Complete direct calls before/after actual Enter92 loading produce:

| Stock shooter/weapon | Before loading | After loading |
|---|---:|---:|
| Marine0 / weapon0 | 128 | 192 |
| Ghost1 / weapon2 | 224 | 288 |
| Firebat32 / weapon25 | 32 | 96 |

These are helper contract values, not a claim about a displayed UI range label. The predicate uses actual inclusive unit rectangles. A loaded Marine at(200,200) and an originally created Marine at(409,200) have gap192 and are accepted; target center410 produces gap193 and is rejected. No weapon DAT range or unit extent is overridden for these boundaries.

`0x420520` reads the real Bunker main image's attack LO. Non-Firebat headings choose `((heading+16)>>5)&7`, direction in32-unit steps, and Thingy Sprite377/Bunker overlay306. Firebat32/Gui10 uses the same eight-way LO point but chooses `((heading+8)>>4)&15`, direction in16-unit steps, and Sprite378/flame overlay. Original successful creation sets elevation to shooter Sprite elevation+1 and updates visibility. Populated stock images/IScript execute in the full callbacks; no renderer executes.

With zero available Thingies, actual muzzle allocation returns null and the guarded original producer continues to the real firing animation/Bullet impact. The Marine still reduces its victim from40 to34HP and the Thingy list stays empty. A cosmetic pool failure therefore does not cancel this selected native attack. The normal finite-pool success paths also execute; a reconstruction's changed allocator cannot be used as proof of this original capacity contract.

## Kill attribution and actual reclamation

The natural Marine combat case continues until the originally created victim is recycled. Seven native6HP hits kill the40HP Marine; actual Death/image termination/pending recycling completes by callback113. The **passenger's kill byte becomes1; the Bunker's remains0**. Enemy Marine live/completed counters become0 and only Bunker+shooter remain allocated. Neither incoming damage nor victim Death is directly authored in this case.

## Unloading, destruction and owner transfer

Actual Unload111 releases all four genuine passengers in **one complete object callback**. Successful first unload sets parent timer15; the grounded-building branch lets the remaining three unload during that same call. All connected pointers and packed cargo clear, hidden flags/status `0x40|0x20` clear, and original status `0x20000` is restored from DAT bit `0x08000000`. Ground/air/spell cooldowns stay0: the mobile unload penalties do not apply.

In the declared clear profile, original placement chooses(200,200), (200,224), (224,224), (224,200). These are actual selected-placement outputs over the supplied terrain/spatial data, not universal exit positions on all campaign geometry.

Actual Bunker Death also attempts those unloads on its first object callback. With usable placement, four Marines survive at full40HP and the Bunker enters pending deletion. Its CUnit recycles by callback28, leaving4 allocated/12 free slots. With the entire tile plane explicitly unwalkable, each unload fails and original Death is issued to the remaining loaded passengers. By28, all five CUnits are reclaimed and all16 slots are free. No free-pool record or physical recycler invocation is supplied by the host. Authentic rubble335 remains an independent Thingy; CUnit reclamation is not a claim that every debris Sprite has expired at that boundary.

Actual Give frontend transfers a loaded Bunker and all four Marines to owner4. Their original packed cargo/connected links remain live; source owner list becomes empty. Bunker live/completed counts change `[1,0]→[0,1]` and Marine counts `[4,0]→[0,4]`. Owner/cargo/count fields are original outputs rather than authored transfer fixtures.

## Primary-source comparisons and remaining scope

The pinned [Teippi range helper](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit.cpp#L1277) also adds64 for a unit inside a building. Its [Bunker muzzle reconstruction](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/bunker.cpp) supplies structural LO/direction leads; its redesigned allocation is distinct from the original guarded finite pool. [GPTP's BunkerGuard hook](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/base_orders/stopholdpos_orders.cpp) supplies a1.16.1 structural reference. No1.16.1 address is transferred without matching the authenticated1.17 code, and agreement with a reconstruction is not counted as original execution.

The batch closes the selected natural Bunker weapon, physical boarding, range, graphics-failure, kill attribution, four-unit unload, clear/blocked destruction and Give compositions. Live computer AI, every loadable hero/nonshooting passenger, loaded worker resource cargo, upgrade application during occupancy, terrain-specific exit geometry, target classes/statuses beyond the declared profiles and the outer campaign runtime remain separate. Initial Bunker creation/completion is explicit; paid SCV construction belongs to the construction producer, and Nydus pairing/travel is the next special-entity batch.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_bunker_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-bunker-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_bunker_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-bunker-assets \
  --asset-manifest docs/research/evidence/native-bunker-assets.json \
  --string-manifest docs/research/evidence/native-bunker-string-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --execute-original-code \
  --out docs/research/evidence/native-bunker-probes.json
```
