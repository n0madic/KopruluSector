# Populated Terran construction, interruption and cancellation

**Six complete scenarios pass through 3,095 original function invocations.** A genuinely allocated SCV walks to the commanded point, pays for a Supply Depot, creates it with stock images, performs construction movement and welding, and completes it. Stop/resume, builder Death/replacement, Operation CWAL, and early/late cancellation run through complete object callbacks with original helpers. The initial build queue is an explicit caller fixture; upstream player/AI command admission remains separate.

The target is StarCraft **1.17.0.1**, executable SHA256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, preferred ImageBase `0x400000`. Addresses below are preferred VAs; subtract that base for RVAs. This extends [placement](native-placement.md), [movement](native-movement-fsm.md), [primary orders](native-primary-orders.md), [populated image allocation](native-image-initialization.md), [object/IScript service](native-active-iscript.md), [Death](native-unit-death.md), [supply accounting](native-training.md) and [production cancellation](native-production.md). Their frozen sources and reports are unchanged.

## Evidence and caller world

- [Static inspector](../../tools/research/inspect_native_construction_117.py), SHA256 `93fd0bc327a78eb5e25038235850acfdd9335042261f3d9dd5724e5fe7a66a9f`, produces [33 ranges / 2,266 decoded instructions](evidence/native-construction-static.json), without undecoded tails. The seven-entry self-build and nine-entry construction switch tables are separate data witnesses.
- [Original-instruction probe](../../tools/research/probe_native_construction_117.py), SHA256 `018fb90419b296f6d2d5527c1819a3d72d9c14f955dfb44fbf40fd3d09f5b409`, produces [six complete scenarios](evidence/native-construction-probes.json), **17,449,531 executed instructions**, **400 visited range starts**, **893 reviewed ranges** and **68 authenticated source dependencies**. All3,095 calls reach the sentinel with original stack cleanup; there are no prefixes or substituted helpers.
- [Image provenance](evidence/native-construction-assets.json), SHA256 `85085b5d52a740a9fb1510303b918562c009a7a692a254dcbb2630b0a86cdd71`, covers **143 image IDs / 122 distinct GRP paths**, selected original LO resources, archive provenance and payload hashes. Payloads stay in private scratch storage.

The runtime is Rosetta x86_64 Python3.9.6, Unicorn2.1.4, pefile2024.8.26. Each invocation has a250,000-instruction bound; the observed maximum is23,065. Original RNG totals/source counters/final state match an independent integer LCG. [RNG specification](native-prng.md).

Original Units/Flingy/Sprites/Images/Weapons/Orders DAT, IScript, GRP and LO inputs are hash authenticated. `424900` creates the SCV7 and `424da0` completes/shows it at512,512. Owner3 is human controller2, with explicit diagonal self-alliance1 and cross-alliance0. Terrain is declared flat/walkable4096×4096 pixels,128×128 tiles, one region/group1, no obstacle contours, a finite path pool, tile flags`0x1f700` for owner3 explored/visible input, and an empty128×128 creep plane. Eight empty valid AI-region arrays and slow-service countdowns10,000 are supplied.

The host writes queue slot0=109 and supplies balances1000 minerals/1000 gas, then issues actual position order30 to704,512 through `42a340`. All subsequent movement, queue consumption, debit, CUnit/image allocation, HP/timer progress, completion, supply publication and pool recycling execute original instructions. Each selected tick invokes complete **`445070`**; the external logical-frame counter `52651c` is a caller fixture. Current-unit/Flingy/Bullet globals must all return to zero after every callback. The host never advances a position, heading, build stage, timer, HP or pool during progression.

This is the selected complete object phase, with real movement/spatial/image services. Full accepted-frame readiness, command/trigger phases, native map loading, generated visibility, AI Town assignment, local completion voice presentation and campaign play remain separate. No Windows import, WinMain, guest host-file or network operation executes.

## Selected ABI and order chain

