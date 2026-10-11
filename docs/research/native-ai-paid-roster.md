# Original paid Firebat production, roster fulfillment and Guard replacement

The authentic Ter5 request for four Firebats now completes from three starting members through **native payment, hidden Train allocation, normal completion, Military assignment, physical staging and wave launch**. A second profile also completes a naturally generated Guard replacement after original queue cancellation/refund and later repayment. No queue, prerequisite count, produced-unit AI, timer or position is authored by the host to advance these chains.

Target: StarCraft **1.17.0.1**, preferred ImageBase `0x400000`, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. [Execution evidence](evidence/native-ai-paid-roster-probes.json) contains **4 passing cases, 3,458 complete original invocations and 27,367,336 instructions**. The maximum single invocation is592,660 instructions. Two cases reach production/assignment and wave continuation; two are complete95-callback rejection windows. There are no function-prefix stops, helper stubs, Windows imports or full-game execution.

## Original recipe and genuine prerequisites

The [new static record](evidence/native-ai-paid-roster-static.json) decodes the original Firebat32 requirement at word offset75 / VA `0x50db1e`:

```text
ff02 111       current producer must match Barracks111
ff08           producer must not be constructing another target
ff05           producer status0x2 must be set
112            require completed Academy112
ffff           terminate
```

The Academy ID is **112**;114 is Starport. This is unchanged executable data decoded with the previously reviewed [specialized requirements parser](native-requirements.md). The new inspector deliberately adds no duplicated function disassembly: execution reuses the frozen requirement, spending, Train, creation and AI bodies through120 pinned source dependencies.

Original `0x424900 → 0x424da0` creates/completes/shows own Command Center106, Barracks111, Supply Depot109, optional Academy112, three Firebats32, and an enemy Command Center. Original creation/completion writes the raw/completed counters and supply values. Original `start_town` scans the actual owner list and attaches genuine BuildingAI records to these buildings. The Barracks and Command Center share the actual allocated Town. Buildings are declared initial completed objects produced by original calls; their preceding paid SCV construction is outside this batch.

Player3 is Computer1/race1; enemy4 is controller2. The initial Firebat availability byte is explicitly1. Cash is50/25 for the roster-only success,100/50 for the Guard extension,50/25 with no Academy, or0/0 with all prerequisites. Each racial supply cap is400. Actual completed Command Center/Depot supply is36; the three starting Firebats use6. No count or supply-used field substitutes completed objects.

The finite pools, actual type-strength initialization, acquisition normalization, stock graphics/IScript and synthetic triangle terrain follow [the frozen connected wave harness](native-ai-wave.md). Script/Town startup is a declared synthetic fragment, followed by direct entry into the unchanged Ter5 BIN at `0x1c8e`. Original mission loading, its preceding build orders and trigger launch conditions are separate. After startup, the only host progress write is one increment of external logical frame counter `0x52651c` per callback pair.

## Paid request to completed Military member

Authentic bytes execute `attack_add(4,32), attack_prepare, wait(500), attack_do, attack_clear, wait(4500)`. Original preparation selects staging region3/state8/destination2 and records the four-member roster. Native periodic service recruits the three initial Guards into Military and emits the missing member's kind1/priority60/type32 request with staging-region context.

The actual Barracks service `0x48cd70 → 0x487110 → 0x487de0` consumes that request. Original legality, current-stock, supply, AI-budget and unit-limit gates run. `0x41b550` pays50 minerals/25 gas and writes queue slot0 as32, BuildingAI tag1 and context staging3. Request demand/heap removal and secondary Train38 issuance are original effects.

In callback1, actual `0x439310 → 0x41b060 → 0x41bc30 → 0x424900` allocates a genuine hidden Firebat at the Barracks center `(128,320)`. Producer secondary state+e2 becomes2 and current-child pointer+ec identifies that child. Its initial remaining time is360. Raw Firebat count becomes4, completed count stays3, and used supply becomes8. Queue acceptance/payment and actual unit allocation are distinct stages.

