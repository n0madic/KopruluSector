# Populated gas entry, hidden service, exit and return

**21 complete cases pass through 1,635 original function invocations.** Native-created SCVs, Drones and Probes physically reach their own completed refinery, enter the hidden-unit list, wait, emerge with stock cargo, and return it to a native-created racial depot. Depleted sources yield two gas. Three further complete object-callback cases emerge with cargo when no depot exists. Twelve separate predicate cases distinguish completion, owner and resource type.

The target is StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. Addresses below are preferred VAs; subtract that base for RVAs. This extends [populated mineral/cargo/queue harvesting](native-harvesting.md), [movement](native-movement-fsm.md), [primary orders](native-primary-orders.md), [populated allocation](native-image-initialization.md) and [object phase ordering](native-active-iscript.md). Their published probe sources and evidence are unchanged.

## Evidence and declared world

- [Static inspector](../../tools/research/inspect_native_gas_harvesting_117.py), SHA256 `daf2bc98936f96101b77ad01445e979e70067ad6f7d0b3a072166005fff75826`, produces [ten ranges / 499 decoded instructions](evidence/native-gas-harvesting-static.json), with no undecoded tails. Both hidden-primary switch tables are retained separately from executable bytes.
- [Original-instruction probe](../../tools/research/probe_native_gas_harvesting_117.py), SHA256 `9d25c5345892bd77f2c64c4685a4e53c9734395e9cf6dca610f5884ba0627ebd`, produces [21 complete cases](evidence/native-gas-harvesting-probes.json), **12,076,414 executed instructions**, **346 visited range starts**, **853 reviewed ranges** and **66 authenticated source dependencies**. Every invocation reaches its sentinel with the original stack cleanup; there are no prefixes or substituted helpers.
- [Separate image provenance](evidence/native-gas-harvest-assets.json), SHA256 `9ceecc937ec2e9a7edde160770340c812a2e59293661c9837969df8d4788a4c7`, covers **109 image IDs / 88 distinct GRP paths**. It adds original geyser smoke profiles430–440 to the previous harvesting inputs. Retail payloads remain in private scratch storage.

Execution uses Rosetta x86_64 Python3.9.6, Unicorn2.1.4 and pefile2024.8.26, with a **250,000-instruction bound** per invocation. The observed maximum is103,677: the complete Hatchery creep-candidate scan exceeds the earlier100,000 limit on the declared empty-creep map. The larger bound changes only the host stop limit; no original helper or branch is replaced. Original RNG source/total counters and final seed match an independent integer LCG. [RNG contract](native-prng.md).

The inputs are authenticated Units/Flingy/Sprites/Images/Weapons/Orders DAT, IScript, GRP and LO resources. Original allocation `0x424900` and completion/show `0x424da0` create the worker, resource and optional depot. Native sprites, images, lists, fixed positions and spatial indices are populated and subsequently maintained by original code.

The world is an explicit clear4096×4096-pixel,128×128-tile map, uniform region1 with group1, no obstacle contours and one finite128-byte path record. Both map size globals and the separate WORD rectangle `0x543228=[0,0,4096,4096]` are supplied. The128×128 creep-record plane is zero, and its pointer/dimensions are initialized. Positive new-creep production remains separate; the original no-candidate scan executes. [Creep contracts](native-trigger-creep.md).

Owner3 is human controller2. Eight valid empty AI-region arrays and empty AIScript/auxiliary lists are supplied. Slow-service countdowns start10,000. The host writes the external logical-frame counter `0x52651c`, then invokes complete **`0x445070`**. It never advances worker position, speed, order stage, timer or cargo. Current-unit/Flingy/Bullet globals must all be zero after every object callback. Full outer accepted-frame readiness, command and trigger phases, genuine Town ownership, complete map loading and campaign play are outside these cases. No Windows import, startup, renderer or guest host-file/network operation executes.

## Exact selected caller contracts

| Entry | ABI and behavior |
|---|---|
| `0x424330` | ECX=resource, stack=[worker], `ret4`; completed own type110/149/157 predicate. |
| `0x4237a0` | ECX=worker; WaitForGas82 validates its target/carry type and, at state0, invokes the actual resource queue `0x424230`. |
| `0x4235c0` | ECX=worker; HarvestGas83 enters, services timer/exit placement, creates cargo, releases the resource and resumes queued orders. |
| `0x43a770` | ECX=hidden unit; applicable subunit processing, movement, status/timers, hidden-primary dispatch, selected secondary services, then populated image update. |
| `0x439e20` | ECX=hidden unit; priority cases precede its throttled dispatch. Order83 maps to `0x439e5e`, which tail-calls `0x4235c0` on every visit. |
| `0x424590` | ECX=worker, EDX=resource, stack=[output_point, output_heading], `ret8`; nearest-depot geometry and actual footprint fitting. |
| `0x429eb0` | ECX=worker, stack=[order_id, queue_boolean], `ret8`; simple primary-order append through the actual queue allocator. |
| `0x429de0` | ECX=worker, stack=[order_id, before_node], `ret8`; inserts a nontarget order before the supplied queued node. |
| `0x424750` | ECX=resource, stack=[waiting_worker], `ret4`; native doubly linked waiting-list unlink and worker link/reference clearing. |
| `0x4928f0` | Original `xor eax,eax; ret8` leaf reached by placement; its constant false return is present in the executable bytes, not a host stub. |

The twelve complete `0x424330` cases use genuine allocated worker/resource objects. Each race accepts its own completed refinery and rejects another owner, a completion bit cleared explicitly in the fixture, or a native-created mineral176. These are predicate checks, separate from the nine physical object-callback cases.

