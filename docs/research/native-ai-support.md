# Native 1.17 observer and transport support requests

**101 complete original-code cases pass**, recovering the periodic observer/transport request tail and selected positive consumers from generated heap entries through queue admission, payment, metadata and completed-child AI assignment. Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`.

Evidence: [five static witnesses, 200 instructions and original support-unit recipes](evidence/native-ai-support-static.json), [101 complete cases](evidence/native-ai-support-probes.json), and [inspector/probe](../../tools/research/inspect_native_ai_support_117.py). The longest individual call, including recorded preceding calls in composed cases, executes4533 instructions. Actual request generation, heap insertion/pop, original lazy requirement initialization/evaluation, costs, supply/budget checks, queue admission, military allocation and primary orders execute without callable helper replacements. Private fixtures provide explicit units, counters, DAT costs/groups, availability, sufficient completed prerequisites, region state and pools. No complete game, WinMain, Windows API or guest host-file/network operation executes.

## Periodic support policy

The tail of `0x4af930(ECX=player)` runs after the regional strength/roster pass. Throughout the ascending region scan it remembers the **last state4 or5 region**. Later transport requests use that region as their context. It does not choose the nearest base or a town pointer at this point.

PlayerAI flag8, read by `0x483350`, suppresses the entire support tail. It does not skip the preceding regional loop. The tail then reads the player's race byte `0x6726d1+player*36`:

| Race byte | Native selected policy |
|---:|---|
| 0 | No support-tail observer/transport requests |
| 1 | Require at least one **completed Control Tower115**; use Dropship11 as transport |
| 2 | Request missing Observers84 up to2 using all-unit count; use Shuttle69 as transport |
| Other | Return from this tail |

Twelve full sweeps vary campaign flag, race0/1/2 and suppression. Four completed/all Control Tower cases establish that an unfinished/all-count-only Control Tower does not pass the race1 gate. This is the tail's local policy; race0's other transport behavior is not thereby absent from the game.

The native all-unit accessor `0x4143c0(CX=ID,DL=player)` reads `0x529604[(ID*12)+player]`. Completed accessor `0x4143e0` reads the corresponding `0x52c0c4` table. These counts differ from queued words and the helper's own demand totals. This batch supplies declared counter fixtures rather than executing every counter producer.

## Observers are independent of a defense-region context

For race2, if all-unit Observer84 count is below2, the tail invokes actual `0x482930(player,ID84,[priority80])` once per missing unit. This inserts **kind8, priority80, ID84, context0** and adds native DAT cost to AI demand. It does so **before** checking whether a state4/5 transport context was remembered.

Thirty-two complete sweeps compare counts0/1/2/3, campaign mode and sole-region states0/4/5/6 with enough Shuttles to suppress transport requests. Counts0/1 make two/one Observer requests even with no state4/5 context; counts≥2 make none. There is no separate completed-Observer requirement at this branch. The original producer's later prerequisite checks are separate.

Two further whole-tail compositions combine missing Observers and Shuttles, observing independent kind8/context0 and kind1/regional entries. Suppression flag8 prevents both. Fixture Observer cost25minerals/75gas/2supply and Shuttle cost100/10/4 make two of each raise demand250/170/12; these numbers are explicit synthetic DAT inputs, not assertions about retail balance values.

## Transport quotas, priority and regional context

After the race gate, a null remembered state4/5 region stops transport work. Otherwise the tail reads **all-unit** count of Dropship11 or Shuttle69. Desired minimum is2 with campaign flag`0x20`,5 otherwise. Every missing transport becomes an actual `0x482990(player,remembered_region,[ID,priority,1,1])` request: **kind1 with regional context**.

Priority is80 when the count before this request loop is0,50 otherwise. It is chosen once for the loop, so several requests from a count0 fixture all have priority80. Twenty-eight cases distinguish counts0/1/2/4/5/6/`ffffffff` in both race/mode combinations. A count already at or above the minimum emits nothing.

Four full sweeps supply region states`[5,4,5]` at indices1/2/3. All resulting transport requests have context **region3**, the last qualifying record. Seven sole-region cases in states0/1/2/3/6/7/9 provide no transport context. Those tests keep strength requirements0 so they do not fabricate unrelated recipe production; the preceding [defense recipe consumer](native-defense-recipes.md) is a separate recovered composition.

## Generated requests enter the actual producer

The positive producer fixtures call actual building service `0x487110`, which dispatches kind8 to Observer consumer`0x488030` and kind1 to ordinary training`0x487de0`. Stocks are below the opportunistic default-production threshold, so no new default request obscures the generated top entries.

The unit-requirement offset array is initialized to **lazy sentinel`ffff`**, not replaced with a success recipe. Actual `0x4b3230` decodes the unchanged packed table in this EXE, then the native evaluator consumes the original Dropship11/Shuttle69/Observer84 recipes. Their raw tokens are retained in this batch's static JSON. Fixtures declare available units, sufficient completed prerequisites, producer fields and connected Control Tower115 for Starport114. The completed-prerequisite counters are permissive controlled state; this is not an authentic campaign snapshot or proof that every prerequisite becomes available at that mission point. [Original requirements](native-requirements.md) defines the lazy table and evaluator.

Three single-request compositions use matching producers Starport114 or Robotics Facility155. They execute original race/requirements/resource/supply/budget/limit gates, circular enqueue, stock debit, successful demand/budget removal, heap pop and secondary Train38. The resulting distinction is:

| Generated request | Queue metadata after actual consumer |
|---|---|
| Observer kind8 / context0 | **tag5 / context0** |
| Transport kind1 / AI-region context | **tag1 / same AI-region context** |

Thus spending kind8 is not copied unchanged into training metadata. Successful admission also does not immediately create a child or increase the supplied all-unit counter.

Two multi-request compositions begin with a count0 race2 tail. Campaign mode generates two Observers and two Shuttles; all four actual admissions yield queue`[84,69,69,84,228]` under the real equal-priority heap behavior, tags`[5,1,1,5,0]`, and contexts`[0,region1,region1,0,0]`. Noncampaign mode generates two Observers and five Shuttles; the first five admissions yield`[84,69,69,69,69]`, leaving one Shuttle and one Observer pending. These are observed original queue/heap orders, not an imposed stable ordering by request kind. Payment, remaining demand, available AI budget and pending records are all checked separately.

Four generated-request stock-failure compositions distinguish policies: Observer retains the request in both campaign modes; ordinary Shuttle retains it in campaign mode, while noncampaign shortage removes it and performs immediate recursive building-service retry. The original outer return remains0. These reuse the exact error/retry contracts in [spending dispatch](native-spending-dispatch.md) and [training admission](native-training.md).

A sixth service after the noncampaign five admissions demonstrates gate order. Relative queue position4 contains Shuttle69, so original unit-production gate`0x4b32a0` returns0/error3 **before the resource helper is called**. The queue, two pending requests, stocks75/385, demand125/85/6 and available budget575/885/982 remain unchanged under the declared costs. There is one dispatcher visit and zero calls to resource helper`0x4826d0`. Calling this merely an unaffordable-unit failure would incorrectly predict request removal/retry. The gate's exact threshold is queue word≤105 at relative position4, not an abstract queue-length test.

## Completed-child assignment boundary

Two compositions retain generated/admitted metadata and then invoke actual `0x48d320(producer,constructed_completed_child)`. They deliberately construct a completed child in private memory; **new-unit allocation, Train progress and visibility are not executed in these cases**.

For Shuttle69, training tag1/context=region1 reaches real `0x487990 → 0x4ae760 → 0x4a9b20`, allocates MilitaryAI type4 in that region and issues ordinary157 with three RNG calls. For Observer84, ordinary assignment helper`0x487990` has an explicit early exclusion for ID84; the child remains without UnitAI and on its initial order1, with no RNG. This is that assignment entry's behavior, not a claim that Observers have no separately scheduled detector/movement policy. Automatic Observer cloak/Show is separately covered in [unit creation](native-unit-creation.md); its whole later behavior is not imported here.

## Reproduction and remaining compositions

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_ai_support_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --out docs/research/evidence/native-ai-support-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-ai-support-probes.json
```

Without the explicit execution flag the tool only validates/disassembles and decodes original recipe data. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4, x86_64 Rosetta. Orders.dat SHA-256: `0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9`. Probe script SHA-256: `36f98ef9824e3c8c95c5977bc92662e753045387001cc44d652afddc6740ab4f`. JSON records current dependency hashes, exact reviewed ranges, complete-return traces including preceding generator/admission calls, original arguments/returns and matched state. Each entry is bounded by100000 instructions.

This closes the selected original support-policy-to-admission/assignment chains. It does not establish automatic completed/raw count producers, authentic prerequisite reachability, a whole mission scheduler, new-unit creation in these compositions, transport loading/drop/exploration policies, full Observer control, or cache-refresh cadence. Those remain separate native lifecycle/AI consumers; no modified source implementation or successful queue return closes them implicitly.
