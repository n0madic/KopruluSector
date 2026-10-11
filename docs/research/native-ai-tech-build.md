# Native paid AI prerequisite construction and Firebat production

Starting from only genuinely created Command Center106 and SCV7, the original Computer AI now constructs and completes **Supply Depot109, Barracks111 and Academy112**, pays for a Firebat32, completes normal training and attaches actual MilitaryAI. Original object callbacks choose the worker and advance every queue, order, movement, construction timer, payment, completed count and AI list. This closes the selected upstream building dependency of [paid roster fulfillment](native-ai-paid-roster.md).

Target is StarCraft **1.17.0.1**, preferred ImageBase `0x400000`, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. [Execution evidence](evidence/native-ai-tech-build-probes.json) contains **3 passing complete cases, 16,957 original invocations and 95,940,371 instructions**; maximum single call 6,257,136 instructions. There are no prefix cases, helper stubs, Windows imports, original process startup or full-game execution. Reaching a complete `0x445070` return is an isolated object-step contract, not accepted outer frames or a played campaign.

## Declared initial world and supported macro

Original `0x424900 → 0x424da0` creates, completes and shows SCV at `(512,512)` and Command Center at `(768,512)`. Controller Computer1, Terran race1 and owner3 are declared before creation. Initial raw and completed counts come from those calls: one SCV, one Command Center, zero Depot/Barracks/Academy/Firebat. Actual supply begins `[provided20, used2, cap400]`. Caps400 for each race and availability bytes for7/109/111/112/32 are explicit valid initial inputs; they do not stand in for prerequisite objects.

The paid profile has450 minerals and25 gas. Negative profiles have399/25, or450/25 with Academy availability0. There are no resource units or income in this composition. Pools contain16 CUnits,64 Sprites,256 Images,64 Thingys and32 Paths; the original Town, WorkerAI, BuildingAI, GuardAI and MilitaryAI initializers execute. Original `0x4a98e0` initializes type strengths and `0x425610` normalizes acquisition. x87 control word037f and MXCSR1f80 are declared emulator CPU state.

Geometry is a declared clear4096×4096 synthetic world with one valid region1, explored/walkable tile flags, original object spatial insertion and native movement/placement. Its region center is `(768,512)`, group1, no neighbors, no contour obstacles. Eight valid player AI-region arrays refer to the original shared Military pool. This is not an authentic campaign terrain/contour import. All selected builder placement and movement execute original code; host positions or path results never advance them.

The macro is a **declared assembly of supported stock opcodes**, not an unchanged original campaign script:

| Macro PC | Instruction | Purpose |
|---:|---|---|
|0| `start_campaign` | Enable campaign AI policy. |
|1| `start_town` | Allocate a genuine Town and bind existing eligible units. |
|2/3| `defaultbuild_off; default_min(0)` | Constrain optional default production. |
|5/10| `build(1,109,80); wait_build(1,109)` | Request Depot; wait for its Town completion. |
|14/19| `build(1,111,80); wait_build(1,111)` | Request Barracks; wait for completion. |
|23/28| `build(1,112,80); wait_build(1,112)` | Request Academy; wait for completion. |
|32/36| `train(1,32); wait_train(1,32)` | Request Firebat; distinguish raw allocation from completed training. |
|40| `stop` | Recycle the script after completion. |

[Static evidence](evidence/native-ai-tech-build-static.json) validates instruction boundaries against the pinned PyMS schema and both unchanged effective BINs. Depot/Barracks/Academy `build` bytes match actual AIScript instructions at `0x686`, `0x674` and `0x759` in TMCu; `train(1,32)` matches `0x792` there. Barracks and Academy waits match TMCx at `0x9713` and `0x932c`. Identical `wait_build(1,109)` and `wait_train(1,32)` operand instances do not occur in those effective BINs; their native handlers and valid operand formats are separately reviewed and actually execute here. Neither that absence nor combining stock instructions makes the entire macro authentic. The record preserves all matching counts and sample PCs, with effective AIScript/BWScript hashes and the schema SHA.

Native `0x494d90 → 0x493a60` starts this macro over rectangle `(400,400)-(900,800)`. Town startup attaches actual WorkerAI and BuildingAI to the two original objects. At its first wait, PC10/wait30, no spending queue has been authored or paid. Requests are original persistent four-byte records `[8,80,109,0]`, later joined by111/112; flag8 and priority80 are native insertion outputs.

## Natural worker selection and physical paid construction

SCV's stock idle order is **Guard2**, even with Computer1 established before creation. At object callback1 the original idle consumer selects **ComputerAI156**. The first native worker service occurs at callback1 through actual caller return address `0x48cdf3`. This batch makes **zero direct calls to `0x487580`, `0x483400`, `0x4865b0`, player order issuers or worker movement** after script startup. All those consuming calls originate within complete `0x445070` calls. This extends the earlier [selected-entry construction](native-ai-construction.md) contract to natural initial service.

