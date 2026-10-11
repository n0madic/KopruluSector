# Native physical obstruction, repath and sliding — StarCraft 1.17

**56 complete cases /11,030 complete original invocations pass.** Native-created Marine0, Zealot65 and SCV7 reach their goals around genuine created Marine/Zealot blockers. Eight cases create a Marine after20 completed SCV movement steps, forcing original collision responses rather than supplying an already detoured path. Original code maintains positions, headings, velocities, image animation, endpoint arrays, path records and movement states throughout.

Target: StarCraft.exe **1.17.0.1**, SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`. [Static witnesses](evidence/native-blocked-movement-static.json) contain21 dedicated ranges /1,529 decoded instructions with no undecoded tails. [Probe evidence](evidence/native-blocked-movement-probes.json) records10,508,160 instructions,240 visited function starts,783 reviewed ranges and61 authenticated source dependencies. Maximum invocation is54,484 instructions under the100,000 bound. Every top-level call returns with checked stack cleanup; no prefixes, helper substitutions, imports or full game execute.

Sources: [inspector](../../tools/research/inspect_native_blocked_movement_117.py), SHA256 `ffdc25694572987f2bd69d42cc2ee42ef49d7b352034f269abdc7e31381ae164`; [probe](../../tools/research/probe_native_blocked_movement_117.py), SHA256 `7495b076604d64110e4656d7b9f8cec9e754c4909637382155274af8210e0338`. [Separate image provenance](evidence/native-blocked-movement-assets.json), SHA256 `f67b0dd739a82d7c958be77e79cc0090046455dd73a30e354c8db5c30dfb59c0`, contains34 image IDs /30 GRP paths, extending the movement resources with249. Retail payloads remain private.

## World and physical inputs

Original acquisition normalization, creation/completion/show, stock image/IScript initialization and spatial insertion execute. Units/Flingy/Sprites/Images/Weapons/Orders DAT remain unchanged and use the native descriptors. Every unit belongs to owner3/controller2 without AI metadata. The synthetic4096-pixel map has uniform walkable region1, class0/group1 and empty terrain contours. Exactly one128-byte path record is available; it returns to the original free head after each settled route.

Source is `(512,512)`, goal is256 pixels away on each selected nonzero axis, and the blocker is halfway between source and goal. The48 initially obstructed cases cover three mover types, two blocker types and eight cardinal/diagonal directions. The eight later-insertion cases use SCV and Marine in the same directions. Original creation and one complete blocker movement/image pair establish stationary readiness; subsequent steps service the mover through complete `42ad30 → 418b20` pairs. No host assignment advances either unit, changes velocity or selects a movement state.

The frame DWORD `52651c` is an explicit monotonically supplied step clock, needed by native path-age tests. Its accepted-frame producer is not executed. The resource loader, command frontend, complete object callback, AI world and campaign map initialization are separate. Existing [clear movement/Move-order composition](native-movement-fsm.md) supplies the selected complete object-callback evidence.

## Follow-path collision and asymmetric shorter steps

State25 `42b8d0` composes actual path/state update, waypoint production, Flingy preparation, CUnit collision and terrain query. It commits a successful proposal through `42ab10`, including native Sprite and sorted endpoint maintenance. Static blockers feed the genuine indexed candidate/path-policy/local-contour search, rather than a host-authored candidate array. [Positive local blocker contracts](native-path-blockers.md), [collision query](native-movement-collision.md).

When a CUnit blocks the proposal and terrain does not, the original tries shorter movement only if:

```text
signed velocity_x >= 32 OR signed velocity_y >= 32
AND prepared moved_speed == current_speed
```

This is asymmetric. Negative motion on both axes does not satisfy the first predicate. The pinned Teippi reconstruction deliberately changes this branch to try shorter steps in every direction; that modification must not become original-game behavior. [Teippi movement source](https://github.com/neivv/teippi/blob/05c006c2f74ad11285c39d37135aed03d1fb8806/src/unit_movement.cpp).

The actual retries calculate each fixed coordinate as:

```text
half    = wrap32(current_fixed + trunc_toward_zero(velocity / 2))
quarter = wrap32(current_fixed + trunc_toward_zero(velocity / 4))
pixel   = lowWORD(fixed >> 8)
```

Each retry calls the original CUnit collision query again. Half success commits that proposal; otherwise quarter is tried. If quarter also fails, the original full fixed/pixel proposal is restored before the blocker-generation handle and saved speed are stored into the path and state28 is selected. Terrain collision chooses a different response and is not a positive branch in this batch.

The probe independently verifies signed division, DWORD wrapping, fixed/pixel conversion and failed-quarter restoration at the actual branch PCs. It records24 half queries,16 quarter queries and9 full-proposal restorations. Stock control0 SCV supplies positive velocity-based retries. The stock control2 Marine/Zealot images use IScript-driven movement; a nonzero physical displacement need not imply nonzero current-speed/vector fields.

For the later-insertion east SCV, one full collision proposes `(621,512)` at speed1280. The original accepts half at `(618,512)`. The next full proposal `(623,512)` remains blocked at half but succeeds at quarter `(619,512)`. The following full/half/quarter proposals all fail, so the full proposal is restored and dodge/repath handling begins. Negative vector components in later retries also match truncation toward zero.

## Generation handles, repath, turn and slide

The original encoder `428bd0` stores the one-based CUnit slot together with generation byte`+a5` shifted11. Resolver `428b50` validates the referenced object through the original dying predicate and generation comparison. The positive second-unit handle is2050. It is not a raw CUnit pointer.

State28 `42b3b0` validates that blocker, hidden/collision/pair state and its relationship to the current goal/waypoint. Actual bounds/side/direction helpers decide between stopping, waiting, replanning or sliding. This batch positively reaches:

- State19 `42c570`: resolve/save the dodge blocker, release the old path, search with `69b57c` set, clear that global and enter20 on success.
- State20 `42b2b0`: turn toward the new waypoint, then enter25 for an ordinary path. The alternative partial-path branch is static here.
- State23 `42bf10`: release/rebuild with the actual blocker override and choose25 or15, clearing the override afterwards.
- State31 `42c390`: for control0, halve saved speed at≥512, or clamp a value between256 and512 to256; copy it to current/next speed, rebuild velocity and enter32.
- State32 `42c1e0`: turn toward the saved dodge direction, advance in chunks no larger than256 raw speed, perform actual CUnit/terrain queries and publish each admitted position. It then checks the ordinary waypoint direction against the same blocker. When clear, an age below150 resumes25; the older-path branch selects19.

The report records all intermediate dispatcher states within a call. Across the56 cases, states0,9,10,11,12,19,20,22,23,25,26,28,31,32 execute. State24 and29/33 helpers are reviewed but are not claimed positive. Full terrain slide, moving-neighbor forecast, occupied goal, path-age150 and generation-loss branches remain separate.

## Spatial invariants and an observed corner crossing

Every step checks CUnit pixel = fixed>>8 = Sprite position, actual endpoint slots = stock extents around that position, both complete axis arrays sorted, blocker position unchanged, and spatial-query scratch released. The evidence contains4,949 original mover commits,4,805 after blocker insertion. Every published endpoint is independently outside the closed blocker rectangle expanded by the mover's opposite extents. Arrival requires five consecutive settled calls with exact goal, zero speed, null path and the finite record returned.

Continuous line segments are recorded as a **diagnostic**, not imposed as an original invariant. The later-insertion northeast SCV has two measured corner crossings against expanded rectangle `(621,620,659,661)`:

| Step | Old center | Published center |
|---:|---|---|
|56 | `(620,658)` | `(622,662)` |
|68 | `(656,662)` | `(660,660)` |

Both endpoints are clear, while the intervening continuous segment intersects the rectangle. Every actual intermediate published endpoint is also checked. The first crosses an x border, but the query's orthogonal filter uses the translated **final y interval**, already below the Marine; the second has the analogous final-x exclusion. This is the original query contract already discriminated by larger synthetic deltas, now observed at ordinary SCV speeds in a composed physical route. An initial strict continuous-segment assertion exposed this discrepancy; it was retained as evidence rather than described as universally clear travel.

A faithful Defold simulation therefore needs the original integer query/publication order. Replacing it with continuous collision or assuming that path geometry guarantees every physical segment remains clear would change these observations.

## Reproduction and remaining navigation

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_blocked_movement_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out docs/research/evidence/native-blocked-movement-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_blocked_movement_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-blocked-movement-assets \
  --asset-manifest docs/research/evidence/native-blocked-movement-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --case-group all --execute-original-code \
  --out docs/research/evidence/native-blocked-movement-probes.json
```

Default execution without the flag prints a plan. Runtime is Rosetta x86_64 Python3.9.6 /Unicorn2.1.4. Unknown hashes, unreviewed code, call-bound exhaustion, wrong cleanup or failed state/geometry/RNG invariants stop the run. Only the successful full56-case report is published.

Remaining compositions include terrain obstruction/dynamic contours, multiple simultaneously moving blockers, group congestion, positive neighbor forecasts/air repulsion, occupied/unreachable goals, later fallback/capacity branches, linked subunits, moving combat targets and authentic-map repeated physical travel. Selected obstruction consumers do not establish complete campaign or visual parity.
