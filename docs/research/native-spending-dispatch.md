# Native 1.17 building AI and spending dispatch

Building AI entry `0x487110`, its nine-kind spending dispatch, recursive retries and selected actual consumer paths are recovered from the reviewed StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses are preferred VAs at ImageBase `0x400000`. See [static dispatcher/table/callees](evidence/native-spending-dispatch-static.json), [46 isolated original-code checks](evidence/native-spending-dispatch-probes.json), and the [inspector/probe](../../tools/research/inspect_native_spending_dispatch_117.py).

All **46 checks passed without helper stubs**: 44 complete functions/actual-callee fixture paths and two explicit Nuclear Silo queue prefixes before primary-order issue. Original instructions execute in private Unicorn memory under an instruction allowlist. No WinMain, full game, Windows API or host file/network operation executes. Complete dispatch-table coverage does not imply every branch of every consumer is recovered.

## Entry and special unit services

`0x487110(ECX=unit)` is the building ComputerAI service as well as the target of production retry calls. Before its AI-type gate it services these current unit types:

| Unit type | Actual call |
|---|---|
| 42 Overlord | `0x486bf0(unit)` position/movement service. |
| 107 Comsat Station | `0x48d3a0(unit, reactive=0)` spell dispatcher. |
| 108 Nuclear Silo | `0x485390(unit)` nuclear queue service. |