Normal object callbacks decrement the native construction timer and update HP. There is no Cwal, timer rewrite or manual completion signal. At callback362 the real placement/completion/Show chain completes the child at `(80,368)`, increments completed Firebat count to4 and invokes `0x48d320 → 0x487990 → 0x4ae760 → 0x4a9b20`. Tag1 supplies the original staging context: the new child obtains MilitaryAI assigned to3 and receives native movement. The producer clears slot0 metadata, writes228 and advances its circular head to1; it does not compact the queue.

The new member physically moves toward staging. At callback481 the original regional readiness/arrival checks permit `0x4b15d0` to dispatch all four to region2/state1 and reset staging3 to0. The authentic script resumes at501, clearing grouping/time/roster and reaching `0x1c9b/wait4500` without retry. This closes the paid missing-member dependency of the earlier no-cash wave.

| Profile | Complete callback pairs | Produced/completed Firebats | Wave launch | Final script PC/wait | Complete invocations / instructions |
|---|---:|---|---:|---|---|
| Cash50/25, Academy present |501| one Military member, native total4 |481| `1c9b/4500` |1,032 /8,084,284|
| Cash100/50, Academy present |979| one Military member plus one Guard, native total5 |481| `1c9b/4022` |1,988 /15,481,576|
| Cash50/25, no Academy |95| none, total3 |none observed| `1c96/405` |218 /1,851,209|
| Cash0/0, Academy present |95| none, total3 |none observed| `1c96/405` |220 /1,950,267|

These are full `0x445070` callbacks followed by the complete selected `0x4be410(ECX42)` clock path, not accepted outer frames. Final elapsed values are132,162,106 and106. In the first success, the two newest/lagging members still physically occupy region3 at the501 stop, while all four Military records are assigned to2. Assignment/launch is not universal physical arrival. Native Bullet impact counts are99 and339 in the two success windows.

## Natural paid Guard cancellation, refund and later fulfillment

The100/50 profile starts with exactly enough cash for two Firebats. The first kind1 request pays50/25 at callback1. Parentless old Guards naturally generate kind2/priority50 requests. At98, the Barracks pays the remaining50/25 for a replacement behind the still-training first child. Its queue is `[32,32,228,228,228]`, with tags `[1,2,0,0,0]` and contexts `[staging3,oldGuard,0,0,0]`. The second queued item has no allocated child yet; raw count and used supply remain4 and8.

At129 the original per-player Town service `0x4865b0` calls `0x41b810` for relative slots1,2,3,4, as previously isolated in [request satisfaction/cancellation](native-production.md). Actual relative-slot1 cancellation removes the paid Guard item and refunds its full50/25 cost. Stocks become50/25, slot1 and its metadata become empty, and the first child's timer continues from232. The first completion remains362. This is observed original cleanup, not an assumption about its intended policy.

After the four-member wave has launched and the script continues waiting, native periodic Guard production later pays/requeues the same Guard context at618. The Barracks now has head1, queue `[228,32,228,228,228]`, tag2/context oldGuard at slot1, stocks0/0 and an actual newly allocated hidden child with time360. Normal completion occurs979.

This completion follows `0x48d320 → 0x487990 → 0x4b25b0` and attaches the second genuine child to the existing Guard record `0x6b6948`. Its native parent becomes `0x5daf70`, type32, AI kind1. Active and reserved home coordinates both equal `(224,144)`. Military count in destination2 remains4; the replacement is a Guard, not an extra member silently appended to the launched wave. The producer clears slot1/tag/context and advances its head to2. Supply is `[provided36, used10, cap400]`, completed Firebat count5, and net current stock is0/0.

The Guard receives its actual completed-unit AI at this stop. Subsequent home arrival or future recruitment of that Guard is outside the case. No host restores the refunded cash, reissues training, attaches the Guard or changes its home.

## Requirements, error ordering and failed profiles

