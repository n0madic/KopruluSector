# Connected original Ter5 attack waves, recruitment and game clock

Authentic **Ter5** attack fragments now compose with original periodic Guard recruitment, Military allocation, physical movement, combat, regional dispatch and subsequent AIScript continuation. All three profiles pass: four Firebats, four Marines plus two Firebats, and three Firebats for a four-member request. The incomplete group remains in state8 until the original campaign grouping timeout, even while its existing members kill the enemy base.

Target: StarCraft **1.17.0.1**, preferred ImageBase `0x400000`, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`. Addresses below belong to that exact image. [Static evidence](evidence/native-ai-wave-static.json) adds **18 reviewed witnesses / 552 instructions**. [Execution evidence](evidence/native-ai-wave-probes.json) records **3 complete compositions, 7,864 complete original-function invocations and 47,822,370 executed instructions**, with no prefixes or helper stubs. The largest single invocation executes 236,610 instructions. These are selected callback compositions, not accepted outer frames or a campaign playthrough.

## Genuine producers and declared environment

Retail DAT, both AI BINs, IScript, GRP/LO and text tables are loaded unchanged after hash validation. Native `0x424900 → 0x424da0` creates/completes/shows two Command Centers106 and the chosen Firebat32/Marine0 roster. Native type-strength initializer `0x4a98e0` processes all228 stock unit types before creation; native caches are Firebat `[0,186]` and Marine `[100,100]`. Fresh Unicorn instances explicitly set x87 control word `0x037f` and MXCSR `0x1f80`; no substitute square-root result is supplied.

Original `0x4b1fe0` allocates/attaches Guards. Original script allocation and VM `start_campaign/start_town` create a Town and attach the owned Command Center, reusing the frozen [Town/construction composition](native-ai-construction.md). No authored CUnit, GuardAi, MilitaryAi, BuildingAi or Town record replaces these producers. Original `0x425610` normalizes acquisition ranges before stock creation. Original Bullet, Town, Guard and Military pool initializers execute.

Declared caller inputs are player3 Computer1/race1, enemy player4/controller2, self-alliance, mineral/gas stocks0/0, and each racial supply cap400. The explored/walkable synthetic4096×4096 terrain has triangle graph `1:[2,3], 2:[1,3], 3:[1,2]`, with centers `(128,128)`, `(768,128)`, `(512,128)`. Its tile plane maps x<320 to1, 320≤x<640 to3, and x≥640 to2. There are no authentic campaign contours. Private finite free pools contain16 units,64 sprites,256 images,64 Thingys and32 Path records, with the original initialized1000-record Guard/Military shared pools. Eight valid AI regional arrays initially carry IDs, owners and shared-pool pointers; native Town creation and preparation produce the tested regional states.

Synthetic startup bytecode executes `start_campaign, start_town, defaultbuild_off, default_min(0), stop`. The authentic fragment then starts directly inside the complete original BIN. Earlier Ter5 build instructions, trigger launch conditions and original mission setup are outside this composition. After startup, the host increments only the external logical frame counter `0x52651c` once per callback pair. It does not advance positions, issue orders, attach AI, change signals, rewrite waits/timers/regions or fast-forward the game clock.

## Authentic bytecode and measured outcomes

Effective AIScript SHA-256 is `635433e546998e05529a9d6acc806c06b528c7ce5075a206f6f2c8de44ffed6e`. Its table begins at `0xe00e`; Ter5 record `0xe01e` contains ID `Ter5`, original entry `0x1c1a`, string1054 and flags3. The [campaign inventory](evidence/ai-inventory.json) establishes the original script identity separately from these deliberately selected middle-of-script entries.

| Fragment | Original PCs and operands | Final native PC/wait |
|---|---|---|
| Firebat request | `1c8e attack_add(4,32); 1c92 attack_prepare; 1c93 wait(500); 1c96 attack_do; 1c97 attack_clear; 1c98 wait(4500)` | `0x1c9b / 4500` |
| Mixed request | `1c9b attack_add(4,0); 1c9f attack_add(2,32); 1ca3 attack_prepare; 1ca4 wait(500); 1ca7 attack_do; 1ca8 attack_clear; 1ca9 wait(3000)` | `0x1cac / 3000` |

The first fragment's bytes through its next wait are `0c0420000d02f4010e0b029411`; the second's are `0c0400000c0220000d02f4010e0b02b80b`. These are unchanged original operands, not regenerated equivalent scripts.

| Genuine starting roster | Native launch callback | AIScript continuation callback | Elapsed at continuation | Actual attack_do retries | Original returns / instructions | Enemy HP, raw1/256 units |
|---|---:|---:|---:|---:|---|---|
| Four Firebats | 61 | 501 | 132 | 0 | 1,029 / 7,747,592 | 384000→301312 |
| Four Marines + two Firebats | 61 | 501 | 132 | 0 | 1,035 / 9,446,726 | 384000→207488 |
| Three Firebats for four requested | 2882 | 2888 | 281 | 77 | 5,800 / 30,628,052 | 384000→0 |

“Callback” counts complete `0x445070` object-step invocations, each followed by the complete selected clock service described below. It is not a wall-clock duration or an accepted-frame count. The profiles execute204,199 and897 native Bullet impact entries respectively; impacts include native hit/miss/effect handling and do not imply one equal damage quantum per entry.

## Periodic recruitment, launch and physical combat

Preparation chooses staging region3/state8/destination2 and grouping word4. Roster words hold ID+1:33 for Firebat and1 for Marine. Each complete object step traverses script, spending, regional and other AI services before unit/Bullet phases. Declared initial slow-service selectors admit player3 on the first visit; actual counters rotate thereafter without resets.

The first periodic `0x483400 → 0x4af930 → 0x4ab040 → 0x4ae960 → 0x4ae760` converts the living Guards to genuine Military records and issues HarassMove158. Old Guards become parentless with death count0. This continuously consumes the previously isolated [recruitment/transfer](native-guard-transfer.md), [arrival](native-military-arrival.md) and [ordinary spending](native-automatic-requests.md) contracts.

Original path records, movement and regional scans establish readiness for the complete rosters. At callback61, `0x4ac870 → 0x4b15d0` transfers all members to destination region2/state1 and sets staging region3 to0. Grouping, last-attack100 and roster remain until script clearing. At callback501 the authentic500 wait has expired, `attack_do` sees the departed staging state, and `attack_clear` zeros grouping/time/roster before the next authentic wait. No script retry is needed in these two profiles.

Native acquisition issues stock AttackUnit10 through `0x4202c0`, then actual unit orders, IScript, Bullet movement and effects damage the enemy Command Center. Firebat targeting begins at callback79 and all four have AttackUnit by97. Mixed targeting begins70 and all six have AttackUnit by115. Waiting for a wave and executing already-issued military orders are separate mechanisms.

**Assignment and physical arrival remain distinct.** At callback501 all four Firebats physically occupy terrain region2. The mixed profile's four Marines remain physically in3 while its two Firebats occupy2; all six Military records are nevertheless assigned to2 and all six are attacking. Dispatch does not assert that every member has crossed the destination boundary.

## Incomplete roster, budgets and genuine timeout

With only three Firebats, the matcher leaves one type32 request missing. Native spending inserts `[priority60, kind1, unit32, stagingRegion]`. Parentless old Guards independently generate kind2/priority50 replacement requests, despite all three original units remaining alive as Military. At callback95 the heap contains the kind1 request plus three kind2 requests. Aggregate mineral/gas/supply demand is `[200,100,8]`; available values are `[0,0,14]`. These are distinct context-bearing native requests, not one generic production counter. Stocks remain insufficient to buy replacements; missing-member training is outside this test.

All three existing members chase and shoot while assigned to state8; hostility does not satisfy the missing fourth slot. The authentic `attack_do` retries77 times at callbacks `501+31k`, `k=0..76`, preserving PC `0x1c96` and setting wait30 on each actual VM visit. Thirty countdown services plus the subsequent eligible visit explain the31-callback interval. A countdown merely passing through30 is excluded from retry classification.

The enemy base genuinely dies around callback2370. Surviving members enter ComputerAI156, consume actual `0x48cd70 → 0x4b1010`, and physically return toward the still-assigned staging center using native movement. By2441 all three have ComputerAI156 in region3. The missing priority60 request persists, with demand `[50,25,2]`; no host retarget or return-home order is supplied.

The original campaign clock threshold is unsigned `elapsed-last_attack >=180`, as discriminated in [campaign-wave contracts](native-campaign-wave.md). Natural clock progress and periodic regional scheduling produce launch at2882: destination2 becomes state1 with three members, source3 becomes0, and members receive158. The next actual script retry visit2888 completes `attack_do/attack_clear`, reaching `0x1c9b/wait4500` at elapsed281. Their physical positions still lie in3 at that stop; the test proves timeout dispatch and script continuation, not subsequent destination arrival.

## Complete original elapsed producer under declared guards

After each whole `0x445070` return, the runner calls complete `0x4be410(ECX=42)` in the original relative order. Initial elapsed `0x5349d8` is100. `0x47cb60` reads zero global `0x67517c`, permitting progress. Word counters `0x6ca750/0x6ca744` start0. An old-zero `0x6ca744` increments elapsed and reloads15; fifteen services decrement it, then another increments. N selected services therefore give `100+1+floor((N-1)/16)`, independently asserted for all profiles.

Countdown `0x5349d4` stays0. Declared objective selector BYTE `0x5349ec=0` makes actual `0x4ba910` clear eight objective pairs and take its unsigned selector-minus17 default return. Actual `0x4a79a0 → 0x4c2200` locates private widget ID-10; its flags+18 have bit8 clear, allowing original `0x4c22f0` to return without an indirect UI callback. This extends the earlier [trigger Wait-loop prefix](native-triggers.md) to a complete selected elapsed-producing return. It does not certify other objective modes, nonzero-countdown formatting, authentic UI state, complete trigger scheduling or the outer frame's acceptance gates.

## New consumed contracts and static-only alternatives

The static witnesses and visitation lists distinguish executed paths from unopened alternatives:

- `0x4ae840/0x4ae870` require nonnull type4 AI, matching parent and no DAT building bit1, then read assigned region air/ground targets at+1c/+20. Both execute in the genuine Military targeting composition.
- `0x4202c0` uses original attack-order DAT and real primary issuance. Firebat/Marine paths execute; Siege Tank-specific `0x421060/0x4317a0` transition and positive subunit/Interceptor alternatives remain unexercised here.
- `0x421320` target-region chase checks execute. Point variant `0x421370` and movement-state15 retry `0x42bfd0` are reviewed static witnesses only in this batch.
- `0x42e9b0` selects original IScript animation/header PC and writes native animation/PC/wait globals during real movement; no host animation VM replaces it.
- `0x4b1010` checks genuine Military identity, attempts `0x4ab630` for Marine/Firebat garrisoning, executes native reaction/acquisition, and uses region centers plus `0x4a9b20` radius14 for return movement. Its complete no-Bunker path executes after the enemy dies. Positive Bunker capacity/admission is outside this profile.
- `0x4acdf0` takes the real elapsed<360 early return; later strategic-update policies remain outside the measured interval.

## Provenance and reproduction

[Runner](../../tools/research/probe_native_ai_wave_117.py) SHA-256: `df19a6dd24d68754734ae16216a34788dd462fe524112fbae485669c1938ae8e`. [Inspector](../../tools/research/inspect_native_ai_wave_117.py): `ca96eef5693e74b558e980dbcb388c1f56a4891c85cd7170d0705245110c978b`. Execution evidence pins118 recursive source dependencies and checks their start/end hashes. All256 RNG source counters and the final LCG state are independently validated. Runtime is Python3.9.6, Unicorn2.1.4, pefile2024.8.26, Capstone5.0.6, x86_64 Darwin under Rosetta with a32-bit guest. Default invocation bound is100,000, preparation500,000, object callbacks8,000,000; each profile has a4,000-callback limit.

[Asset manifest](evidence/native-ai-wave-assets.json), SHA-256 `f2784f3bf0eacae82a3d6bbb594afed45c845e9c06592f6dcc4f241faa14d212`, records247 Image IDs,215 distinct GRP paths and260 selected LO bindings with archive provenance. Authentic [string manifest](evidence/native-hallucination-string-assets.json) binds network.tbl to `0x665de0` and stat_txt separately to `0x6ab4f8`. Payloads remain private. The raw334,177,782-byte completed report remains private with SHA-256 `751d7380cb986344da2b1f721e29374ddfdd5967b3cfadf4f82ddc6ada5252bd`.

Published execution JSON is a generated derivative. The frozen [trace compactor](../../tools/research/compact_native_call_trace.py) summarizes1,141,142 callee-entry records into counts, first/last argument samples and canonical full-trace hashes. It preserves case inputs, assertions, state, impacts, bounds and source hashes; it executes no original code. Samples are not the full argument trace.

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_ai_wave_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-ai-wave-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_ai_wave_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest /private/tmp/koprulu-campaign-data/manifest.json \
  --assets /private/tmp/koprulu-ai-wave-assets \
  --asset-manifest docs/research/evidence/native-ai-wave-assets.json \
  --string-manifest docs/research/evidence/native-hallucination-string-assets.json \
  --pyms /private/tmp/koprulu-pyms --profiles all \
  --out /private/tmp/koprulu-ai-wave-raw-probes.json --execute-original-code

python3 tools/research/compact_native_call_trace.py \
  --input /private/tmp/koprulu-ai-wave-raw-probes.json \
  --out docs/research/evidence/native-ai-wave-probes.json
```

The remaining dependencies are original mission/trigger startup, authentic terrain and contours, paid completion of missing troop requests, simultaneous waves, other races and strategic states, positive garrisons/transports, save/load and outer-frame policy. No full game, WinMain, Windows import or guest host-file/network operation executes. Earlier publications remain unchanged.