| Entry | Selected contract |
|---|---|
| `41a580` | ECX=SCV; PlaceBuilding30, including actual movement, requirements/payment and incomplete building allocation. |
| `41bcb0` | ECX=worker; validates the current queued type and debits its stock cost on the accepted branch. |
| `41a870` | ECX=SCV; ConstructingBuilding33, target validation, builder ownership, construction movement/welding and progress tail. |
| `41a770` | ECX=building; BuildTerran44 changes construction graphics at HP thresholds. |
| `41a280` | ECX=building, stack=[raw HP gain, check-space flag], `ret8`; clamped HP gain, WORD build-time decrement, then completion when time was already zero. |
| `41b290` | ECX=building; reads WORD HP gain+`a8`, shifts it left4 under CWAL flag2. The construction caller then zero-extends AX. |
| `41b1f0` | ECX=SCV; restores DAT sprite elevation, clears status`0xa00000`, sets Collides`0x100000`, executes collision/path reset and resumes/orders idle. |
| `428a10` / `43a4e0` | ResetCollision150 and its actual movement-state reset consumer. |
| `41a340` | ECX=incomplete building; selected Terran cancellation/refund/Death issuance. |
| `408490` | CL=owner, DX=type; refunds floor(3×u16 mineral/gas cost÷4) to current stock with DWORD addition. |
| `48e910` | Native encoded-head path-record free, reached by late-cancel overlap escape. |

The connected chain is:

```text
position order30 → physical approach → payment/424900 incomplete Depot
  → SCV ConstructingBuilding33 + Depot BuildTerran44
  → original work-point movement and welding + HP/time tail
  → Depot full construction graphics/Nothing23
  → build-time zero on a later call → Completed + supply + worker ResetCollision150
```

PlaceBuilding stage0 chooses a target sprite point when supplied, otherwise uses its order point, then calls actual `43a070` movement. Stage1 waits for the worker's native position to match the move target before placement `417aa0` and payment `41bcb0`. The selected Supply Depot allocation uses `424900`; type110 has a separate refinery producer. The consumed build slot becomes empty228. Success writes building+`80`=worker, worker order/stage33/3 and target pointer, issues building44, and calls AI assignment `486200`. The latter returns through the human-controller guard in these cases.

## Normal HP, timer and graphics phase ordering

With the declared geometry, callback57 pays100 minerals, publishes incomplete Depot109 at704,512 with **HP12,800 (50HP), build time600**, and sets worker33/stage3. Callback58 reaches worker stage4 without advancing building HP/time. Callback59 applies the first **192raw HP** gain and decrements time to599.

`41a280` always applies clamped HP gain before inspecting the WORD remaining time. Positive time is decremented1 normally; zero time at entry calls `424da0` for the builder's check-space0 path. Thus callback658 has maxHP128,000 and time0 while **Completed is still false**; callback659 sets Completed, clears building+`80`, publishes supply, and leaves the worker at ResetCollision150. Max HP alone cannot stand in for completion.

BuildTerran44 uses strict HP comparisons against one-, two-, three- and four-fifths of stock max HP. Its interleaved stages change animations/construction graphics on subsequent visits:

| Callback | HP raw | Time | Building order/stage |
|---:|---:|---:|---|
| 57 | 12,800 | 600 | 44/0 |
| 125 | 25,664 | 533 | 44/1 |
| 126 | 25,856 | 532 | 44/2 |
| 259 | 51,392 | 399 | 44/3 |
| 260 | 51,584 | 398 | 44/4 |
| 392 | 76,928 | 266 | 44/5 |
| 393 | 77,120 | 265 | 44/6 |
| 525 | 102,464 | 133 | Nothing23/0; Completed=false |
| 658 | 128,000 | 0 | Nothing23/0; Completed=false |
| 659 | 128,000 | 0 | Nothing23/0; Completed=true |

The full building sprite therefore precedes logical completion by134callbacks in this case. This is an original construction-graphics consumer, not a host stage change.

## SCV work-point movement and RNG

Construction order33 validates the target and uses actual follow/distance helpers. Its stage2 refuses a different active builder when target+`80` points at a worker still targeting that same building. Accepted ownership stores target+`80`=this worker, clears collision, sets elevation to DAT+1, queues ResetCollision150/idle and moves toward the building. Stage3 waits for arrival, sets stage4 and returns before HP progress on that transition.

Stage4 draws two source9 RNG coordinates from **the current building Sprite's byte width/height, centered on its Sprite position**. A separate fallback rectangle is derived from DAT placement dimensions, shrunk by worker extents and intersected with the sprite rectangle. Only a rejected first candidate draws again in that fallback rectangle. `41a4b0` clamps the point; original movement/path helpers commit the route. Using only the building footprint for the first draw would change both coordinates and potentially the RNG schedule.

Stage5 advances to6; stage6 checks distance20, stops and chooses the native waypoint/heading offset; stage7 checks waypoint equality or weapon-angle tolerance, sets timer `(source10 RNG & 0x3f)+30`, plays welding animation15 and advances to8. Stage8 returns to4 when the timer reaches0, with animation8. Applicable active stages also execute the independent HP/time tail. Welding animation duration is not the exclusive construction clock.

