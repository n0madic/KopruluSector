# Native 1.17 campaign wave launch and military transfer

This batch executes the selected original paths from a prepared state8/9 region through roster/arrival checks or a grouping timeout, actual military-record transfer, native movement-order issuance, and the subsequent AIScript `attack_do → attack_clear`. Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`.

Evidence: [25 static witnesses, 1469 instructions](evidence/native-campaign-wave-static.json), [127 complete original-code cases](evidence/native-campaign-wave-probes.json), and [inspector/probe](../../tools/research/inspect_native_campaign_wave_117.py). **All127 cases pass with no helper stubs.** The guest executes reviewed original instructions and actual callees, including the real AI allocator and primary-order pool/list/activation code. Fixtures supply explicit objects, pools, cached strengths, terrain, spatial indexes and DAT fields; they do not replace callable helpers. No WinMain, complete game loop, Windows import, guest host-file or network operation executes.

This extends [attack preparation](native-campaign-attack.md), [military arrival and exact random destinations](native-military-arrival.md), [military/group behavior](native-military-ai.md), and [primary orders](native-primary-orders.md). It establishes a real launch path beyond the preparation report's manually changed staging-state fixture. It does not execute physical movement or a whole campaign mission.

## Grouping timer and readiness

`0x483940(ECX=player)` reads last-attack dword `PlayerAI+0x228`. Zero returns0 immediately. Otherwise it compares the **unsigned** difference `elapsed_game_time - last_attack` against **180 in campaign mode, 120 otherwise**. Equality counts as expired. Expiration clears the last-attack dword and returns1; a non-expired timer preserves it and returns0. Sixteen cases distinguish zero, equality, one before the limit, wraparound, and a numerically smaller current time. An elapsed value below the stored value can therefore expire the timer through unsigned subtraction; this is native arithmetic, not a time-sanitizing policy.

The elapsed counter is `0x5349d8`. Its independent producer is recovered in [native triggers](native-triggers.md): one elapsed game-time unit per16 eligible scheduler calls. The120/180 comparison uses this game clock, separately from AIScript wait30 and speed-dependent trigger waits. No wall-clock duration is measured here.

`0x4aa500(ECX=region, EDX=output, stack=[exclude_incomplete])` copies nonzero attack-roster words as `unit_ID+1 → unit_ID`, then scans the actual player unit list. In the exercised argument1 path, only eligible completed units with MilitaryAI assigned to this region satisfy a request. It normalizes Tank30 to5. Each match replaces the **first** matching output entry with228; consuming the current last entry also shrinks the returned count. It does not compact interior holes. Duplicate requests need duplicate matching units. Existing dead-unit roster marker229 becomes output sentinel228 and therefore does not hold the later `<228` readiness test. Nine cases distinguish duplicates, interior/trailing matches, another region, incomplete units, Tank normalization, and marker229. The separate argument0 egg/unfinished-production paths are present in static witnesses but are not exercised by these cases.

`0x4ab150(ECX=player)` supplies the arrival gate. It scans active player units with MilitaryAI in state8/9. A member blocks arrival when its actual terrain-region ID differs from its assigned region, it has no queued primary order, and it has no actively hostile target. A queued order or a target that the real hostility helper recognizes as hostile excludes that member from this blocking test. Four direct cases execute the original hostility and position helpers. Thus readiness is more specific than every roster member standing in the staging region.

## Actual state8/9 launch

`0x4b0ce0(ECX=player, EDX=region_index)` resolves the AI region, clears flag4, and decrements a nonzero retry byte+7. Its unchanged ten-entry dispatch table sends **both state8 and state9** to `0x4ac870`. The dispatcher table is recorded as data, not disassembled as instructions.

The group handler first runs actual strength and target updates. When its special-terrain/route branch is not selected and flag`0x20` does not disperse the group, it checks the grouping timer. Expiration bypasses the roster and arrival checks. Otherwise it returns while a requested output ID<228 remains, or while the arrival gate is true.

When ready and stored destination u16+2 is nonzero, it changes the **destination to state1**, transfers eligible military members using `0x4b15d0(source,destination,[0,0,1])`, then changes the **staging/source region to state0**. Destination priority becomes70 in campaign mode,160 otherwise. Arrival issues order158 for the ordinary moved member in destination state1. The group word `PlayerAI+0x21e` and roster remain until `attack_clear`; a ready launch before timeout also retains last-attack time. A zero destination resets the source to state0 without transferring its list.

Twenty composed cases begin at the real dispatcher, compare state8/9 and campaign flag clear/set, and distinguish ready roster, missing roster, not-yet-arrived member, one time unit before expiry, and exact expiry. Four additional ground-unit cases execute the original transport-count, upgrade and point-connectivity helpers. Two zero-destination cases retain the original list and orders. These paths do **not** require a state8→state9 transition to launch.

Two further cases take this actual timeout launch into the original script scheduler with bytecode `[attack_do,attack_clear,wait(1)]`. The former grouping region is now state0, so `attack_do` continues instead of retrying. The real clear operation zeros the128-byte roster, group word and time; the destination stays state1, the military AI stays assigned there, and order158 remains. The script reaches **PC5/wait1**. This composes real launch and opcode effects, without externally changing the region state between them.

## Military transfer, including failure

`0x4b15d0(ECX=source, EDX=destination, stack=[extra_radius,include_casters,restrict_in_building])` walks the source MilitaryAI list while caching each next node. With `include_casters=0`, the native classifier retains IDs9,45,46,67. With `restrict_in_building!=0`, status`0x20` members remain in place outside campaign mode; campaign mode can transfer them. The exercised null-connected-unit branch is explicit, rather than proof of authentic occupied-garrison release.

For a moved ground member it decrements source u16 ground count with wrapping arithmetic. It unlinks the node, pushes it onto the **source** pool's free list at area+`0x4e20`, clears UnitAI, then calls real `0x4ae760(destination,unit,force=1)`. With a shared pool this can immediately reuse the released node. Allocation from a different destination pool can fail **after** source detachment: UnitAI remains0, the node remains free in the source pool, and no new movement order or RNG call occurs. This procedure is not an atomic transfer or boolean success API.

If the destination detector pointer+`0x28` is empty, it receives the source detector and clears the source pointer **before** member processing. This occurs even when all members are retained or allocation later fails. An already occupied destination detector preserves both pointers. Source cached slowest+`0x24` is not repaired by this transfer, and a deliberately zero source ground count wraps to65535 when one ground member is removed. A caller changing the old nonzero source state does not implicitly clear every cached pointer; [state initialization](native-campaign-attack.md) only clears the payload when the old state was0.

Sixteen single-member and four InBuilding cases execute release→allocation→arrival, two cases distinguish cross-pool success/failure, two mixed-list cases verify retained caster nodes, backward links and reversed moved-head order, and two cases distinguish occupied detector/ground wrapping/extra-radius behavior. The exercised `extra_radius=1` case executes the actual radius query on an empty index. Positive recruitment callback `0x4ada40 → 0x48dbc0 → 0x4ae760` remains a separate composition dependency.

## Strength, weapons, and dispersion

`0x4adbd0` clears local and all-military strength pairs, then sums cached air word Unit+`0x138` and ground word+`0x13a` from military members excluding status`0x20`. All strength accumulators are **u16 and wrap**. Local sums require the member's actual terrain region to equal the AI region. Campaign mode additionally runs the real radius256 callback `0x4aa2c0`, adding owned, local, non-InBuilding units independently of MilitaryAI membership. Five positive/query-gate cases prove owner, region and status filtering, and show that a member also present in the supplied spatial index is added by both passes.

Enemy-air strength+`0x18` raises needed-air+`0x0e`; enemy-ground+`0x1a` raises needed-ground+`0x0c`. Noncampaign mode uses `enemy + floor(enemy/2)`, campaign mode uses the original value. Existing larger needed values survive. A nonzero max-force word `PlayerAI+0x21c` caps both requirements afterward. Twelve cases combine campaign, caps0/10/200 and a remote member; four distinguish InBuilding status. These cached inputs are not proof of the upstream combat-strength or enemy-strength producer, which remains separately documented in [combat AI](native-combat-ai.md).

`0x4aaef0` returns ground-member presence and two all-missing-weapon booleans. Ground weapon getter `0x41fc20` reads the unit directly; unburrowed Lurker103 reports sentinel130 even when the loaded weapon field is non-sentinel. Air getter `0x41fb40` falls back to a nonnull subunit when the parent field is130. Seven cases cover weapon combinations, Lurker burrowing and asymmetric subunit fallback.

Transport capability `0x4aa8a0` uses actual completed-unit counts: Shuttle69 or Dropship11 suffices; Overlord42 suffices only with nonzero upgrade24, Ventral Sacs. Six cases execute both native accessors. Ground path checks can then call actual `0x48e980 → 0x481d00`; these fixture paths use real terrain-group comparisons, whose general contract is in [placement](native-placement.md).

With region flag`0x20` and insufficient local strength, `0x4aa9b0` can reset a state8/9 group to0 and reallocate members into their **current** terrain regions, issuing ordinary order157. Four full dispatcher cases exercise this route in both campaign modes and current-region choices. Noncampaign mode has a literal first-member exception for Firebat32/Zergling37: it suppresses this low-strength dispersion. Four discriminating cases execute that exception and the campaign alternative. Campaign neighborhood-owned-base queries are real, with an explicitly empty index in these fixtures.

A ground member in a terrain record marked`0x1ffd` selects global attack-target fallback instead of the ordinary timer path. Two complete no-enemy cases run the actual global selector and dispersal `0x4b1500`: source becomes0 and members are assigned to their current regions. Positive enemy selection/retargeting to state9 is present in the static witness, but not established by these empty-global-list cases.

## Reproduction and remaining selected dependencies

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_campaign_wave_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-campaign-wave-static.json \
  --orders-dat /private/tmp/koprulu-campaign-data/arr/orders.dat \
  --execute-original-bounded-probes \
  --probe-out docs/research/evidence/native-campaign-wave-probes.json
```