Static visible HarvestGas/WaitForGas validation additionally accepts carry type0 or any carry byte with bits1/2 set; another nonzero carry byte rejects. A hidden Sprite flag`0x20` bypasses HarvestGas's target/completion/owner/carry validation. That bypass is visible in the exact instructions; invalid hidden targets and unusual carry combinations are not positive runtime cases here.

## Entry, hidden timer and list publication

Only the worker receives actual target order79 through `0x42a380`. Its original movement and resource selection produce:

`Harvest79 → WaitForGas82 → HarvestGas83 → ResetHarvest151 → ReturnGas84 → MoveToGas81`.

Resource queue service sets worker active byte`+0xce=1` and its resource reference at`+0xd0`. HarvestGas first sets status`0x800000` and inserts ResetHarvest151. At state0 it executes `0x430390(worker,1)`, which hides/removes the worker from active spatial membership, then writes state5 and timer37.

The worker newly added to the hidden list is subsequently serviced within that same complete object callback. Actual `0x43a770` calls timers before hidden-primary dispatch, so the first returned hidden snapshot has **timer36**. There are36 consecutive hidden snapshots; the final hidden snapshot has timer1, and the next callback reaches zero and performs exit. Treating37 as37 further hidden object callbacks would change this measured phase order.

With worker/resource/depot present, spatial boundary count changes **6→4→6** across hide/show. Without a depot it changes **4→2→4**. Hidden-unit index slots are removed from live spatial storage; visible snapshots validate native sorted endpoints, stock worker extents, Sprite coordinates and fixed coordinates shifted right8. Hidden snapshots still require matching worker/Sprite/fixed positions, but do not read removed indices as live borders.

## Exit geometry and resource release

When timer0 is reached, `0x424590` first executes nearest-depot helper `0x431f00`. With depot and resource present it computes the heading from resource to depot through `0x40ba10`. It adds a32-pixel vector at heading+`0xc0` to both endpoints, expands the resource rectangle using the worker's opposite extents plus1, and clips the shifted line through `0x48c6d0`.

The first actual `0x47dc30` attempt receives that rectangle, search size1 and flags`0x8000`. If it fails, the original helper retries with a null rectangle and the same flags. The six depot-present cycles take the first successful attempt. The fallback-failure/retry paths are statically recovered here but are not positive runtime cases.

When the nearest depot is null, the original branch fits around the worker's current Sprite point with null rectangle, size1 and flags0. It does not overwrite the supplied heading in this branch. All three no-depot cases execute this complete alternate placement path successfully.

After a successful fit, the original order sets heading, commits position through `0x43a270`, shows the worker through `0x422f60`, extracts stock cargo through `0x423fd0`, then releases active resource service through `0x424130`. It removes old ResetHarvest151 nodes, appends ReturnGas84 if no other queued order exists and gas is carried, inserts a fresh151 before the next node, and resumes through `0x429d00`.

If exit fitting fails, exact instructions set timer18 and return while hidden. That branch requires a further blocked-world probe; its existence does not establish a played blocked-exit scenario.

## Complete measured cycles

Worker starts(512,512), refinery(768,512), depot(256,512). The six depot-present cases use source amount1500 or0; they differ in cargo quantity/overlay, while the selected route and timing remain equal within each race.

| Native worker / resource / depot | First hidden callback | Exit callback / point | Deposit callback / point | Cargo overlay for8 /2 |
|---|---:|---|---|---|
| SCV7 / Refinery110 / CommandCenter106 | 67 | 103 / (700,544) | 198 / (326,544) | 403 /404 |
| Drone41 / Extractor149 / Hatchery131 | 58 | 94 / (692,544) | 189 / (317,544) | 401 /402 |
| Probe64 / Assimilator157 / Nexus154 | 67 | 103 / (708,544) | 207 / (324,544) | 399 /400 |

For stock1500, actual extraction leaves1492 and cargo8; deposit makes both current and cumulative gas8, removes the real cargo image, and clears carried amount/type. With stock0, the source remains0, cargo2 is created, and current/cumulative gas become2. These outputs agree with the separately recovered [8/2 extraction and deposit rules](native-harvesting.md#extraction-low-reserves-and-overlays). The depleted Assimilator's stock animation creates additional smoke Image438, whose genuine resource is supplied.

The Drone's deposit updates native Hatchery side words to `[0,0,25,0]` on this route. The Terran/Protoss depots' corresponding recorded words remain zero. This composes the earlier independent side-weight contract with the full gas return.

Without a depot, SCV/Drone/Probe emerge at callback103/94/103 at their existing side points(700,512)/(692,512)/(708,512). They carry8 gas, source1492, active byte0, and visible ResetHarvest151. Current and cumulative gas remain zero. Those cases stop at successful emergence; subsequent repeated depot search and eventual recovery after a depot is built are separate.

The observed callbacks depend on the declared positions, native initial state and traversal; they are not universal throughput constants. The37 timer write, same-callback decrement and actual hide/show/queue semantics are the transferable mechanisms.

## Reproduction and remaining dependencies

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_gas_harvesting_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-gas-harvesting-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_gas_harvesting_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-gas-assets \
  --asset-manifest docs/research/evidence/native-gas-harvest-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --execute-original-code \
  --out docs/research/evidence/native-gas-harvesting-probes.json
```

Blocked exit/fallback/retry, resource destruction or ownership changes while occupied, worker cancellation/death, dynamic depot loss/recovery, naturally moving multiworker throughput, mixed resource transitions, genuine Town allocation and complete campaign-world scheduling remain required. These cases establish complete selected gas-order/object paths; they do not close all harvesting or AI behavior in the [completion ledger](completion-ledger.md).
