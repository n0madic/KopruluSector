# Native 1.17 terrain movement on all campaign contours

**10,608 complete original query cases pass on all68 authenticated maps.** This extends the [synthetic sweep/cache contracts](native-terrain-sweep.md) to full directional contour arrays containing many obstacles, real corner flags and native sorting ties. Native qsort and source-footprint checks contribute1088 additional complete returns, for **11,696 entry returns** in the batch. Target: StarCraft.exe1.17.0.1, SHA-256`94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase`0x400000`.

Evidence: [14 static blocks,1716 instructions](evidence/native-campaign-terrain-sweep-static.json), [original execution report](evidence/native-campaign-terrain-sweep-probes.json), [runner](../../tools/research/probe_native_campaign_terrain_sweep_117.py). No helper replacements, Windows imports, whole game, movement dispatcher or campaign execution run.

## Authenticated inputs and original consumers

Each CHK, CV5 and VF4 payload is hash-checked against [terrain provenance](terrain-assets.md). The host reconstructs complete megatile-ID and flag arrays and compares their whole-byte hashes with [native MTXM import](native-terrain-import.md). The reconstructed walk grid is compared with its recorded original producer, then contour geometry is reconstructed and sorted by actual native qsort`0x4d4490`/comparator`0x48ad40`.

The entire sorted eight-byte record stream must match both [native contours](native-contours.md) and [the preceding all-map footprint batch](native-contour-fit.md). This authenticates direction, coordinates, information bytes and actual native tie ordering. Native terrain/contour producers are **not rerun in the same instance**; their byte-identical outputs are inputs. Each orientation uses separately supplied storage with capacity15000; allocator/growth behavior is not exercised here.

For each map, six stock types are tested: Marine0, SCV7, Siege Tank30, Zergling37, Drone41 and Zealot65. Their unchanged DAT extents are copied to the original extent table as explicit loader fixtures. Two diagnostic source points per type are selected beside real walls, falling back to valid tile centers if necessary. Every selected point passes complete original footprint entry`0x42ed50`; **816 positive source validations** execute. Selection is a deterministic research procedure, not an original AI or pathfinding algorithm.

Each source then executes thirteen successive complete calls to movement consumer`0x42da50`, retaining the unit's cache between calls:

```text
(0,0), (-1,0), (0,-1), (20,0), (0,20), (-20,0), (0,-20),
(80,80), (-80,-80), (256,0), (0,256), (-256,0), (0,-256)
```

These are prospective deltas. The unit's source/Sprite position **does not advance** between queries. Large deltas deliberately distinguish crossing/contractor behavior; they do not establish natural per-frame movement speeds.

## Complete cache/sweep composition

The original consumer compares the signed destination with Unit+`0x108`'s inclusive safe rectangle. A hit returns0 without terrain lookup or extent initialization. A miss initializes the current point's128-pixel neighborhood, executes stock extent initializer`0x4891c0` and complete contractor`0x48b9e0`, and stores the contracted rectangle. If the destination lies outside that newly calculated rectangle, it invokes actual directional sweep`0x48bec0` and inverts its clear result into collision1/0.

The independent geometry oracle iterates all ordered contour records, applies extent/corner expansion, contracts in the original round-robin direction sequence and checks swept interval overlaps. Every query compares **both the complete stored rectangle and returned collision result**, and checks whether the real contractor was entered. This distinguishes an accidentally correct boolean from a wrong cache that could change later calls.

| Recorded outcome | Cases/calls |
|---|---:|
| Complete prospective query cases | 10608 |
| Cache rebuild cases | 7843 |
| Existing-cache hit cases | 2765 |
| Actual directional sweep invocations | 7027 |
| Returned collision cases | 6087 |
| Positive original source-footprint validations | 816 |
| Complete original qsort setup returns | 272 |

The longest movement query executes24977 instructions; bounded qsort setup may take millions. The per-entry execution bound is10million instructions. All68 maps include their complete arrays, including the three maps exceeding the original initial5000-record orientation capacity. Private capacity changes storage only; it does not replace the queried record stream.

The [previously demonstrated native vertical/horizontal corner-policy difference from pinned OpenBW](native-terrain-sweep.md) remains part of the oracle. This batch finds no additional mismatch in its authentic array/source/delta set. It does not prove that all source points and movement vectors are covered, or that the pinned reconstruction implements every native movement rule.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_campaign_terrain_sweep_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --terrain-report docs/research/evidence/terrain-assets.json \
  --terrain-import-report docs/research/evidence/native-terrain-import-probes.json \
  --contours-report docs/research/evidence/native-contours-probes.json \
  --contour-fit-report docs/research/evidence/native-contour-fit-probes.json \
  --terrain-data /private/tmp/koprulu-terrain-data \
  --campaign-data /private/tmp/koprulu-campaign-data \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --static-out docs/research/evidence/native-campaign-terrain-sweep-static.json \
  --execute-original-code \
  --out docs/research/evidence/native-campaign-terrain-sweep-probes.json
```

Runtime: Python3.9.6 x86_64 under Rosetta, Unicorn2.1.4, pefile2024.8.26, Capstone5.0.6, pinned PyMS`bfc5d3aad0b5614a5aff72c223f8efa00afddfa4`. Runner SHA-256`998a599e3b31e391d78a13f0b4b46004d6082c5938ca803bbf306a2eb5f868d2`. Static regeneration was byte-identical. A different EXE hash was rejected before PE loading, Unicorn setup or output publication. Every executed basic block is checked against the reviewed instruction address/size set.

## Remake contract and remaining boundaries

A faithful Lua simulation needs an explicit safe rectangle per unit, inclusive hit tests, native direction/endpoint policy and invalidation at the actual original callers. Independent render/physics state must not replace this cache. Whole-array geometry comparisons are useful converter fixtures, while the retained native sorting bytes constrain any order-sensitive implementation.

This batch closes the selected authenticated multi-record query composition. It leaves native contour allocation/growth, dynamic terrain/doodads, **cache invalidation callers**, other-unit collision, local path search, full movement state transitions, natural unit creation and accepted-frame scheduling as separate work. Valid footprint fixtures plus correct query results do not establish a moving unit, a complete initialized mission or remake parity.
