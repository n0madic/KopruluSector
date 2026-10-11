# Complete selected local path construction and continuation (1.17)

**572 cases and1064 complete original entry returns pass**, connecting the original region route, local geometry search, packed finite path records and subsequent point/segment consumption. Selected rectangle detours are checked against independent exact-rational geometry. The unit's Sprite is advanced explicitly between point-consumer calls; physical Flingy movement does not execute in this batch.

Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`. [Probe](../../tools/research/probe_native_local_paths_117.py), [static witnesses](evidence/native-local-paths-static.json), [measured results](evidence/native-local-paths-probes.json). Addresses below are preferred VAs for this target. These results extend [long paths](native-region-paths.md), [path storage](native-path-storage.md), [terrain sweep/cache](native-terrain-sweep.md) and [unit collision](native-movement-collision.md).

## Executed construction chain

| Entry | Role in these complete calls |
|---|---|
| `0x491210` | Initialize source/destination region IDs, stock extents and path fields; choose same-region or real long-path search. |
| `0x491300` | Produce a long route on the declared graph/raster; unchanged bounded heap/arena helpers execute. |
| `0x491c10` | Advance the region cursor, choose an intermediate target or final goal, check nearby target objects/footprint, then run local search. |
| `0x491f50` | Complete local node search, neighbor expansion, costs, visited-area filtering and parent-chain output. |
| `0x491ba0` | Collect geometry and generate/filter neighbors for an actual selected node. |
| `0x48ec50` | Build/pack a missing path or consume the next retained point; unpack/release/continue when a segment ends. |
| `0x4919b0` | Initialize and search a following local segment while retaining the existing long route. |
| `0x492860` | Reverse a goal-to-source parent chain into retained local points. |

Actual target query `0x421c80 → 0x422330`, original release `0x4222b0`, target-footprint adjustment `0x492c00`, stock extent setup `0x4891c0`, contour import `0x488e10`, local terrain/unit collection, all four neighbor generators, visited-area insertion/filtering and native copy routines execute. The spatial endpoint count is explicitly0, so the selected complete searches have no other-unit blocker. The target-unit pointer is also0. Positive target objects, moving blockers and door/beacon predicates remain additional profiles.

The fixture uses32×32 tiles /1024×1024 pixels. All six units use unchanged stock dimensions: Marine0, SCV7, siege Tank30, Zergling37, Drone41 and Zealot65. Graphs, raster, contour pointers and unit records are declared private inputs, not imported original campaign worlds.

## Why an unobstructed path has intermediate points

Native geometry collector `0x488320` begins each expansion with a rectangle **current position ±64 pixels**, clipped to map coordinates0..width−1 and0..height−1. Its direction mask can suppress sides. Nearby extent-expanded contours contract this rectangle; cardinal/diagonal neighbors and visited areas are then generated in the native stored order.

The312 unobstructed cases vary six stock units, four cardinal directions and distances0/1/8/16/17/63/64/65/127/128/129/255/256. Every complete initializer produces the exact64-pixel boundary chain followed by the goal. For example, from `(512,512)` to `(768,512)`, the retained points are:

```text
(576,512), (640,512), (704,512), (768,512)
```

These are actual search outputs, not waypoints inserted by a comparison helper. Even an equal-source/destination full `0x491210` call returns1 and a single point equal to the source. The continuation initializer `0x4919b0` separately has a static equal-source early return0; do not infer that its guard applies to the full initializer.

The native local search uses32-byte nodes beginning at`0x69b788`, fixed-point costs and the recovered [approximate distance](native-region-paths.md). Its open heap is based at`0x69d6c8`, count`0x69d924`, with a sentinel pointer stored at`+0x260`. Static comparisons establish an open-set bound150 and250-node arena behavior, including earlier150/200-node fallback decisions under selected conditions at`0x492170/0x49219b`. This batch does not force every capacity/fallback branch. Node count is written to context WORD`+0x166`; maximum open count is WORD`+0x16a`. The earlier long-search counters occupy the other two WORDs`+0x164/+0x168`.

The reviewed local heap insertion bubbles equal priorities upward; downward repair prefers the left child on an equal-child comparison and stops when the current node is no worse. Existing nodes are identified by exact packed position. Strictly lower accumulated cost can change parents/reopen a node, while other encounters combine direction flags. These policies are visible in unchanged witnesses and exercised as part of search, but exhaustive standalone heap/reopen equivalence is not claimed here.

## Rectangle detours

The24 detour cases use the six types approaching a blocked rectangle from four sides. The blocked pixels are `[448,384]..[575,639]`, inclusive. Walk/contour fixtures are derived using the previously native-validated [contour comparison model](native-contours.md), then sorted by signed `(axis,start)` keys. Region1 is the surrounding walkable space; region2 is marked`0x1ffd` for the blocked tiles. This derivation is explicitly a fixture producer; the original contour/region producers are not rerun here.

For each returned path, an independent rational slab-intersection calculation expands the rectangle by the unit's unchanged dimensions:

```text
left   = 448 - unit.right
top    = 384 - unit.down
right  = 575 + unit.left
bottom = 639 + unit.up
```

Every complete path ends at the requested goal, has multiple points, and every segment stays outside this inclusive forbidden rectangle. This checks reachability and collision safety independently of native contour traversal; it does not prove global shortest-path optimality or all possible contour corner encodings.

Marine from `(256,512)` to `(768,512)` returns:

```text
(320,448), (384,384), (439,373), (503,373), (567,373),
(584,373), (648,437), (712,501), (768,512)
```

Stock dimensions change other types' detours. All exact output points, raw context flags, node counts, heap peaks and actual helper-entry visits are recorded in the report.

## Packed point consumption and empty-pool failure

Twenty-four connected cases call complete `0x48ec50` for six types and four cardinal distances. The first call runs actual initialization/search→`0x48e730` packing→`0x48e8d0` allocation. Later calls increment the packed current-point byte and consume the existing record without another search. Output, current index, stored next point and movement flag`0x10` are checked on every call. The final point equal to the unit's move target clears that flag.

Twelve additional cases use a genuinely empty finite free list. Search completes and the packer attempts original allocation, but `48ec50` returns0 with CUnit path pointer0. Output and prospective record bytes remain unchanged; movement flag`0x10`, set before the attempt, remains set. This is an allocation failure after positive search, rather than a replacement helper returning failure.

Original [path storage](native-path-storage.md) explains the128-byte payload reduction. Record byte`+0x1b` is the full region-chain count's low byte; it is not a partial-path flag.

## Long/local segment continuation

Seventy-two cases use explicit undirected, same-group stripe graphs with2/4/8 regions, six types and two directions. Half execute full initialization from the supplied graph to a local output; half execute `48ec50` repeatedly through the final destination with **one finite path record**.

`491c10` finds the current source region in the retained route. With at most three remaining regions, it uses the requested destination. With more, it uses the center of the region two entries ahead, while local search is directed through the next region. This yields a shorter intermediate local segment. In a four-stripe graph, moving right fromX64 produces pointsX128/192/256; moving left fromX960 producesX896/832/768/704. The extra leftward point follows the raster's boundary membership: X768 still belongs to the right stripe. The local segment's reached-goal byte`+0x1e` is0 for these intermediate outputs.

On segment exhaustion, `48ec50` executes actual unpack→unit release→`4919b0` continuation→local search→pack→pop. The full-count byte equals the retained region count in these short graphs, so the existing long route can be reused. The connected cases execute exactly **one long search** each;2/4/8 regions require respectively0/1/5 local continuations and the same number of actual record releases. Every reallocation returns the same sole record. The complete consumed route is the exact14-point cardinal chain across896 pixels; the last movement flag`0x10` is0.

These calls establish segment selection and continuation on declared graphs. Authentic campaign graphs, disconnected/partial long routes, path shortening, positive target collisions and full movement remain separate compositions.

## Parent-chain output limit

All128 valid parent-chain lengths1..128 execute complete `492860`. The input is explicitly ordered goal→parents→source. Output excludes the start and reverses the remaining chain. Above50 output points, the function keeps the first49 points from the source side and then the **actual goal**, discarding intervening tail points. This differs from the later128-byte packer's prefix-only truncation; both stages must be preserved. Current-point index is reset to0.

## Reproduction and evidence boundaries

| Family | Cases |
|---|---:|
| Unobstructed cardinal full initialization |312 |
| Stock-dimension rectangle detours |24 |
| Full unit path build/point consumption |24 |
| Empty pool after positive search |12 |
| Parent-chain reversal/50-point limit |128 |
| Long/local initialization and finite continuation |72 |
| **Total** | **572** |

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_local_paths_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --units-dat /private/tmp/koprulu-campaign-data/arr/units.dat \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --static-out docs/research/evidence/native-local-paths-static.json \
  --execute-original-code --out docs/research/evidence/native-local-paths-probes.json
```

Default mode is static only. Unknown executable hashes are rejected before parsing/emulation. The unchanged target runs in private Rosetta x86_64 Python3.9.6 / Unicorn2.1.4 memory. Each executed PC must be a decoded instruction in77 reviewed blocks /7109 instructions; top-level return/stack cleanup is checked. The bound is1000000 instructions per call; maximum observed is286808. The complete recorded runs execute11671003 instructions. No helper stubs, imports, WinMain, guest host-file or network calls occur.

Probe SHA-256 is `30f48892838c9e2495561a6dd6726222d1ed7543e2d6be7271c6dd575aa676f0`. Seven local source dependencies, stock Units DAT and pinned PyMS identity are recorded. Pinned [OpenBW local search](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L9421-L10563) and [continuation/packing](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L10614-L10862) are source leads; complete original calls establish the selected local behavior here.

The [completion ledger](completion-ledger.md) retains all unresolved dynamic geometry, capacity/fallback profiles, target-adjustment edge cases, positive blockers, authentic-map local routing, cache invalidation and physical movement obligations. A complete call over a declared fixture is not a played-campaign trace.
