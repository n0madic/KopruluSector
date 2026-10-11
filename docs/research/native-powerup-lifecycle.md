# Native 1.17 carried-powerup drop and timed return

This batch connects the verified stock pickup to original DropPowerup `4272c0`, hidden-object relocation, reappearance and Flag order17's timed origin return. All fifteen stock powerup types tested in [special orders](native-special-orders.md) run through genuine allocation and pickup before dropping: Uraj128, Khalis129 and types215–227. The positive compositions never supply a signal, heading, CUnit/sprite position or timer after creation; original functions produce these states.

The exact target is PE32 `1.17.0.1`, base `0x400000`, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs; subtract `0x400000` for RVAs. No 1.16.1 address is transferred.

## Evidence and scope

[The probe report](evidence/native-powerup-lifecycle-probes.json) contains **28 complete passing cases, zero prefixes, zero bounded fragments and zero helper stubs**. It executes 508 complete original calls and 683,284 original instructions, visits 4,856 distinct instruction addresses and records 116,689 guarded guest writes. Runtime is x86_64 Python 3.9.6 under Rosetta, Unicorn 2.1.4 and pefile 2024.8.26. No Windows import, process startup, full game, sound backend or renderer executes.

| Cases | Contract |
|---:|---|
| 15 | Actual stock pickup, native carrier relocation, Drop, native carrier departure and powerup Show/query |
| 1 | Actual dropped Flag's 337 native decrements and subsequent original origin-return call |
| 9 | Resource-byte guard alternatives on a genuine already-carried Young Chrysalis |
| 2 | Prepared carrier-tile alternatives forcing original Flag origin fallback |
| 1 | Genuine immediate re-pickup during order17 and its subsequent counter/state writes |

[The probe](../../tools/research/probe_native_powerup_lifecycle_117.py) SHA256 is `73b9771af7c1007c90c2ce9f319cd246b95f865868819f54461d0d04555e1fee`. [The inspector](../../tools/research/inspect_native_powerup_lifecycle_117.py) SHA256 is `f8dfb5933b3ee1e8815c9dec3e9d4317a7173a890d8a4ecb2fda7232e120c156`; [its static report](evidence/native-powerup-lifecycle-static.json) contains five blocks and 183 decoded instructions plus actual order17/97 dispatch-table witnesses. The native report pins 71 imported source files, stock DAT/IScript/resource inputs, executable, runtime, manifest hashes and guarded code ranges.

Terrain is an explicitly prepared flat walkable region with empty contours. Finite private unit/sprite/image/order pools, controller2 inputs, zero shared vision and CTF mode0 are declared fixtures. Original `469460`, populated stock images/IScript, placement and completion create every object. Original `43a270` produces the carrier's requested relocations and all related image/list/index changes; the host does not patch their final positions. This batch also rechecks the hashes and UNIT inventory of all 68 physical CHKs/21,427 records, but its 28 execution scenarios are synthetic stock-resource states, not actual campaign tuple coverage or a full CHK loader run.

## Original drop guard and placement

`4272c0` reads carrier resource byte CUnit+`e0`. It returns immediately when the byte is zero or either low bit is set. It does **not** require that byte to equal 4; nonzero8/12 take the drop branch in the explicit scalar alternatives. The caller must provide a valid carried object at+`c0` for that accepted branch. The report does not claim that arbitrary accepted byte values are naturally produced by gameplay.

The positive path uses actual stock pickup to establish carrier+`c0`, powerup+`d4`, resource byte4 and the LO-following carry image. After native carrier relocation from`[768,768]` to`[1024,1024]`, Drop calls genuine `427410` to remove the carry overlay. It tests the carrier's sprite position through `413220` with included flags`0xffffffff` and excluded flags`0x08000000`, then checks stock powerup placement through `42ed50`. A successful candidate uses the carrier position. A failed tile/placement candidate instead uses the powerup's native origin at+`d0/d2` and clears its internal success flag.

`413220` reads the declared map-width/tile-buffer inputs and succeeds only when at least one included bit is present and no excluded bit is present. The two fallback cases prepare only the carrier destination tile with flags0 or`0x0801ffff` before creation/movement. Full Drop genuinely rejects those respective missing-included/excluded-bit states and returns the Flag to its stored origin. No host location is substituted at this boundary.

