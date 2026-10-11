# Native 1.17 triggers, clocks, briefings and campaign progression

This investigation uses `StarCraft.exe` **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. Every address below is a preferred VA; subtract `0x400000` for its RVA. [Static evidence](evidence/native-triggers-static.json) also records instruction bytes and file offsets. These addresses must not be transferred to 1.16.1.

The investigation now includes **152 successful isolated executions of original instructions** in Unicorn 2.1.4, in addition to static disassembly. They use synthetic private memory and an instruction allowlist, with **no stubs and no DLL imports**. Neither the complete game nor a campaign was executed. The [execution evidence](evidence/native-trigger-leaf-probes.json) states these boundaries explicitly.

Structural anchors came from pinned [Samase_scarf `map.rs::run_triggers`](https://github.com/neivv/samase_scarf/blob/f1681dbac8c7d1ac184dc84918ab7dd5771061ed/src/map.rs#L152) and [`campaign.rs::campaigns`](https://github.com/neivv/samase_scarf/blob/f1681dbac8c7d1ac184dc84918ab7dd5771061ed/src/campaign.rs#L23). They identify table dispatch inside an RNG-enabled caller and the Zerg/Terran campaign signatures. [Teippi `ProgressTriggers`](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/triggers.cpp#L40) supplies a useful older-version comparison, but its modified hardcoded 42-ms step is not the inspected 1.17 caller. Native addresses and the following behavior were recovered from the exact local executable.

## Dispatch and state

| Native entry or table | VA | RVA | Finding |
|---|---|---|---|
| Trigger scheduler | `0x4be500` | `0xbe500` | Waits/time, cycle counter, player traversal, victory application |
| Condition evaluator | `0x4bded0` | `0xbded0` | Up to 16 conditions, short-circuit AND |
| Condition table | `0x50e058` | `0x10e058` | 24 pointers, IDs 0–23 |
| Action runner | `0x4bdf10` | `0xbdf10` | Up to 64 actions, continuation index |
| Action table | `0x50df68` | `0x10df68` | 60 pointers, IDs 0–59 |
| Player list runner | `0x4bdfd0` | `0xbdfd0` | Ordered linked list, disabled/running flags |
| Owner expansion | `0x4bdb00` | `0xbdb00` | Copies raw TRIG records to player lists |
| Active-player iterator | `0x4b3160` / `0x4b3170` | `0xb3160` / `0xb3170` | Mode-dependent order and player eligibility |
| Briefing runner | `0x44fee0` | `0x4fee0` | Separate MBRF interpreter |
| Briefing action table | `0x503908` | `0x103908` | 10 pointers, IDs 0–9 |

Names are projected from pinned [BWAPI `Triggers.h`](https://github.com/bwapi/bwapi/blob/d727fed68558c506163048ea889131d8cbb33915/bwapi/BWAPI/Source/BW/Triggers.h). The evidence lists every pointer, including repeated shared handlers, and bounded handler disassembly. A pointer inventory does not establish every helper's semantics. The normal evaluator and action runner do not show a bounds check before indexing their tables; a new importer should validate IDs while preserving valid retail behavior.

A native trigger node has eight bytes for linked-list pointers, followed by the 2,400-byte CHK record. Important native offsets are condition type `+0x17`, action array `+0x148`, execution flags `+0x948`, and action continuation byte `+0x967`. Raw CHK offsets are eight bytes lower. In particular, the raw tail contains **27 owner bytes plus the continuation byte**, not 28 independent owners. The earlier archive manifest retains 28 tail bytes in its `owners` field; consumers must interpret its last byte as the continuation field.

| Global | VA | Representation |
|---|---|---|
| Current player | `0x6ca754` | u32 |
| Current trigger node | `0x6ca790` | pointer |
| Cycle countdown | `0x6ca748` | u16 |
| Player wait active | `0x6ca760` | eight bytes |
| Player wait remaining | `0x6ca768` | eight u32 values |
| Pending victory states | `0x6ca758` | eight bytes |
| Trigger pause | `0x6ca740` | byte |
| Frame-progress gate | `0x67517c` | u32, read by `0x47cb60` |
| Player trigger lists | `0x50e0d8` | eight 12-byte list headers |
| Elapsed game seconds | `0x5349d8` | u32 |
| Countdown game seconds | `0x5349d4` | u32 |
| Countdown paused | `0x53632c` | u32 |
| Switches | `0x534f20` | eight u32 bitsets, 256 switches |

## Conditions, continuation and preservation

The evaluator at `0x4bded0` skips a condition when its entry flags contain `0x02`. An enabled condition with type zero terminates the list successfully; it does not continue to subsequent nonzero slots. Otherwise the native table is called and a zero result immediately rejects the trigger. The stride is 20 bytes and the limit is 16 conditions. Always (22) returns one at `0x4b8e70`; Never (23) and MissionBriefing (13) return zero at `0x4b8e80` in the normal TRIG table. Native probes cover early termination, disabled Never, and the sixteenth condition boundary.

The list runner sets the current-trigger pointer, skips execution flag `0x08`, and evaluates conditions only when flag `0x01` is clear. Once actions have begun, subsequent visits resume the saved action index **without rechecking conditions**. Other triggers in the same list continue to be visited when an action suspends.

The action runner first sets execution flag `0x01`. Entry flag `0x02` skips an action before dispatch; enabled type zero terminates all remaining actions. A handler returning zero leaves the action index unchanged and suspends this trigger. A nonzero return advances the index. At completion, a preserved trigger clears flags `0x51` and resets its index to zero. An unpreserved trigger sets disabled flag `0x08`; the original observed flags become `0x09` after an ordinary completed trigger.

Preserve action 3 at `0x4b5fc0` calls the real setter `0x4bd5d0`, which ORs execution flag `0x04` into the current trigger. Original execution probes verify preservation, completion, disabled-action skipping and a Wait followed by Preserve. Flag `0x20` additionally invokes UnpauseGame through action-table entry 6 when the action sequence finishes; the UI side effects of that handler are outside the isolated checks.

## Waits and the two clocks

Wait action 4 at `0x4b5fd0` uses the **player's** timer, not a timer owned independently by each trigger:

1. An already-active player wait returns zero without changing the action's wait flag.
2. Once the player timer is inactive, action entry flag `0x01` is cleared and the action returns one, completing an earlier wait.
3. Without that entry flag, trigger execution flag `0x10` skips the UI wait and returns one.
4. Otherwise the action's u32 time value at `+0x0c` is written to the player timer, the player active byte and entry flag `0x01` are set, and the action returns zero.

The original instructions confirm that **Wait(0) still suspends**. The timer loop at `0x4be410` clears an active wait when `remaining <= delta`, sets remaining to zero and forces the cycle counter to one. Equality at 42 is covered by the native probe. `0xffffffff` is explicitly excluded from subtraction and acts as an indefinite wait. The probe stops at `0x4be463`, before unrelated time/UI helper calls.

Two triggers waiting for the same player interfere. A native probe executes their real runners in list order: the first arms a wait, the second suspends before setting its own entry flag; after expiry, the first completes and the second starts a new wait. This validates the mechanism behind wait blocks. It is not a trace of the complete outer scheduler.

The caller at `0x444f5b` enables RNG, loads `delta` from `0x503360 + speed_index*4`, calls the scheduler at `0x444f71`, then disables RNG. The table contains `[167, 111, 83, 67, 56, 48, 42, 501]`. The first seven values are the ordinary speed-index range observed by the surrounding speed code; this report does not assign a normal gameplay speed to the additional value 501. Wait durations therefore use a speed-dependent millisecond decrement.

Elapsed/countdown time follows a different rule. The u16 divider at `0x6ca744` is post-decremented. When its **old** value is zero, `0x4be4b6` increments elapsed game seconds and the divider is reloaded with 15. A positive countdown is decremented unless its paused global is set. Initializer `0x4be240` resets elapsed/countdown, clears waits and initializes both time/UI dividers to 15; it initializes the trigger cycle counter to one. This yields one elapsed game second per **16 eligible scheduler calls**. Countdown reaching zero also forces a trigger check by writing cycle one.

The trigger cycle at `0x4be529` is likewise post-decremented: `mov cx,ax; add ax,0xffff; ... test cx,cx; jne`. An old-zero value reloads 30 and runs the player lists, giving **31 eligible calls between ordinary checks**. The first ordinary check occurs on the second eligible call after initialization. Thus the ordinary interval is `31/16 = 1.9375` game seconds. Nominally it is 2,077 ms with delta 67 and 1,302 ms with delta 42. These are values derived from static control flow, not measured wall-clock intervals; waits, forced checks and pause gates change the schedule.

If the frame-progress gate is nonzero and trigger pause is clear, the scheduler exits. When trigger pause allows the scheduler to continue, the wait loop still runs but the later elapsed/countdown divider update exits at `0x47cb60`. A Defold implementation must keep these clocks and gates separate. A single general-purpose `timer` value cannot reproduce them.

Native probes also cover unsigned countdown/elapsed comparisons (0: at least; 1: at most; 10: exactly), equality at 299, invalid comparisons and `0xffffffff`. SetCountdownTimer at `0x4b6720` applies Set/Add/Subtract using 32-bit arithmetic, then clamps the signed result to `[0, 359999]`. Switch Set/Clear/Toggle and query probes cover indices 0, 31, 32 and 255; index 256 is ignored by SetSwitch. Random switch uses the actual RNG helper with source ID 57 and tests bit `0x80` of the result's low byte. RNG-disabled and seeded cases execute that original helper without substituting a fixture.

## Owners and traversal order

The CHK loader calls owner expansion in raw record order, advancing by `0x960` bytes after each record (`0x468e4a` onward). Owner expansion copies each matching record into a 2,408-byte node and appends it to the corresponding native list. Owners 0–11 mean direct player slots, owner 17 expands AllPlayers, and owners 18–21 expand Forces 1–4. Only eligible active players below eight receive executable lists. The other owner-byte values take the no-copy path. This is an owner-expansion rule; action/condition player selectors can have additional meanings such as CurrentPlayer.

The ordinary iterator checks players 0–7 in ascending order, accepting player types 1, 2, 3, 4 or 7 before applying `0x4bd590`. That helper excludes certain completed player states using `0x5349e0`. If both globals `0x672664` and `0x67294d` are nonzero, the iterator instead traverses four entries from `0x5264ac`, with the same eligibility filtering. The exact mode labels and population of that four-entry table remain dependencies; assuming numeric player order in every mode would exceed the evidence.

Expanded ownership can create multiple independent copies of one raw record. No deduplication appears in the owner expansion loop. Save state consequently needs per-copy flags, continuation index and mutable action flags, alongside player wait arrays and list order. Condition/action side effects are visible to later records and later players in the same traversal.

## Briefings are a separate interpreter

The native CHK section table maps MBRF to loader `0x468e80`, which validates divisibility by 2,400 and calls `0x4607e0` for each record. That owner loader selects the local player, AllPlayers or a matching local-player force and appends copies to a separate list at `0x5082f8`.

UI list runner `0x460ea0` calls the briefing interpreter with the node's CHK payload, advances only after a nonzero result, and stops after the last record. Interpreter `0x44fee0` requires the first condition type to equal 13; it does not use the normal condition dispatcher. It then executes its own action table at `0x503908`, with the same action-entry ignore bit and continuation byte pattern. Completion clears the running bit and resets the continuation index.

The ten briefing IDs are None, Wait, PlayWAV, DisplayTextMessage, SetMissionObjectives, ShowPortrait, HidePortrait, TalkingPortrait, Transmission and EnableSkipTutorial. The 64 catalog MBRF records all contain condition 13. Their nonzero action counts are: Wait 178; PlayWAV 4; DisplayTextMessage 68; SetMissionObjectives 64; ShowPortrait 189; HidePortrait 191; TalkingPortrait 12; Transmission 414; EnableSkipTutorial 1. These are raw counts, not runtime reachability.

Briefing Wait uses global byte `0x667bac`, independently of the eight TRIG wait timers. Its duration goes to `0x4611a0`, which schedules callback `0x4611c0` through UI helper `0x4c5960`; the callback calls `0x44ff60` to clear the wait byte. Briefing Transmission at `0x44fe30` derives a duration, launches WAV/text/portrait helpers and uses that same timer mechanism. Native normal Transmission at `0x4b60e0` instead uses the player TRIG timer, setting an indefinite duration in the single-player branch (`0xffffffff`, chosen by `cmovne` on multiplayer byte `0x672664` at `0x4b6175`; multiplayer uses the computed duration).

**Single-player Transmission completion (resolved by the function atlas, [P40 `0x4b60e0`](atlas/p40.md), [P36 `0x4a7320`](atlas/p36.md)).** For the local player the action plays the WAV, flashes and pings the unit found at the location, and calls `0x4a7320`, which arms UI timer 4 on the portrait control with the computed duration. When that timer fires, callback `0x4a7300` deletes it, clears the portrait-active flag `0x6ab3d0` and, in single player only, jumps to `0x4be3a0`: the local player's wait byte `0x6ca760[p]` and remaining time `0x6ca768[p]` become 0 and a trigger check is forced (`0x6ca748` = 1). On the next visit the action sees its entry flag `0x01`, clears it and returns 1. Escape ends it early: `0x4bda80` (single player and mission key `0x526524` < 32 only) marks every executing trigger of the local player that lacks execution flag `0x40` as skipped (flag `0x10`), clears the local wait, forces a trigger check and stops streamed sounds (`0x49db50`), the trigger portrait (`0x4a73e0`) and trigger text (`0x46aef0`). Action 58 (Disable Debug Mode) sets flag `0x40` on its trigger and so makes its Transmission and Wait actions unskippable; action 59 clears it. The dispatch, wrappers and timer separation are established statically; the UI callback scheduler and audiovisual helpers were not executed in these probes.

## Actual campaign ordering and hidden variants

The six pointer table entries at `0x50e0b8` are base Zerg/Terran/Protoss followed by expansion Zerg/Terran/Protoss. Each row is eight bytes: an unresolved u16 label ID, u16 mapdata index, u16 cinematic ID, u8 race and u8 hidden flag. A zero mapdata index terminates a table. Cinematic rows may reuse the following mission index or use a boundary index; they must not be treated as map loads.

| Campaign table | VA | Rows selectable by number | Hidden map resources | Cinematic rows |
|---|---|---:|---:|---:|
| Terran | `0x4f0680` | 11, including tutorial | 0 | 4 |
| Zerg | `0x4f0700` | 10 | 0 | 4 |
| Protoss | `0x4f0778` | 10 | 0 | 4 |
| Expansion Protoss | `0x4f08d8` | 8 | 0 | 1 |
| Expansion Terran | `0x4f07f0` | 8 | 1 | 1 |
| Expansion Zerg | `0x4f0848` | 10 | 6 | 1 |

Number/suffix selector `0x4bbb60` counts only non-cinematic rows with hidden byte zero. Thus there are **57 selectable map rows including the tutorial**, plus seven hidden catalog resources. This explains the 64 catalog CHK entries, while four additional prerelease maps bring the physical inventory to 68. These counts describe different structures and do not define a 64-mission play order. The native tables establish the base Terran path through `terran01`–`06`, `08`, `09`, `11`, `12`; omitted directory suffixes `07` and `10` are outside this path.

Campaign progression at `0x4bb790` ordinarily advances to the next eight-byte row. A pending SetNextScenario string at `0x526526` changes that selection through resolver `0x4bba20`. Normal action 41 at `0x4b5900` resolves its CHK string and copies it to this field. The resolver distinguishes `.SCM` from expansion filenames, matches a basename against the map catalog, and finds a non-cinematic row. Exact Storm ordinal string-operation behavior remains a dependency. Pure native map lookup `0x4bbae0` was executed for 34 discriminating inputs, including hidden rows, bonus, index zero and invalid/boundary indices.

The 46 raw SetNextScenario actions provide explicit candidates:

| Source | Candidate transitions |
|---|---|
| Expansion Terran 04 | `Terran05a.scx` or `Terran05b.scx` |
| Expansion Terran 05a / 05b | `Terran06.scx`, plus retry/self targets |
| Expansion Zerg 03 | `Zerg04a.scx` through `Zerg04f.scx`, plus self targets |
| Expansion Zerg 04a–04f | `Zerg05.scx`, plus self targets |
| Expansion Zerg 09 | `Bonus.scx` or `Zerg10.scx`, plus self targets |
| Expansion bonus | `Zerg10.scx`, plus self target |

The six Zerg 03 branches require CurrentPlayer to accumulate at least 10,000 minerals and command exactly 0–5 units of ID 130, respectively. The pinned [PyMS unit catalog](https://github.com/poiuyqwert/PyMS/blob/bfc5d3aad0b5614a5aff72c223f8efa00afddfa4/PyMS/Data/Units.txt#L131) names ID 130 Infested Command Center. These raw predicates explain why all six Zerg 04 resources matter. Native counting/filter semantics behind Command and Accumulate still require their helper probes before calling a branch behavior fully validated.

The local Zerg 09 bonus gate is also concrete. Trigger 2 sets countdown time to 1,800. Trigger 18 sets switch index 8 once switch index 0 is set and countdown is at most 299; an additional ElapsedTime(1500) condition has ignore flag `0x02`. Trigger 19 selects `Bonus.scx` when switch 4 is set and switch 8 is clear; trigger 20 selects `Zerg10.scx` when both are set. Therefore the data describes a cutoff tied to the 30-minute countdown and its 299-second threshold, approximately after 25 game minutes, subject to trigger cadence and event order. It is not evidence for a hardcoded 20-minute wall-clock rule. [Static evidence](evidence/native-triggers-static.json) includes the gate's raw numeric predicates and writes, plus map hashes.

Hidden flags do not imply unused resources. Terran 05b, Zerg 04b–04f and bonus are referenced by transition actions even though numeric selection skips them. The default sequential rows pass through hidden variants; the maps' explicit next-scenario actions are therefore part of campaign glue, not optional metadata. Label strings, exact campaign end-state UI meanings, profile persistence and invalid override fallbacks still need separate validation.

## Reproduction and remaining dependencies

The scripts read original artifacts only from user-selected paths. Default native-probe mode prints a reviewable plan; `--execute-original-code` is required to execute the isolated functions. The successful run used the x86_64 Rosetta Python environment because the available arm64 Unicorn runtime previously failed during startup. The executable itself is never launched as a host process.

```sh
python3 tools/research/inspect_native_triggers_117.py \
  --exe /absolute/scratch/app/StarCraft.exe \
  --data /absolute/scratch/campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --out docs/research/evidence/native-triggers-static.json

arch -x86_64 /absolute/path/to/x86-python tools/research/probe_native_triggers_117.py \
  --exe /absolute/scratch/app/StarCraft.exe \
  --execute-original-code \
  --out docs/research/evidence/native-trigger-leaf-probes.json
```

Dependencies: pefile 2024.8.26, Capstone 5.0.x for static disassembly, and Unicorn 2.1.4 for execution. The static probe verifies every supplied CHK against its manifest hash. Both probes reject an unknown executable hash. [General reproduction](reproduction.md) describes archive extraction and pinned sources; no extracted CHK, MPQ or retail executable belongs in the repository.

The next semantic work is explicit: original unit/group/location selection and caching behind Command/Bring, accumulated resources, Kill/Deaths/Score and most/least conditions; original side-effect helpers for creation, removal, ownership, orders, locations and properties; the briefing callback scheduler, portrait/WAV/text behavior and skip/reset handling; mode-specific player order; staged victory/allied-victory and campaign/profile/end-state behavior. The complete dispatch and data inventories define this workload, while the 152 leaf checks close specific continuation, wait, switch, time-comparison and campaign-lookup questions. They do not prove six-campaign gameplay compatibility.