Read-only hooks record each original requirement-gate entry and actual return/error. With a completed Academy, a Command Center trying the Firebat recipe returns `ffffffff/error25` for wrong producer; the original consumer retains the request. Barracks111 subsequently returns exactly1/error0, admitting payment. Academy/Depot are also wrong producers and produce25 when visited.

Without an Academy, the first Command Center attempt instead returns **`ffffffff/error8`**, with actual completed Academy count0 and Barracks count1. Missing prerequisite fails before terminal producer-mismatch reporting. The original consumer removes that failed request and recursively services the now-empty heap; **no Barracks requirement evaluation occurs in this95-callback window**. The regional service later regenerates demand, but no production/payment occurs before the stop. A remake must preserve this native error and request-lifetime ordering rather than simply try every producer with one boolean legality result.

In the no-cash profile, the Barracks recipe actually returns1/error0 eleven times. Resource/AI-budget admission still rejects: no queue, child, debit or launch appears. At95, demand is `[200,100,8]`, available budget `[0,0,30]`, and the heap contains the missing Military request plus three Guard replacement requests. No-Academy retains cash50/25; no-cash retains0/0. These windows do not prove the future campaign can never acquire an Academy or money.

## Evidence and reproduction

[Probe](../../tools/research/probe_native_ai_paid_roster_117.py) SHA-256: `736cc6b20d8b5cdce2acc84566e70d96d608986c50bd6aae4a4ae288bf0522f8`. [Inspector](../../tools/research/inspect_native_ai_paid_roster_117.py): `8a3950951e97d390a37041faa026d6c3d99306b506adc13b8ea066fae28109bc`. Source hashes are checked before/after execution; all256 RNG source counters and final LCG state are independently validated. The x86_64 Rosetta runtime is Python3.9.6, Unicorn2.1.4, pefile2024.8.26 and Capstone5.0.6. Bounds are100,000 instructions ordinarily,500,000 for preparation,8,000,000 per object step, and1,600 callback pairs per profile.

[Private-resource manifest](evidence/native-ai-paid-roster-assets.json), SHA-256 `0295db46bb429a0b7ed47bb9070fd12e0fdd9897239c2201a504a89a7c8d0e2f`, covers258 Image IDs,226 distinct GRP paths and276 LO bindings. Network/stat_txt bindings use the unchanged [authentic string manifest](evidence/native-hallucination-string-assets.json). No retail payload is published.

The completed private raw report is196,452,973 bytes, SHA-256 `75906826361dd4de5f61aa2de1952b14d4821f89fe4f6350b5b48b93d3d16e86`. The frozen [compactor](../../tools/research/compact_native_call_trace.py) generates the published18,011,101-byte derivative, summarizing665,760 callee-entry records with counts, sampled arguments and canonical full-trace hashes. Actual outputs, assertions, native requirement returns and cancellation inputs are preserved. The compactor executes no original instructions.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_ai_paid_roster_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-ai-paid-roster-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_ai_paid_roster_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest /private/tmp/koprulu-campaign-data/manifest.json \
  --assets /private/tmp/koprulu-ai-paid-roster-assets \
  --asset-manifest docs/research/evidence/native-ai-paid-roster-assets.json \
  --string-manifest docs/research/evidence/native-hallucination-string-assets.json \
  --pyms /private/tmp/koprulu-pyms --profiles all \
  --out /private/tmp/koprulu-ai-paid-roster-raw-probes.json --execute-original-code

python3 tools/research/compact_native_call_trace.py \
  --input /private/tmp/koprulu-ai-paid-roster-raw-probes.json \
  --out docs/research/evidence/native-ai-paid-roster-probes.json
```

The selected paid production and both AI assignment classes are now connected. Remaining scope includes upstream paid Barracks/Academy construction, authentic mission startup/terrain/contours, other recipes/races, concurrent producers/waves, later Guard movement, pool/resource changes, save/load and outer-frame policy. A full game, WinMain, Windows API or guest host-file/network call does not execute. All earlier frozen publications remain unchanged.