Drop then executes actual `47dc30` placement search and `43a270` relocation. In the fifteen ordinary positive profiles the resulting powerup position is`[1024,1024]`. Its sprite remains hidden immediately after Drop. A successfully placed Flag215 receives order17; other powerups, or a Flag using the origin fallback, receive order97. Drop clears powerup+`d4`, carrier+`c0` and carrier resource byte+`e0` through original instructions. The original powerup state retains its saved origin.

The fifteen positive cases move the carrier away through original `43a270` to`[1280,1280]`, then execute the relevant powerup handler. Its genuine hidden-sprite branch calls `422f60` Show, restoring original active/spatial membership and visible state. The nearby query finds no worker, and scratch depth returns to zero. Assertions check cleared reciprocal carry pointers/resource, correct17/97 order, visible powerup, no carry image, and empty hidden list. These are finite geometry/list/image contracts, not rendered visual parity or full pathfinding movement of the carrier.

## Flag order17 and native origin-return counter

The executable's stage3 mapping `439d5c`, indexed by order minus1, sends order17 through case `4397b5` to `427210`; order97 goes through `4398bd` to `427290`. [Pinned Teippi](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/order.h) calls17 `UnusedPowerup` and notes its dropped-Flag role. This batch proves an actual producer: successful native Flag Drop issues17. The historical name does not imply an unexecuted handler.

`427210` first shows a hidden powerup and executes the same real nearby worker query and pickup as order97. It then reads order state+`4e`. At state0 it sets state1 and initializes the 16-bit counter+`ac = 337`. At a later state with a nonzero counter it decrements once and returns; a call with an already-zero counter performs the original origin relocation, executes the CTF notification guard and issues order97.

The natural connected case starts with stock pickup, native carrier movement, actual Flag Drop, native carrier departure and the first genuine order17 call. That first call writes 337. The next 337 calls decrement it to zero while leaving order17; the following 338th continuation call returns the Flag to native saved origin`[784,768]` and changes to order97. Every counter value and position in the timeline comes from the original code. The report retains all 338 continuation observations.

These counts are selected complete handler invocations, not elapsed simulation frames or seconds. The full primary dispatcher/cadence, other status-counter producers and complete object-frame integration are outside this batch; [the special-order dispatch witness](native-special-orders.md#dispatch-and-caller-cadence) must not be turned into a wall-clock timeout without that composition.

## Immediate re-pickup continues through the handler

In the separate natural early-repickup case, the carrier stays at the drop point. Order17 shows the Flag and its real query immediately picks it up again. Actual pickup hides the Flag, restores the reciprocal carry pointers/resource byte4, and changes the Flag's primary order to Nothing23.

`427210` then continues after that pickup. It observes the freshly reset order state0 and writes state1/counter337, even though the primary order is now23. The complete original call therefore returns a hidden, carried Flag with order23, state1 and counter337. This is observed native control flow; it is not modeled as an early return merely because an inner helper changed the order. A Defold implementation should preserve this ordering when composing the same helpers.

## Resources, reproduction and remaining consumers

The batch reuses unchanged [special-order resource provenance](evidence/native-special-order-assets.json), SHA256 `f94be36f3b6a494609a19548375f84ec04140d1056f0d9874f321930665a3a42`, with private bytes at `/private/tmp/koprulu-special-order-assets`, and [mission notification provenance](evidence/native-mission-notification-assets.json), with private `rez/network.tbl` at `/private/tmp/koprulu-mission-unit-assets`. No new retail payload or asset manifest is required. The [resource reader](../../tools/research/inspect_native_image_assets_117.py) and all previous published scripts/reports remain unchanged.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_powerup_lifecycle_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out /private/tmp/powerup-lifecycle-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_powerup_lifecycle_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-recheck \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-special-order-assets \
  --asset-manifest docs/research/evidence/native-special-order-assets.json \
  --notification-assets /private/tmp/koprulu-mission-unit-assets \
  --notification-manifest docs/research/evidence/native-mission-notification-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out /private/tmp/powerup-lifecycle-probes.json --execute-original-code
```

At origin return, `4216e0 -> 447cb0` executes the actual non-CTF mode0 guard and returns. Positive mode4 continues into original network-string selection/formatting/display; that branch and its external/UI dependencies are not counted as executed. Destroying a carried powerup, stock carrier Death→Drop composition, natural cargo-state producers for the scalar alternatives, positive obstacle-driven placement alternatives, delivery/campaign-goal logic and actual mission/player/terrain setup remain separate. [GPTP's powerup source](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/hooks/orders/powerup.cpp) is a structural comparison; the 1.17 original instructions/resources and complete calls establish the contracts above.