Slower per-player spending and Town services retain their original8-step and64-step nested cadence. The declared counters `0x6c46d4`, `0x69b568`, `0x6663e4` initially10000 suppress selected creep, long-refresh and vision-expiry branches during the bounded run; `0x69b564=0` leaves ordinary spending active. Initial player cursors are2, and the regional cursor begins at player3/region1. Counters then change only by original code. See [frame cadence](native-frame.md).

| Produced object | Native PlaceBuilding accepted | Payment / genuine allocation | Normal completion | Native placement |
|---|---:|---:|---:|---|
|Depot109|179|257|859| `(752,672)` |
|Barracks111|902|979|2181| `(768,784)` |
|Academy112|2252|2304|3506| `(848,672)` |

The worker receives original PlaceBuilding30, then physically moves and enters ConstructingBuilding33. Original spending reserves AI available budget before payment; current stocks debit only when original paid creation occurs. Costs are100,150 and150 minerals. The building has real raw counts and BuildingAI while incomplete, but `wait_build` does not advance on raw allocation. All three reach actual normal completion, native completed counters, final HP and supply effects without Cwal, timer changes or host signals. The same WorkerAI survives the three constructions; each building's actual BuildingAI points to the genuine Town.

Original completion briefly selects order150, then ComputerAI156, and native callbacks later select the worker again. SCV's movement around construction and its resulting destination are original outputs. The observed callback numbers depend on these declared geometry, seed, cadence and initial states; they are not universal build-duration constants.

## Native Town counting and actual prerequisites

The new static block `0x4872f0..0x4874ec` has172 instructions. It walks the Town's WorkerAI head+0xc and BuildingAI head+0x14, takes the CUnit pointer from each AI record's+0x10, and counts matching unit types. **Mode1 skips units without status bit1** and does not count production queues. `wait_build` invokes this mode, preserves its PC on insufficient count and writes wait30. Original scheduler countdown means retries are31 object callbacks apart. Creation of an unfinished BuildingAI alone cannot satisfy it.

Mode0 can include matching unfinished objects and consult `0x41bde0` for queued types. The full static body also combines selected Zerg building aliases131..143 through its original switch. This composition executes the normal Terran mode1 paths; it does not claim new positive queue-counting or Zerg-alias coverage from that static inventory.

Original requirement recipes are separately preserved in the new static JSON:

| Requested type | Actual original words | Relevant dependency |
|---|---|---|
|Depot109| `ff02,7,ff0b` | SCV producer; reject its ConstructingBuilding33 state. |
|Barracks111| `ff02,7,ff0b,106` | Completed Command Center106. |
|Academy112| `ff02,7,ff0b,111` | Completed Barracks111. |
|Firebat32| `ff02,111,ff08,ff05,112` | Barracks producer, construction/status gates, completed Academy112. |

Depot is an ordered macro step and supply source; it is **not** the Barracks recipe's required completed building. Academy is ID112;114 is Starport. Native `0x4b32a0` entry/return hooks record actual producer types, completed Academy/Barracks counts, EAX and error without replacing helper results. This preserves the requirements interpreter and error ordering described in [requirements](native-requirements.md) and [paid roster fulfillment](native-ai-paid-roster.md).

## Firebat payment, completion and Military attachment

After Academy completion at3506, the next eligible script visit at3534 sets PlayerAI+0x220 to33 and waits at train PC32. Actual Barracks legality succeeds, original spending pays50/25 at3535, and genuine hidden Firebat allocation raises raw count to1 while completed count remains0. BuildingAI queue tag1 carries the actual region1 context; the producer enters secondary Train38, state2, with its true child pointer. Remaining child time begins360. Stock availability or supply caps do not replace the completed Academy requirement.

At3565, the macro sees raw count1 and reaches wait_train PC36, which still rejects completed count0. Normal completion at3896 places the child at `(720,832)`, performs Show and native completed-count/supply updates, clears the producer slot and advances its circular head. Actual `0x48d320 → 0x487990 → 0x4ae760 → 0x4a9b20` attaches genuine MilitaryAI and issues order157; the next callback changes its primary to ComputerAI156. Its assigned ID and record's region pointer both identify region1, whose genuine ground count is1.

At3906, the next original VM visit satisfies wait_train and executes stop. PC becomes41, the script active head becomes0, and the node returns to the actual free list. **Wait remains `ffffffff`**: the eligible scheduler decremented zero before dispatch, and stop does not reset payload. No host clears it. Final cash is0/0; real completed counts are one each SCV/CC/Depot/Barracks/Academy/Firebat; actual Terran supply is `[36,4,400]`. The original explicit-training word is0 and the spending heap is empty at this stop.

This closes natural paid prerequisite production and one completed Military member. The macro includes no attack roster/attack_prepare/attack_do. Authentic multi-region wave recruitment/dispatch remains [the separate frozen wave](native-ai-wave.md) and [paid missing-roster](native-ai-paid-roster.md) composition; this case does not silently claim a campaign wave or strategic arrival.