Default mode only reads/disassembles the hash-gated EXE. Runtime: Python3.9.6, pefile2024.8.26, Capstone5.0.6, Unicorn2.1.4 under x86_64 Rosetta. Stock orders.dat SHA-256 is `0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9`; its189 interruptibility/highlight/subunit-inheritance entries are unchanged. JSON records script/dependency hashes, reviewed instruction ranges, actual arguments, complete returns and state deltas. The bound is500,000 instructions per entry; this batch's longest complete call uses3293.

Pinned [AISE region layout](https://github.com/neivv/aise/blob/da4fed681dc09bda7a21c0051f60cdad98cc7226/bw_dat/src/bw/structs.rs#L781) corroborates field names. [GPTP order names](https://github.com/BoomerangAide/GPTP/blob/ce321f0fa83174aee741b91f1b2eaec30300773e/GPTP/SCBW/enumerations/OrderId.h#L315) distinguish156 ComputerAI,157 AtkMoveEP and158 HarassMove; those labels do not replace original order-handler evidence. Original local1.17 instructions determine the behavior here.

Positive score, regional/global target selectors and positive regional target-field updates are separately recovered in [strategic targets](native-strategic-targets.md). Selected positive ComputerAI-order consumers and global retarget→state9 transfer are composed in [attack consumers](native-attack-consumers.md). Remaining work includes upstream enemy-strength production, initial group recruitment from the periodic strategic scheduler, positive guard radius recruitment, transport actions, general region states1..7 and later movement/combat execution. These explicit boundaries prevent a completed wrapper path from being reported as complete strategic-AI or campaign parity.