Names correlate with [pinned Teippi unit constants](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/constants/unit.h#L115); the native type comparisons are independent local evidence.

It then loads unit+0x134. Null AI or AI type byte+8 other than3 returns1 without spending dispatch. Thus the special calls can occur even when the subsequent AI record is military type4. Three native cases observe that ordering using actual early-return paths: Overlord position mismatch, Comsat spell cooldown, and Nuclear Missile limit255. These fixtures do not emulate their unselected movement/spell/order branches.

The Comsat gate accessor `0x4833c0` reads byte `0x67d295 + player*0x4e8`, which is **PlayerAi+0x22d** for native base `0x67d068`. Nonzero rejects nonreactive spell dispatch. A fixture using source-relative label+0x255 did not reach the early return; the corrected+0x22d fixture does. The absolute native address and relative offset must agree. Broader [combat spell AI](native-combat-ai.md) remains its own contract.

## Opportunistic generation before the top request

For AI type3, the entry reads owner byte+0x4c. If signed current minerals>1500 **and** signed gas>1500, and predicate `0x407b10(unit)` reports not busy, it calls default request generator `0x4849c0(player, producer, priority160)`. The return is converted to a boolean local flag later passed as the ordinary training consumer's second stack argument, controlling its retry policy.

Predicate `0x407b10` checks queue head empty228 and both construction-target predicates false. It does not independently reject a current research byte. Native cases distinguish stocks1500/1501, signed-negative mineral stock, an occupied Marine queue and a research byte. An occupied queue suppresses this opportunistic generator but does not prevent the original Marine recipe from accepting a second queued Marine: that recipe uses `ff08` construction-target checks, not the `ff07` full busy predicate. Retaining each actual recipe matters more than one generic producer-busy rule.

The generator's early rejection paths are independently executed: incomplete status, primary order0x6a/0x96, secondary order other than0x17, minerals<600, gas<300, and default-production-disable flag0x4. Those checks are described below. A subsequent [default-production batch](native-default-production.md) covers the successful chooser and selected connected queue path.

## Native dispatch table and return behavior

After the optional generation attempt, top-kind getter `0x483550` returns byte PlayerAi+0x19 for a nonempty heap, or0 for empty. The dispatcher checks kind<=8 and jumps through nine pointers at `0x487270`:

| Kind | Table target | Actual consumer / effect | Dispatcher return |
|---:|---|---|---|
| 0 | `0x487232` | Default generation `0x4849c0` with priority50; if nonzero, call ordinary training with retry disabled. | 1 when generation fails; otherwise training result. |
| 1 | `0x487258` | Ordinary training `0x487de0`. | Training result. |
| 2 | `0x487258` | Same consumer, distinct request metadata. | Training result. |
| 3 | `0x487210` | Town building-morph consumer `0x488100`. | Consumer result. |
| 4 | `0x4871ba` | Town worker-training consumer `0x487d30`. | Always1 afterward. |
| 5 | `0x4871d0` | Upgrade consumer `0x485d30`. | Always1 afterward. |
| 6 | `0x4871e5` | Research consumer `0x485ca0`. | Always1 afterward. |
| 7 | `0x4871fa` | Addon consumer `0x485b70`. | Always1 afterward. |
| 8 | `0x487221` | Observer consumer `0x488030`. | Consumer result. |

Kinds>8 return1 with no consumer. The first ordinary-training stack dword is the building AI pointer; its reviewed body does not use it. The second is the local retry flag. A kind0 call uses that AI pointer and literal1. Types and values passed to consumers are distinct from the producer's primary/secondary order IDs.

Native complete paths cover all nine kinds: ordinary kind1/2 Marine success, kind4 SCV success, kind8 Observer success, kind5/6 stock failure, kind3/7 town mismatch, and empty/kind0 default-generation rejection. An empty high-resource fixture invokes generation twice, priority160 then50; with an occupied queue it invokes only50. These are actual calls through the table and reviewed callees, not a simulated dispatch response.

Consequently **return1 is not a production-success signal**. Research/upgrade stock failure and addon town mismatch retain the request yet the outer dispatcher returns1. Conversely, an ordinary first-request failure can return0 even when its recursive retry successfully trains the next request. The broader ComputerAI caller must preserve these return distinctions; [combat AI](native-combat-ai.md) identifies its building service call.

## Recursive retry and spending effects

Selected errors in [ordinary training](native-training.md) and [research/upgrade consumers](native-research-upgrade.md) remove the failed top request and invoke this same building entry. The next visit re-reads the now-current heap top and repeats the optional generation and dispatch. This is immediate recursion, not a timer-only instruction.

A complete native two-request case places unavailable Marine0 first and valid Ghost1 second. The first consumer removes Marine demand without deducting current stocks or available AI budget. Recursive dispatch then enqueues Ghost, deducts75/25 current stocks, removes the remaining demand and deducts75/25/2 from available budget. Both heap entries are gone and the queue contains Ghost; the original outer ordinary consumer and dispatcher still return0. The evidence records two `0x487110` visits and actual removal/enqueue calls.

With no second request, the recursive visit sees kind0 and default generation rejects secondary order0; it returns normally with an empty heap. Campaign unit-resource failure retains the request and performs one visit; noncampaign failure removes it and performs a second visit. Native cases record count, demand and call depth separately. These results establish bounded successful/failed recursion paths, not safety for arbitrary cyclic or malformed AI state.

## Observer consumer

`0x488030(player, producer, stack=[building_AI])` acts only on requested ID84. For other IDs it returns0. It checks race matching, specialized unit requirements exactly1, current resources/supply/AI available budget and unit limit. Unit-limit failure removes the request through the failure helper and returns0. Resource failure retains it regardless of the campaign flag, differing from the ordinary training consumer's noncampaign policy.

On acceptance it enqueues through `0x41b550` with training tag **5** and context **0**, issues secondary Train0x26, successfully removes the request and returns1. This is neither the top-kind8 tag nor the ordinary request's context. Native success observes those metadata fields and payment/account changes. Unavailable-unit failure removes and recursively retries; wrong producer error25 retains; separate native failures cover resource shortage and unit limit255.

This selected consumer does not require the top context to match the building town. Town worker/morph/addon consumers have their own context checks; imposing that check on Observer would change behavior.

## Nuclear Silo service and requirement return value

`0x485390` first checks Nuclear Missile14's unit limit, race match, unit requirements, resources/supply/AI budget, and queue insertion. It passes tag8/context0, then requests primary order0x7c and secondary Train0x26. It does not pop the spending heap in this service.

Its requirements branch uses **nonzero**, rather than requiring exactly1. Two native prefixes observe the original gate return before the branch: Silo108 returns1 and queues Missile14; a deliberately wrong Barracks111 stand-alone helper input returns`ffffffff`/error25 and still queues/pays for Missile14. Both prefixes stop before primary-order issue and retain the spending entry. The normal dispatcher calls this helper only for Silo108; the wrong-type case tests the helper boundary and does not assert ordinary campaign behavior.

A common boolean conversion must therefore not replace the distinct equality/nonzero tests at their native callers. The [specialized requirements report](native-requirements.md) explains the gate's three-valued return behavior.

## Default generation and unclosed consumer branches

Default generator `0x4849c0` rejects producer-incomplete status or primary order0x6a/0x96 via `0x4b14d0`, then requires secondary order0x17. It tries explicit training request accessor `0x482be0`: nonzero word PlayerAi+0x220 is returned as value-1 and cleared. Without an explicit value, signed stocks must meet600 mineral/300 gas and flag accessor `0x483750` must report default production enabled (PlayerAi flag0x4 clear).

After these gates, code selects alternatives by producer type and uses unit limits, capability helper `0x4878d0`, region accessor `0x4b0960` and request insertion `0x482990`. An explicit training value additionally enters needed-guard query/update helpers `0x4b25f0`/`0x4b2750`. The seven early-return probes in this batch do not establish those effects; the subsequent [default-production report](native-default-production.md) recovers all seven producer lists, the actual carried-argument ABI, main/morph-parent insertion and these guard predicates. Guard-list lifecycle and producer progression remain separate.

Town building-morph consumer `0x488100` has special Overlord42/Larva35 and morph IDs132/133/146/144/137; successful paths call primary order helpers before request removal. Addon consumer `0x485b70` checks town, requirements and resources, then calls placement `0x496240`, with campaign fallback search `0x4962c0`. Success removes the request before issuing order0x24 through `0x42a340`, calls `0x41b5c0`, and stores the requested addon ID at unit+0xc4. `0x41b5c0` is CancelAllQueued: it cancels every occupied queue slot from the last to the first, cancelling a current child through `0x41a340` or refunding a non-building entry through `0x408460`, then leaves all five slots 228 and the head 0. It does not merely initialize an empty queue. Corrected after the function atlas ([P29 `0x485b70`](atlas/p29.md), [P06 `0x41b5c0`](atlas/p06.md)). This batch executes their town-mismatch returns only. Subsequent reports cover [placement predicates](native-placement.md), [AI candidate search](native-ai-placement.md) and [primary orders](native-primary-orders.md); the complete successful consumer chain and subsequent production/morph execution still require connected validation.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_spending_dispatch_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-spending-dispatch-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-spending-dispatch-probes.json
```

Default inspection is static and hash-gated. Explicit execution records target/script/dependency hashes, runtime versions/architecture, exact VA allowlist, instruction counts, call trace, default-generation priorities, requirements returns and complete/prefix boundaries. All46 cases matched. This closes the dispatch/control contract and declared consumer paths; it does not establish the complete building AI, production state machine or campaign parity.