## Discriminating bounded failures

| Initial profile | Callback pairs | Completed Depot/Barracks/Academy/Firebat | Final stocks | Script PC/wait |
|---|---:|---|---|---|
| 450/25, availability1 |3906| 1/1/1/1 | 0/0 | 41/0xffffffff |
| 399/25, availability1 |2276| 1/1/0/0 | 149/25 | 28/17 |
| 450/25, Academy availability0 |2276| 1/1/0/0 | 200/25 | 28/17 |

Both negative cases execute the same natural paid Depot and Barracks pipeline and stop95 callbacks after Barracks completion. Academy remains absent in both raw and completed lists, PC28 remains blocked, no Firebat queue/allocation/payment occurs, and used supply remains2. Cash399 leaves149 minerals, one below the actual Academy cost; availability0 preserves200 minerals despite enough funds. These are separate resource and availability gates, not a combined generic failure.

The recorded Academy112 gates distinguish those causes directly. With cash399, actual SCV7 legality returns **1/error0 twice**, then resources/available budget prevent construction. With availability0, the same genuine SCV gate returns **0/error2 twice**. Both final heaps retain `[priority80, kind3, type112, Town]`, with demand `[150,0,0]`; available budgets are `[149,25,34]` and `[200,25,34]`. The original requests remain pending instead of becoming nonexistent or paid queues.

An additional complete original `0x4b32a0(ECX32, EDX3, stack=[genuine Barracks])` call after each bounded failure returns `ffffffff/error8`, with actual completed Barracks1/Academy0. That discriminates the downstream Firebat prerequisite using a valid actual producer; it issues no order or progress signal. Future cash acquisition or availability changes are outside these fixed-input windows.

## Evidence and reproduction

[Probe](../../tools/research/probe_native_ai_tech_build_117.py) SHA-256 `db7222fb45c7ceb0500ad7e16807728acc6a30800a7421ab049d5c50057e656e`; [inspector](../../tools/research/inspect_native_ai_tech_build_117.py) `c05198254096e94ddf983b25314e9d93b724b2164a3fd398f507ae4533203b3c`. All120 recursive local source hashes are checked before/after execution. Every256 RNG source counter and final state match the independently advanced original integer LCG. Runtime: x86_64 Rosetta, Python3.9.6, Unicorn2.1.4, pefile2024.8.26, Capstone5.0.6. Bounds are100,000 instructions ordinarily,8,000,000 per full object call,5,000 callback pairs for success and3,500 for each negative profile.

[Image-resource provenance](evidence/native-ai-tech-build-assets.json), SHA `908dc5c8839deaaa9d7c99e354cd12ef27db106f37c5b945dabe974b3f0fee5c`, covers280 Image IDs,241 distinct GRP paths and290 LO bindings, including genuine SCV working/spark overlays. Strings retain separate authenticated network665de0 and stat_txt6ab4f8 bindings from [the frozen string manifest](evidence/native-hallucination-string-assets.json). No retail resource payload is published.

The completed raw private report is557,447,121 bytes, SHA `64795b95f72571fe2cfcbd8640696a194c813762ea5297c1bcdf41f497952acb`. The unchanged [trace compactor](../../tools/research/compact_native_call_trace.py) generates the publication, preserving assertions, outputs, bounds, requirement returns, sources and native state while summarizing1,756,499 callee entries with canonical trace hashes. Compaction executes no original instructions.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_ai_tech_build_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --schema /private/tmp/koprulu-pyms/PyMS/FileFormats/AIBIN.py \
  --out docs/research/evidence/native-ai-tech-build-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_ai_tech_build_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest /private/tmp/koprulu-campaign-data/manifest.json \
  --assets /private/tmp/koprulu-ai-tech-build-assets \
  --asset-manifest docs/research/evidence/native-ai-tech-build-assets.json \
  --string-manifest docs/research/evidence/native-hallucination-string-assets.json \
  --pyms /private/tmp/koprulu-pyms --profiles all \
  --out /private/tmp/koprulu-ai-tech-build-raw-probes.json --execute-original-code

python3 tools/research/compact_native_call_trace.py \
  --input /private/tmp/koprulu-ai-tech-build-raw-probes.json \
  --out docs/research/evidence/native-ai-tech-build-probes.json
```

Each callback pair is full original `0x445070`, then the selected complete `0x4be410(ECX42)` timer path from [connected wave clock evidence](native-ai-wave.md). Declared elapsed100 advances only through that timer. The sole host gameplay progress write is the external logical frame counter52651c; an executable assertion rejects any other host `w()` call after progress starts. Stack/ABI setup remains the isolated-call boundary. Initial objects, stock-resource binding, geometry and auxiliary counters are explicit fixture inputs, not an original mission loader or accepted outer frame. Wider concurrency, arbitrary terrain/pools, other recipes/races, save/load and full campaign playback remain separate boundaries. Every earlier publication and dependency stays frozen.
