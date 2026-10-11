# Native terrain movement sweep and safe-area cache (1.17)

Original movement collision uses both an extent-expanded contour sweep and a cached rectangle around the unit. **3241 complete original-code cases pass** for the declared contour/unit fixtures. A concrete directional difference from pinned OpenBW is recovered: vertical-wall sweeps reject inclusive span overlap before corner-mode exemptions that can admit a horizontal-wall sweep.

Target: StarCraft.exe **1.17.0.1**, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`. [Static witnesses](evidence/native-terrain-sweep-static.json), [3241 complete cases](evidence/native-terrain-sweep-probes.json), [probe](../../tools/research/probe_native_terrain_sweep_117.py). No helper stub, imported call, whole game or guest host-file/network operation executes. Stock DAT extents are authenticated; contours, units, sprite positions and cache contents are explicit synthetic caller data.

## Complete original consumer chain

| Entry | Inputs | Return/effect |
|---|---|---|
| `0x42da50` | ECX=unit, stack=[deltaX,deltaY] | Returns1 on terrain collision; updates unit+0x108 safe-area rectangle when needed. |
| `0x4891c0` | ECX=contour context, stack=[unit ID] | Writes the original inner/outer extents. |
| `0x48b9e0` | ECX=context, stack=[currentX,currentY,rectangle] | Contracts the supplied safe rectangle using sorted terrain contours. |
| `0x48bec0` | ECX=context, stack=[sourceX,sourceY,destinationX,destinationY] | Returns1 if the directional sweep is clear,0 if blocked. |

The outer movement helper reads current sprite position, adds the supplied movement and retains the low16-bit destination. Signed WORD comparisons determine membership in the stored rectangle `[left,top,right,bottom]`. If the destination is inside it, the helper immediately returns0 without consulting contours or initializing extents. Cache production/invalidation therefore matters separately from collision geometry.

When the destination is outside the existing cache, the helper initializes bounds to **current position ±128 pixels**, initializes stock extents and calls the original rectangle contractor. If the destination is inside the new rectangle it returns0. Otherwise it calls the original sweep and returns its boolean inverse. This helper does not by itself test center-point walkability, other-unit collision, flight status or order eligibility; the surrounding movement callers remain separate contracts.

## Expanded contours and directed crossing

The inner extent vector is `[up,-right,-down,left]`, as established in [footprint fitting](native-contour-fit.md). For an eight-byte contour record `(axis,start,end,direction,info)`, the sweep expands:

```text
expanded_axis  = axis  + inner[direction]
expanded_start = start + inner[info & 3]
expanded_end   = end   + inner[(info >> 2) & 3]
```

Native additions retain WORD results. Coordinates and comparisons are signed16-bit in the consumed geometry. Bits4 and5 of info encode the two corner modes. Bits6 and7 are not selected by the recorded extent/corner instructions.

Only the direction being approached is tested: top for decreasing Y, bottom for increasing Y, left for decreasing X and right for increasing X. Equal coordinates skip that axis. The initial directional search admits an axis at the current extent-shifted boundary; the subsequent scan stops beyond the destination's expanded boundary. The other coordinate uses the inclusive min/max span between source and destination. This is a directional swept-range rule, not an arbitrary line-intersection replacement.

For a reached contour with inclusive interval overlap:

- **Top/bottom:** collision occurs if either corner bit is clear, or expanded start is at/before the movement span's low endpoint, or expanded end is at/after its high endpoint. Both corner bits set can admit a contour strictly inside the span.
- **Right/left:** the original preceding comparisons reject overlapping spans, including that interior-contour case. The corner tests do not supply the symmetric exemption above.

The difference is visible at original vertical branch instructions `0x48c2a8` and `0x48c1af`, versus horizontal `0x48bfc1` and `0x48c0b8`. The [pinned OpenBW sweep](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L8923-L8978) expresses the corner rule symmetrically for all four directions. It is a useful source lead; copying that policy would fail these local1.17 calls.

Four exact recorded cases use one edge at axis400, endpoints380..420, info48 (`0x30`) and an orthogonal movement span350..450. The two axis coordinates cross350↔450 in the selected direction; stock Marine extents expand the edge. Native clear returns are **top1, right0, bottom1, left0**. The symmetric reference would return1 for all four. These are explicit fixtures, not a claim that a particular mission naturally produces this exact edge and trajectory. The report retains both expectations in `reference_conflicts`.

## Safe rectangle contraction

The contractor finds the nearest starting iterator in each sorted directional array using the unit's extent-shifted current position. It advances in rounds: top backward, right forward, bottom forward, left backward. For each expanded contour it first checks the current rectangle limits and interval overlap. A contour beyond a contracted bound ends that direction's scan.

Within an admitted overlap, the contractor compares distance to the contour axis with distance to the nearest interval endpoint. A strictly nearer corner contracts the perpendicular side to **start-1** or **end+1**; otherwise the axis side contracts to **axis±1**. Equality selects the axis contraction. Later directions see the already changed rectangle. This order is part of the behavior contract. The separately expressed comparison model follows the pinned [OpenBW safe-area contraction](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L8807-L8921); original complete calls verify selected stock/fixture compositions.

## Cases and evidence limits

| Family | Cases |
|---|---:|
| Complete stock Marine extent setup | 1 |
| Four directions × all64 low info encodings × nine orthogonal span profiles | 2304 |
| Eight stock types × nine positions × thirteen consecutive cache/movement queries | 936 |
| **Total** | **3241** |

The stock types are Marine0, SCV7, Tank30, Zergling37, Drone41, Zealot65, Command Center106 and Gateway160. A supplied building movement query establishes helper arithmetic, not natural walking eligibility. Cache positions cover a synthetic four-wall scene, corner/outside positions, signed cache bounds near8/9 and64/64, and positions4095/8191. Movement includes zero, unit steps, cardinal40/100 and diagonal100, with cache state preserved between successive queries. Each directional fixture list has one record; broader multi-record/cache tie scenarios remain separate.

The connected cache cases execute **316 actual bound rebuilds and620 existing-cache returns**. Every returned rectangle and collision result matches the comparison model. Original `0x48bec0` is reached2548 times across direct and connected cases; each outer entry returns with verified stack cleanup. Maximum observed complete invocation is570 instructions. These inputs cover signed bounds/destinations within the documented game-coordinate range, not arbitrary signed overflow or malformed capacities.

## Reproduction

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_terrain_sweep_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --units-dat /private/tmp/koprulu-campaign-data/arr/units.dat \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --static-out docs/research/evidence/native-terrain-sweep-static.json \
  --execute-original-code --out docs/research/evidence/native-terrain-sweep-probes.json
```

Default mode disassembles only the exact-hash EXE. Runtime: Python3.9.6, Unicorn2.1.4, pefile2024.8.26, Capstone5.0.6, x86_64 Rosetta. Stock units.dat and pinned PyMS identity are checked before execution. The bound is100,000 instructions per entry. Probe SHA-256 is `e533ed0ce3a1a08311b6077d60239803116d23bb054fcf46f765f463219a7a50`; the report records the fitting-tool dependency, runtime, exact cases and static scope.

Authenticated full-contour movement compositions, cache invalidation after terrain/unit changes, other-unit avoidance, local path generation and the complete movement state machine remain in the [completion ledger](completion-ledger.md). This batch closes selected actual sweep/cache consumers; it does not turn the independent [all-map footprint proof](native-contour-fit.md) into physical movement or campaign parity.
