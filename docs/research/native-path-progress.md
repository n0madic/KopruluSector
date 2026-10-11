# Original waypoint production and movement-state 17

**336 complete cases /624 original entry returns pass**, connecting waypoint reuse, full local search, finite path publication, coordinate setters and the state17 success/failure/retry branch. These are complete selected functions in private memory; Sprite advancement between waypoint consumers is explicit fixture input rather than physical walking.

Target: StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred image base `0x400000`. [Static witnesses](evidence/native-path-progress-static.json) contain88 reviewed ranges /7,457 decoded instructions, including overlapping ranges. [Probe evidence](evidence/native-path-progress-probes.json) records2,633,550 original instructions, with maximum19,494 per return under a1,000,000 bound. There are no helper substitutions or imported calls. [Probe](../../tools/research/probe_native_path_progress_117.py) SHA256: `1f2e3489002f22d66e3ceec45099ef3639e64df158169f9c03f8a4be3ea054d8`.

## Full consumer and actual fields

`0x48eb90` takes ECX=CUnit and one packed coordinate argument, returning with `ret4`. It first calls the original [reuse predicate](native-path-storage.md), `0x48ee40`. That predicate reads the next point at **packed path+4**, reached through CUnit `+0x100`. It does not substitute the CUnit's separate waypoint fields. A true result returns1 immediately, without constructing or publishing another local segment.

When reuse fails, status **`0x100000`** selects the local-search branch. Actual `0x48ec50` builds/continues the local route and publishes a point. A zero result with a null CUnit path pointer returns0. Otherwise `0x43a260` tails into `0x40cd30`, then the wrapper calls the reuse predicate again and returns1. The second predicate's return is not the wrapper's return value.

The coordinate setter separately compares/writes CUnit WORD pairs **`+0x1c/+0x1e`** and **`+0x18/+0x1a`**. A change of the latter sets Flingy byte `+0x20` bit1. The evidence labels these as `attack_waypoint` and `next_target`; these analytical JSON labels identify the offsets, not a claim that this navigation setter executes a combat order. Pinned structure names are next target waypoint and next movement waypoint, respectively.

The96 chain cases cross six stock shapes—Marine0, SCV7, Tank30, Zergling37, Drone41, Zealot65—with four cardinal directions and distances1,64,128,256 from`(512,512)`. The unchanged consumer produces64-pixel intermediate points, then the exact goal. Both coordinate fields receive each consumed point. An immediate repeated call returns1 without changing the128-byte record or entering the local-path builder/coordinate setter. The complete sequence performs one local search. Sprite coordinates advance explicitly only after each pair of consumer calls.

All24 collision-enabled empty-pool cases finish the positive search but cannot publish a record: return0, null path and source-valued CUnit waypoint. This is a real finite-pool failure, not a substituted allocation result.

## Direct branch without collision status

When status`0x100000` is absent, `0x48eb90` calls actual destination setter`0x43a0d0`, then actual direct-record writer`0x48ef10`, and **returns1 even if the finite record pool is empty**. Consequently its success value alone cannot mean that a path record exists.

The96 direct cases cross the six stock shapes, four directions, available/empty pool and old movement-target equal/different from the goal. If the target was already equal, the destination setter returns early and preserves the CUnit movement waypoint at the source. Otherwise it writes the target and movement waypoint. This is distinct from the subsequent local-point setter above.

The original direct-record writer, ECX=CUnit with two packed coordinate arguments, uses the existing finite128-byte pool. On success it writes source from Sprite, next point at`+4`, goal at`+8`, zero WORD`+18`, full-length byte`+1b=1`, count/current bytes`+1c..+1f = 1,0,1,0`, and point payload at`+20`. It does not initialize every other byte in the record.

Twelve additional calls start with an existing record whose packed next point equals the Sprite position, forcing reuse to fail. The original writer pushes this record using one-based128-byte links and immediately pops it again. It preserves either an empty free head or the second supplied free record. The same existing record becomes the unit's new one-point path. Other record bytes and state are explicitly supplied diagnostic fixtures.

These status profiles establish the native branch distinction; they do not assert that every stock ground type naturally reaches each supplied collision-disabled profile.

## Complete movement-state 17 and RNG source 51

`0x42c400`, ECX=CUnit, calls the full consumer with the current movement target. Failure sets movement byte`+0x97=15` and returns0. Success normally sets state **25** and returns1. The48 stock success/failure cases cross six shapes, four directions and available/empty path pool.

After success, byte`+0x93` bit2 is cleared if set, preserving its other bits. Only when that bit was set **and** movement-type byte`+0x27=2`, the actual range RNG`0x478270` draws inclusive0..2 using source **51**, writes path timer byte`+0x19`, and selects state **18** instead of25. The supplied movement type is a fixture, not a claim that all six types use it normally.

The48 retry cases cross enabled/disabled RNG, seeds0,1,`0xfffffff8`,`0xffffffff` and six shapes. Enabled draws independently match:

```text
seed = uint32(seed * 22695477 + 1)
raw = (seed >> 16) & 32767
delay = (raw * 3) >> 15
```

The original total and source51 counters each increment once. Disabled RNG returns delay0 without advancing either seed or counters. Every retry case changes flags`0xa7→0xa5` and state17→18. Twelve further flag/type combinations select25 without consuming RNG. These exact state transitions are inputs to the [full movement dispatcher](native-active-iscript.md), which repeats when a state handler returns nonzero; physical continuation through subsequent states is not executed here.

## Reproduction and remaining movement

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_path_progress_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --units-dat /private/tmp/koprulu-campaign-data/arr/units.dat \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --static-out docs/research/evidence/native-path-progress-static.json \
  --out docs/research/evidence/native-path-progress-probes.json \
  --execute-original-code
```

Without the execution flag the tool checks the target and extracts witnesses only. Runtime: Rosetta x86_64 Python3.9.6, Unicorn2.1.4. Stock UnitsDAT bounds/property bytes and pinned PyMS identity are authenticated in the report. The clear1024×1024 graph, empty unit index, Sprite/CUnit state and finite records are fixture inputs. No Windows API, whole game, accepted campaign frame or native resource loader executes.

Together with [local continuation](native-local-paths.md), [authentic first segments](native-campaign-local-paths.md) and [positive unit blockers](native-path-blockers.md), this closes the selected path-to-waypoint-to-state transition. Remaining work includes physical acceleration/turning/position/index updates through the full movement FSM, collision and slide response, moving/targeted blockers, congestion and complete authored campaign-world composition.
