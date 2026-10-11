# Original local routes around positive unit blockers

**587 complete cases /635 original entry returns pass.** Forty-eight cases execute real positive CUnit enumeration, path collision policy, generated local contours and complete local search. Another539 cases discriminate the direct path-policy gates. Target: StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, image base `0x400000`.

[Static evidence](evidence/native-path-blockers-static.json) contains77 reviewed ranges /7,109 decoded witness instructions; ranges can overlap. [Probe evidence](evidence/native-path-blockers-probes.json) records635 complete original returns,3,509,113 instructions, zero helper replacements and zero imports. Each call has a1,000,000-instruction bound; maximum observed104,513. Static-only regeneration is byte-identical. [Script](../../tools/research/probe_native_path_blockers_117.py) SHA256: `ea95c1c806aaf42667254dd0f96f5b051a4c2bad819594cb8f25d63ee189ced1`.

## Positive unit geometry and detours

Six stock mover shapes—Marine0, SCV7, Tank30, Zergling37, Drone41 and Zealot65—search in four cardinal directions across an otherwise clear1024×1024 map. A stationary Marine0 or CommandCenter106 occupies`(512,512)`. Mover source/goal pairs are`(256,512)↔(768,512)` and`(512,256)↔(512,768)`.

CUnits occupy the original pool base `5da3a0` and its next150-byte record. Sprite positions, Completed/collision status`100003`, finder flag1 and current orders are declared. Extents and property flags are unchanged effective UnitsDAT fields. Both axis interval arrays contain the two real unit IDs, sorted endpoints and signed sentinels; their producers are authored fixture setup. Largest-unit query dimensions are explicitly256×256. This batch does not claim native insertion or bootstrap of that index.

The unchanged `491210` initializer reaches actual positive `488600/488910` enumeration, `425640` pool lookup, `42e490` policy, `42ebf0` pair policy, `488f00` local contour insertion and local search/reconstruction. The candidate units are genuine records read by original instructions, not replaced callback results. The original spatial scratch depths return to0.

All48 complete routes reach their requested goal. Every one of444 returned segments avoids the independently expanded blocker rectangle under an exact-rational slab test:

```text
left   = blocker_x - blocker_left - mover_right
top    = blocker_y - blocker_up   - mover_down
right  = blocker_x + blocker_right + mover_left
bottom = blocker_y + blocker_down  + mover_up
```

For Marine against Marine, the positive horizontal route includes`(495,532)→(529,532)`, passing immediately below the closed forbidden rectangle. The CommandCenter profile yields a wider detour. Unit avoidance therefore depends on stock bounds and the native local contour geometry, not just a blocked center point. No shortest-route or physical movement claim follows from this finite diagnostic set.

## Path policy differs from a universal unit-collision test

Direct `42e490`, ECX=potential blocker and EDX=mover pointer, first compares the blocker's move-target coordinates with its Sprite position. An exact match permits evaluation of the remaining gates. If they differ, evaluation only continues when this blocker equals shared target pointer`69b57c`, or shared path mode`69b578` is nonzero. Otherwise it returns0 immediately.

The remaining gates reject:

- Types205,206,207,208 through actual `4290a0`, irrespective of the other supplied profile fields.
- Finder byte`+105` without bit1.
- NoCollision status`200000`.
- The mover itself, when the supplied mover pointer equals the blocker.
- A failed actual `42ebf0` pair policy, when a nonnull different mover was supplied.

A null mover bypasses the pair helper while retaining the other gates. Stationary fixtures with status`80000` set or clear have the same measured result. No broader meaning is assigned to that status bit here.

The539 direct cases use seven mover types, seven potential blocker types and eleven profiles. They distinguish the stationary coordinate test, differing move target, shared target override, shared mode override, finder/collision suppression, diplomacy0/1 under status`400000`, self, null mover and status`80000`. The special Larva35 branches come from the previously proven [pair contract](native-movement-collision.md). Their supplied completion/status fields are fixtures, not proof of naturally initialized Larva state.

Consequently a remake must preserve both the path-specific gate and the lower pair policy. Replacing these with a single rule such as “all collision-enabled units obstruct every planned route” would change the original output.

## Reproduction and remaining consumers

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_path_blockers_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --units-dat /private/tmp/koprulu-campaign-data/arr/units.dat \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --static-out docs/research/evidence/native-path-blockers-static.json \
  --out docs/research/evidence/native-path-blockers-probes.json \
  --execute-original-code
```

Omitting execution only checks the target and disassembles. Runtime: x86_64 Python3.9.6 / Rosetta, Unicorn2.1.4, Capstone5.0.6 and pefile2024.8.26. Exact dependencies and UnitsDAT identity are recorded in the JSON.

The prior [all-map local search](native-campaign-local-paths.md) supplies authentic terrain/graph composition; this positive-unit batch supplies explicit index records and a clear graph. Their combination with naturally populated campaign state, moving blocker's real status/cadence, target-unit adjustments, multiple unit blockers, congestion, physical collision/slide response, repath and native index/cache maintenance remains required. Full movement and complete campaigns have not executed.