The baseline consumes121RNG draws: source3=45,5=45,9=18,10=8,14=2,15=2,36=1; final seed3,756,437,078 from12345. Each interrupted/CWAL scenario retains its own independently validated source schedule. Native worker/Sprite/fixed coordinates and sorted spatial borders are checked throughout visible movement, including overlap escape after Stop and cancellation.

## Stop, builder Death and replacement

Stop is issued through actual `42a2c0(worker,[1])` at callback119, with HP24,320/time540. After32complete object callbacks, those two building fields are unchanged. The building retains its worker+`80` pointer while the stopped SCV restores collision/elevation and physically escapes the footprint. Resume uses actual targeted order33 through `42a380`; it performs no queue write or second payment. Completion at713callbacks reflects the pause and native reapproach.

In the Death variant, actual `42a4d0(worker)` at the same point clears building+`80` through original removal. After80object callbacks the building still has HP24,320/time540, and the old SCV has no Sprite and belongs to the genuine free list. A newly allocated/completed SCV at512,512 receives order33 targeting that existing building. It resumes and completes at816callbacks without another debit. Its CUnit address differs from the original because the removed object was returned at the pool tail. The building's incomplete state survives independently of builder lifetime.

## Cancellation and CWAL

Early cancellation calls complete `41a340` at119 with HP24,320/time540. Late cancellation calls it at599 with HP116,480/time60. Both refund exactly75minerals: current balance900→975, gas remains1000. This path uses stock u16 cost and three-quarter integer arithmetic, with no progress scaling.

Original Death issuance precedes later object/IScript removal. The first returned object snapshot still permits a final worker progress tail on the dying target (early HP24,512/time539; late HP116,672/time59). Final cleanup reaches the CUnit free list with Sprite0 and no pending-delete nodes at146/626callbacks, retains one SCV, and restores that worker's stock PlayerGuard3. Refund and reclamation are separate phases. Completed-building guards, nonbuilding/morph cancellation, paired Nydus handling and other cancellation branches are static witnesses here, without positive connected coverage.

With initial CWAL flag2, the same paid placement occurs at57 and completion at97callbacks. WORD HP gain192 becomes3072 through `41b290`; the caller's AX truncation remains part of the arithmetic contract. Positive WORD remaining time subtracts16, clamping a negative signed result to0; actual completion still occurs only on a later invocation that enters with time0. These cases establish the stock Depot values, not every possible gain/time overflow input.

## Supply and simulation implications

The native supply arrays are `(provided, used, global cap)`, not `(maximum, used, total)`. For owner3 the selected Terran addresses are `5294b4/5294e4/529514`; Zerg uses `529424/529454/529484`, Protoss `529544/529574/5295a4`. After SCV creation, the declared triplets are Zerg20/0/400, Terran20/2/400, Protoss20/0/400. Depot completion adds16 to **provided**, producing Terran36/2/400. Cancellation leaves20/2/400. See the [corrected training accounting](native-training.md) and [ownership accounting](native-trigger-ownership.md); historical fixture variable names in frozen reports are not semantic field authority.

The simulation must preserve separate queues/targets, builder ownership, paid incomplete building lifetime, work-point movement and RNG, HP/time/completion, graphics thresholds, native interruption, and later reclamation. Presentation should consume these authoritative states. The observations above specify six clear-world examples; they do not certify an implementation or campaign runtime.

Remaining connected scope includes Protoss/Zerg construction and morphs, repair, addons, lift/land, refinery replacement, AI Town/request/builder assignment, unavailable resources/space/finite allocation failure, conflicting builders, moving obstacles, interrupted target destruction, command requirements and broader original-frame scheduling.

## Reproduction

The paths below reference private original inputs; the repository stores their hashes and extracted instruction metadata only. [Reproduction prerequisites](reproduction.md).

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/inspect_native_construction_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --out /private/tmp/construction-static.json

arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_construction_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --data /private/tmp/koprulu-campaign-data \
  --manifest docs/research/evidence/retail-manifest.json \
  --assets /private/tmp/koprulu-construction-assets \
  --asset-manifest docs/research/evidence/native-construction-assets.json \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --out /private/tmp/construction-probes.json \
  --case-group all --execute-original-code
```

The image manifest records143selected IDs. Recreate those IDs with the unchanged [image asset extractor](../../tools/research/inspect_native_image_assets_117.py), the pinned StormLib and the declared original archive set, before executing the probe. Without `--execute-original-code`, the probe prints its plan only.
