# Native 1.17 carried-item destruction and campaign Bring consumers

This batch closes a connected portable-object lifecycle: genuine automatic pickup, refusal of a second nearby object, original carried-item destruction, full object callbacks that reclaim the removed object and automatically pick up the remaining object. It also executes all seven stored campaign Bring records for portable objectives and all three stored campaign Kill/Remove records for those types on genuinely allocated, carried objects. It extends [special producers](native-special-producers.md), [special orders](native-special-orders.md), [Drop/Flag return](native-powerup-lifecycle.md), [Command/Bring](native-trigger-unit-counts.md) and [visible Death/recycling](native-unit-death.md). Their frozen sources remain unchanged.

## Evidence and execution boundary

The target is the exact original PE 1.17.0.1 at preferred base `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses below are preferred VAs; subtract `0x400000` for RVAs. No 1.16.1 address is transferred to this executable.

[The original-code report](evidence/native-powerup-goals-probes.json) contains **32 passing complete cases, zero prefixes, zero bounded fragments and zero helper stubs**. It records **399 complete original top-level calls**, including **111 complete `445070` object callbacks**, **3,074,082 original instructions**, **8,614 distinct instruction addresses** and **1,612,641 guarded guest writes**. Runtime is x86_64 Python 3.9.6 under Rosetta, Unicorn 2.1.4 and pefile 2024.8.26. All 98 imported research source hashes were checked against the published files. Native code runs in private mapped memory; Windows imports, process startup, guest file/network operations, rendering/audio backends and the full game do not run.

| Cases | Complete original composition |
| --- | --- |
| 15 | All portable types128,129,215–227: actual automatic pickup, genuine second allocation, duplicate refusal, carried-item destruction, automatic replacement and actual recycling |
| 3 | All actual campaign portable-object Kill/Remove records, including the location callback, carried-object branch and subsequent whole-object cleanup |
| 14 | All seven actual campaign Bring records, each with a declared inside/outside creation profile, natural pickup, original cached/fresh queries and actual destruction |

[The probe](../../tools/research/probe_native_powerup_goals_117.py) SHA256 is `e72157399c29a1ffd32f877caf1b04909152c6e76207c26d051798bb86f5756b`. [The inspector](../../tools/research/inspect_native_powerup_goals_117.py) SHA256 is `e361e64359fb54f7c5156784a3bffa9394ad8ce6b28765cda8c4a45b8f522e43`; [its static report](evidence/native-powerup-goals-static.json) contains four new blocks and 26 decoded instructions, with exact file offsets/bytes. Complete trigger selectors, owner/location traversal, spatial query, accounting, allocation, stock IScript and recycling are imported by hash from the frozen native reports.

Every positive item/carrier pair is created by original `469460`, actual CUnit/sprite/image pools, authenticated stock DAT/GRP/LO/IScript and original placement/completion. No host write changes a CUnit/sprite stage, signal, HP, type, order, heading, target, timer or position after creation. There is no external relocation. The host supplies caller inputs: private finite pools, human controller2, raw FORC plus one for runtime force inputs, Current Player0, a flat visible 256-tile terrain/region profile, self-alliance diagonals, auxiliary countdown guards and an incremented frame-number input before each callback. These are declared fixtures, not an executed complete campaign world loader or accepted-frame scheduler.

## Why the next campaign consumer is Bring and item removal

The inspector authenticates every one of the **68 physical CHKs** against [the retail manifest](evidence/retail-manifest.json), separately rechecking all **21,427 UNIT records**. This is physical corpus coverage, not campaign progression or live mission coverage.

Across normal UNIT records, TRIG Create11/44 records and THG2 records with the sprite flag clear, the corpus contains **zero Flag215 producers and zero flag-beacon197/198/199 producers**. Stored TYPE values present are `RAWB`. These observations support prioritizing the actual portable mission objectives; they do not exclude every possible AI/other producer or establish the runtime victory-mode producer. Original `447cb0` tests byte `672945 == 4` for CTF. All executions here retain explicit mode0 and run its genuine guard where reached. Positive CTF delivery/scoring/network-message/audio/UI remains unexecuted, with no claim of CTF parity or universal campaign unreachability.

The actual stored Bring records are:

| Map | Trigger/slot, zero-based | Object | Location | Selector | Comparison | Elevation mask | Stored action1 Victory |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Expansion Protoss04 | 23/1 | Uraj128 | 35 | 17 All Players | At least1 | 56 | No |
| Expansion Protoss06 | 7/0 | Khalis129 | 8 | 0 Player1 | At most0 | 56 | No |
| Expansion Zerg02 | 60/1 | Psi Emitter217 | 105 | 17 All Players | At least1 | 0 | No |
| Terran08 | 2/0 | Psi Emitter217 | 1 | 0 Player1 | At least1 | 56 | Yes |
| Zerg02 | 1/0 | Young Chrysalis216 | 1 | 13 Current Player | At least1 | 0 | Yes |
| Zerg09 | 1/0 | Khaydarin Crystal219 | 1 | 13 Current Player | At least1 | 0 | Yes |
| Zerg10 | 3/0 | Khaydarin Crystal219 | 2 | 18 Force1 | At least1 | 56 | Yes |

All seven condition ignore bits are clear and none follows an earlier enabled None condition. Their flags, full stored condition/action type sequences, owner bytes, map SHA256 and exact record SHA256 are published. A stored Victory action identifies a campaign goal connection; **the whole trigger condition/action VM, other conditions, mission timing, speech/Wait/next-scenario actions and actual Victory execution do not run in these cases**. The other three rows are campaign portable-object presence/absence consumers, not claimed victory triggers.

The three authentic destruction action records are expansion Protoss06 trigger14/action2 Remove At25, Khalis129/location8/selector17; expansion Zerg02 trigger60/action2 Remove24, Psi Emitter217/selector17; and expansion Zerg02 trigger65/action2 Kill22, Psi Emitter217/selector17. Their ignore bit2 is clear, flags are20, and none follows an earlier enabled None action. Raw action bytes and actual MRGN bytes are unchanged inputs; scenario positions/controllers are explicit fixtures.

## Duplicate refusal followed by genuine replacement

For each of the fifteen portable types, original `469460` creates SCV7/owner3 at request `[768,768]` and an item/owner11 at `[776,768]`. The first complete object callback naturally runs Powerup97 and picks it up. The native carrier resource byte becomes4, carrier+`c0` points to the item, item+`d4` points to the carrier, the item is hidden and its carry overlay is present.

Only after that actual pickup does original `469460` create a second item of the same stock type at request `[776,768]`. The first object's original Hide has removed it from spatial occupancy. A second complete callback leaves the second item visible and uncarried while the first remains carried. No synthetic unit/cargo state establishes this refusal. Original worker predicate `431980` rejects nonnull worker+`c0`; the subsequent complete `4270f0(worker,second)` call independently verifies its own nonzero-resource/nonnull-carried-pointer early return. It leaves the entire observed first carry state unchanged.

Complete original `4271d0(worker)` then destroys the carried object. In all fifteen scenes, the first post-destruction callback naturally picks up the second item. The removed first item's sprite is gone in that callback, but its CUnit remains pending; the second callback actually returns it to the unit pool. Final allocated count is2: SCV plus the second hidden/carried item. Exactly one corresponding carry image remains, resource byte is4, reciprocal pointers point to the second pair, second primary order is Nothing23 and the spatial scratch depth is0.

These are four complete callbacks per scene: initial pickup, refusal after the second allocation, replacement after destruction and first-item CUnit recycling. The observed one/two-callback milestones apply to this finite stock resource/guard profile; they are not universal elapsed-time assertions. The second item's original coordinates, saved origin, both carrier/item pointers, overlays and every transition are recorded. No host animation signal or physical-location adjustment makes the composition pass.

## Original destruction semantics and authentic Kill/Remove

New helper `4271d0..427203` uses ECX=carrier and reads resource byte+`e0`. A zero byte returns. A nonzero byte calls real carry-overlay removal `427410`, loads carried pointer+`c0`, and, if nonnull, calls original removal issuer `425700`. It finally clears carrier+`e0` and carrier+`c0`. All positive scenes establish those fields through genuine pickup, rather than supplying scalar alternatives.

`425700` ORs item user-action byte+`93` with4 and tail-jumps to the real Death issuer `42a4d0`. This is removal semantics for the carried object. It differs from [carrier Death→Drop](native-special-producers.md): the item is not made visible at the carrier position. Immediately after destruction it is still hidden with primary Death0, a live sprite, user bit4, and allocated count2. The original helper does **not** clear item+`d4` at that immediate observation; that field still names the live carrier even though the carrier's pointer and resource are zero. The pending object must not be treated as a valid carried pair merely from its stale back-pointer.

The genuine callbacks then release the sprite/images, remove actual owner/accounting/list/index state and reclaim the CUnit. In the three authentic Kill/Remove action scenes the first post-action callback gives sprite0/allocated2/pending=item; the second gives sprite0/allocated1/pending0. The carrier survives with original idle order3, cleared carried pointer/resource and no carry overlay.

Actual global owner traversal `4b73a0` detects a matched powerup through DAT flag`0x800` and item+`d4`, then calls `4271d0` on that carrier. Actual location callback `4b7d00` first recurses into a worker's carried object, with recursion flag`6c4c08`, and takes the same carried-powerup branch on a match. The Khalis location case therefore finds the hidden carried item through the original worker traversal. The selected original owner/type/elevation/location predicates and callbacks run; the host does not supply a query result.

All three unchanged authentic action records reach `4271d0 -> 425700 -> 42a4d0`, followed by real `4b9820` cache invalidation. **Even the authentic Kill22 Psi Emitter action removes the already-carried item through this branch.** It does not kill the worker, drop the item or use an ordinary uncarried unit's visible death path. The numeric type labels are Uraj128, Khalis129, Flag215, Young Chrysalis216, Psi Emitter217, Data Disk218 and Khaydarin Crystal219, matching the [pinned GPTP enum](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/enumerations/UnitId.h).

## Genuine carry counts and original cache lifetime

All seven raw Bring condition records run complete `4b8f20`. Actual MRGN rectangles/masks and raw FORC provide their location/force inputs. Each condition is tested with separately created inside/outside stock objects: inside uses the authentic rectangle center; outside uses a declared x offset320. These are parameter-complete finite scenarios; moving a live worker to a goal through normal pathfinding is not executed here.

Original location traversal counts the visible worker and excludes the hidden item as an independent spatial root. Completed increment `4b9830` tests DAT Worker flag8, follows worker+`c0`, and, for ordinary portable items, increments that type under the worker's owner. The counter address is `6c5150 + 4*(12*type + owner)`. The ordinary/all table is `6c7c10 + 4*(12*type + owner)`; it receives no separate carried-item increment. The preserved Flag215 branch recursively uses the flag object's owner; it was already documented in [Command/Bring](native-trigger-unit-counts.md) and is not generalized from these seven non-Flag rows.

For every inside carried scene, native completed count is1 and ordinary/all count is0. For every outside scene both are0. At least comparisons use the completed family; the one Khalis At most0 condition uses the ordinary/all family. Thus the fresh inside Khalis condition changes from false before pickup to true while carried. After original destruction it remains true. All six At least1 rows are true while carried inside and become false after destruction; their outside profiles remain false. No host counter edits produce any of these results.

The original query cache has a measurable lifecycle. A pre-pickup Bring query primes the location key. Automatic pickup in a complete `445070` callback does not invalidate that same cached result in these scenes; an immediate post-pickup query returns the pre-pickup result. This equality is asserted in all14 cases. Real `4b9820`, followed by the same unchanged Bring record, causes an actual new traversal.

Two authentic inside cases discriminate the result: Khalis At most0 stays cached false until native invalidation, then becomes true; Young Chrysalis Current Player At least1 stays cached false until invalidation, then becomes true because the genuine neutral-owned item is counted under its worker's owner. The latter is naturally created with owner11 and picked up by owner0. This extends the separate Create→Command cache evidence in [special producers](native-special-producers.md); it is not a claim that every mutation or every scheduler path behaves identically. The trigger scheduler's original invalidation boundary remains documented in [native triggers](native-triggers.md).

## Resources, reproduction and remaining consumers

[The resource manifest](evidence/native-powerup-goals-assets.json) contains provenance only:128 image IDs,113 distinct GRP paths,128 LO bindings and134 unique GRP/LO paths. It retains the Trap batch's stock dependencies and adds genuine image249 `unit\thingy\tscGlow.grp`, SHA256 `f083b78855434679dac1a4fe6f68ab16db76e22bd1d36103c18de08eeb16e7d9`, and image291 `unit\terran\tglShad.grp`, SHA256 `85c051ebe4f18af8d001614cdae4316551f448f8ad472ab37467c294eb0850ac`, reached during unchanged stock image progression. This is graphics-resource/native image-state evidence, not actual rendered pixel observation. Private resource bytes remain in scratch.

The resource extractor is the frozen [inspect_native_image_assets_117.py](../../tools/research/inspect_native_image_assets_117.py), SHA256 `4e82f06230a943c7d225c751aac8db7d1ac9c7c47386a95f9915102a7c11a909`. The probe rechecks every GRP/LO hash, stock DAT/IScript hashes and the existing [network.tbl provenance](evidence/native-mission-notification-assets.json). No new guest resource loader or import is substituted.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_powerup_goals_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --out /private/tmp/powerup-goals-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_powerup_goals_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-powerup-goal-assets \
  --asset-manifest docs/research/evidence/native-powerup-goals-assets.json \
  --notification-assets /private/tmp/koprulu-mission-unit-assets \
  --notification-manifest docs/research/evidence/native-mission-notification-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --out /private/tmp/powerup-goals-probes.json \
  --execute-original-code
```

Without the execution flag the probe prints a plan. `--case-group destruction/actions/goals` isolates the respective consumer batch. [General reproduction](reproduction.md) describes authentic extraction/runtime setup; extract the image IDs in the published asset manifest to rebuild the private resource directory.

[Pinned GPTP powerup.cpp](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/powerup.cpp) structurally describes killPowerup and duplicate refusal. [Pinned OpenBW Bring](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L18852) similarly counts carried non-Flag objects under the carrier, but [its trigger Kill/Remove powerup branches](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L19104) retain explicit errors. These are primary-source structural comparisons. The unchanged 1.17 calls and genuine resource/state producers establish the positive contracts above.

Remaining boundaries include positive CTF delivery/scoring/feedback, alternate cargo/resource-state producers, obstacle/height/controller profiles, physical objective movement along actual campaign terrain, full trigger VM/campaign activation and speech/Wait/Victory/next-scenario sequencing. Worker construction while carrying an item reaches a separate original Drop→origin-relocation consumer; it does not run here. The batch proves selected connected object and authentic parameter contracts, not full six-campaign mission completion.
