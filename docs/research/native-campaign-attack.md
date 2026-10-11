# Native 1.17 strategic state and campaign attack preparation

This batch recovers the complete selected region-state mutation and attack-preparation paths, including successful preparation through original region classification, border expansion, staging search and AIScript calls. Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`.

Evidence: [static witnesses](evidence/native-campaign-attack-static.json), [80 complete original-code cases](evidence/native-campaign-attack-probes.json), and [inspector/probe](../../tools/research/inspect_native_campaign_attack_117.py). **All80 cases pass without helper stubs.** Original native callees execute under an instruction allowlist in private Unicorn memory. No WinMain, game loop, Windows API or guest host-file/network operation executes. These results extend [regional combat-AI static recovery](native-combat-ai.md) and [AIScript wrapper recovery](native-ai-opcodes.md). Subsequent state8/9 launch, actual military transfer and original opcode continuation are recovered in [campaign waves](native-campaign-wave.md).

## Region-state mutation and initialization

`0x4ab900(ECX=region, EDX=new_state)` returns without changing anything when the region's u16 ID is0. Four complete no-op cases compare all52 bytes before and after.

When the **old state is0**, even a request to state0 runs initialization. It reads marker predicate `0x481bb0`: terrain record word0 equals`0x1ffd`. It reads default-minimum **byte** `PlayerAI+0x222` through `0x4837d0` and campaign flag`0x20` through `0x4831e0`. A campaign region without the marker receives default-minimum needed-ground strength and zero needed-air strength; a marked region receives the reverse. Without campaign flag both are0. Four marker/minimum cases distinguish ordinary/`0x1ffd` and byte0/255, using actual original helpers.

Old-state0 initialization clears region dwords+`0x10`,+`0x14`,+`0x18`,+`0x1c`,+`0x20`,+`0x24`,+`0x28`: cached local/all strength, enemy strength, targets, slowest pointer and detector. It normally clears destination u16+2; a requested state7 preserves that destination. It does **not** clear the MilitaryAI list head+`0x30`, pool pointer+`0x2c`, ground count+`0x0a`, or retry byte+7. Calls from a nonzero old state retain the initialized strength/target payload instead.

All nonzero-ID transitions clear flag`0x20`, write the state byte, and set priority as follows:

| New state | Priority and additional mutation |
|---:|---|
| 0 | 10;40 when region flag`0x40` is present and campaign flag is absent |
| 1,2 | Campaign70; otherwise160 |
| 3 | Clear flag1, priority10 |
| 4 | 40 |
| 5 | 50 |
| 6 | 110 if the current region has an owned worker/building; otherwise100 |
| 7 | Clear byte+9;110 if destination has an owned worker/building; otherwise apply the current-region110/100 test |
| 8,9 | Campaign50; otherwise160 |
| >9 | 10 |

The campaign priority expressions use byte arithmetic, including160+166→70 and160+146→50. Forty-four complete transition cases compare old0/4, new0..10, and campaign flag clear/set, observing state, flags, counter, destination and all seven payload dwords. Four additional state6/7 cases execute positive Worker-property spatial queries through actual `0x4861a0 → 0x4222d0 → 0x422330`, with the worker in the current or destination region. Query depth and scratch offset restore to0. The lower-level query contract is independently recovered in [AI removal](native-ai-removal.md).

These priorities are scheduling/defense values. They are not durations or proof of the actions subsequently dispatched for each state.

## Native attack_clear resets state8 only

Eight complete cases begin at original script scheduler `0x4938e0` with bytecode `[attack_clear,wait(1)]`. They execute the dispatcher, `0x483770`, the actual CRT memset implementation, and regional iterator `0x4ad230`.

The operation zeros all128 roster bytes, clears grouping-region-plus-one word `PlayerAI+0x21e`, and clears last-attack dword+`0x228`. Regional iteration visits indices**1 through count-1**. A record changes only when its state is8 **and** its ID is nonzero. It clears flag`0x20`, writes state0 and priority10 together; flag`0x40` without campaign flag raises priority to40.

State9 remains unchanged. Index0 remains unchanged even when its state is8. Other flag bits survive. Fixtures distinguish campaign flag and flags0/`0x20`/`0x40`/`0x60`; all finish at PC4/wait1. This is actual opcode execution with regional effects, not only a directly invoked reset wrapper.

## Attack-preparation gates and returned readiness

`0x4af300(ECX=player, EDX=center_x, stack=[center_y,arg2,arg3])` is called by both `attack_prepare` and `attack_do`, with the same player/center and trailing constants1,0. The direct planner probes additionally exercise1,1 as a declared fixture variant; the actual AIScript calls and their native traces use1,0.

Its first predicate `0x4833e0` returns whether PlayerAI byte+`0x22e` equals0. If the byte is nonzero, preparation immediately returns1. Otherwise read grouping word+`0x21e`. For a nonzero word, resolve **region index `word-1`** and return0 while its state is8, or1 for any other state. It does not rescan the world on this existing-group path.

With no grouping region, preparation builds a2500-byte classification array and sets player flag`0x100`. It then attempts staging selection. Eight complete cases distinguish disabled/enabled, no grouping, and grouping state8/9/0. The enabled empty-world case executes the entire original classification and selection chain:336,621 instructions, below the explicit500,000 bound. With both fallback constants1 and a non-air-only roster, failed selection sets byte+`0x22e=1` and returns1. A returned1 therefore does not mean that an attack was scheduled; disabled and exhausted-selection paths share that result.

## Region classifications and positive staging selection

Own-region classifier `0x4ab760` scans the player's actual unit list. It marks regions with eligible active buildings as class5, excluding DAT property`0x10`. If it found none, it scans eligible ordinary owned units instead. Enemy classifier `0x4ab6d0` scans the global active list. A candidate requires a weapon or Building property, hostility, and the relevant per-player detection bit when invisibility status`0x300` is present; accepted enemy regions become class1.

`0x4aab90` reads a snapshot and expands neighbors of regions with class>=5. A neighboring class1 becomes2; a neighboring class0 becomes6. `0x4aab20` promotes snapshot class2 to3 and changes its currently unclassified neighbors to4. Preparation repeats promotion/expansion while expansion reports a change. These classifications describe regions for this preparation; they are not the region state byte.

The roster scan determines whether its nonempty entries all have the native Flyer DAT property4, using stored `unit_ID+1`. Preparation calls `0x4b1710` with normal and alternate search flags, and can retry using air-neighbor traversal when its fallback argument allows it. These literal checks are retained; deleted roster marker229 and arbitrary malformed IDs are not reinterpreted as a valid unit recipe by this report.

The staging selector examines **2499 indices1..2499**. `0x4b1810` initially accepts only a classification3 index. It builds a bounded five-layer search using ground neighbors, or the all-neighbor accessor according to the passed flag. Expansion accepts classification6 neighbors whose AI states are0,4,5. Selection depth order comes from unchanged native data: **[4,3,5,6,2]**. Its candidate pass rejects region flag`0x20`, prefers qualifying regions with tile count>=60 by strictly smaller distance from the supplied center, and records a largest-tile fallback. Campaign mode can use that fallback when no distance candidate was found. General multi-candidate choice, alternate traversal and tie/fallback combinations still require their own applicable probes.

Two complete positive cases execute the full planner in campaign and noncampaign modes on a three-region triangle. An own building occupies region1, an enemy building region2, and region3 is neutral. The actual final classifications are **[0,5,3,6]**. The native search selects neutral **region3 as staging** and enemy **region2 as destination**. This direction is established by original writes; the input classification3 region is not itself the eventual staging region.

The planner writes grouping-plus-one4 and last-attack100, changes staging region3 to state8, then writes destination2. Staging priority is50 in campaign mode and160 otherwise. Player preparation flag`0x100` is set; requested roster `[38]` is retained. The function returns0, indicating the newly created grouping state is still waiting. These fixtures exercise one valid candidate, so they invoke no random multi-candidate choice or noncampaign ranking helper `0x4aa350`. No military transfer or unit production is substituted or implied: this stage records the wave's staging/destination state.

## Composed AIScript behavior

Six further complete scheduler calls compose the positive planner with its actual opcode consumers, in both campaign modes:

1. Actual `attack_prepare` invokes the positive original planner and continues into wait1, reaching PC4 with staging state8, grouping4 and destination2. It does not retry on the helper's returned0.
2. Actual `attack_do` sees existing staging state8, leaves its own PC at0 and writes retry wait**30**. The requested roster remains.
3. An **explicit external fixture** changes staging state to9 and clears the script wait. The next actual `attack_do` continues to `attack_clear` and wait1. Grouping/time/roster clear; state9 remains9.

The third step does not execute or replace `StepRegion`. It isolates the interpreter's readiness/continuation branch. The [campaign-wave batch](native-campaign-wave.md) separately executes actual state8/9 dispatch, roster/arrival or timeout readiness, destination-state1 mutation, military transfer and source-state0 mutation. Those real launch paths do not require the externally supplied state9 used by this interpreter discriminator.

The recovered contract explains a campaign sequence such as `attack_add → attack_prepare → wait → attack_do → attack_clear`: the prepare opcode can create a grouping region and proceed, while the later do opcode waits for that region to leave state8. The effective original campaign bytes and PCs are separately anchored in [campaign content](campaigns.md) and [AI research](ai.md).

## Reproduction and remaining dependencies

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_campaign_attack_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-campaign-attack-static.json \
  --execute-isolated-original-code \
  --probe-out docs/research/evidence/native-campaign-attack-probes.json
```

Default mode only disassembles the hash-gated EXE. Probes use Python3.9.6, Unicorn2.1.4, Capstone5.0.6 and pefile2024.8.26 under x86_64 Rosetta. CPU-feature fixture flags select the actual native scalar/REP memset body; optimized alternative CRT branches are not replaced or executed. Synthetic units, DAT properties, region graphs and script bytes are stated inputs. JSON records hashes, allowlisted ranges, complete returns, actual call arguments, instruction counts and matched memory effects.

Pinned [AISE AiRegion layout](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781) corroborates field names. Native effects and fixed bounds come from the local1.17 instructions, not modified AISE/Teippi policies. [Campaign waves](native-campaign-wave.md) closes selected state8/9 launch, cached-strength, dispersion and actual military-transfer paths. Remaining preparation work includes multiple-candidate ranking/random selection and transportation/air search alternatives; the companion report retains precise boundaries for positive target ranking and other strategic states. Completed preparation and selected launch paths do not establish full mission parity.
