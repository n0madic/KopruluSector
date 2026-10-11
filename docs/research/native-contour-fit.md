# Native footprint fitting on campaign contours (1.17)

The complete original footprint consumers pass **8937 cases**, including104 queries on each of the68 authenticated maps. Their terrain arrays and sorted contour bytes match previously recorded original producers before the query runs. This extends [contour construction](native-contours.md) from geometry output to actual consumers; it does not execute local routes or unit movement.

Target: StarCraft.exe1.17.0.1, SHA-256 `94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0`, ImageBase `0x400000`. [Static instructions](evidence/native-contour-fit-static.json), [8937 passing cases and9213 complete returns](evidence/native-contour-fit-probes.json), [probe](../../tools/research/probe_native_contour_fit_117.py). No fragments, helper stubs or imports execute in this batch. Private caller data supplies contour pointers, terrain arrays, stock extents and map dimensions. All executing original instructions belong to reviewed ranges.

## Complete entries and ordering

| Entry | Inputs | Contract |
|---|---|---|
| `0x42ed50` | ECX=unit ID, EDX=X, stack=[Y] | Stock unit bounds, point walkability, extent initialization, then contour fit. |
| `0x42ec70` | ECX=X, EDX=Y, stack=[width,height] | Rectangle bounds, point walkability, derived extent initialization, then contour fit. |
| `0x42ea10` | ECX=X, EDX=Y | Center-point walkability from tile flags and optional VF4 minitile. |
| `0x4891c0` | ECX=contour context, stack=[type/radius] | Inner/outer signed extents from stock units.dat or encoded radius. |
| `0x489160` | ECX=context, stack=[left,up,right,down] | Explicit signed inner/outer extents. |
| `0x48b780` | ECX=context, stack=[X,Y] | Search sorted contours for expanded footprint overlap. Returns1 if clear. |

For a stock unit with extents `(left,up,right,down)`, map bounds require `X>=left`, `X+right<map_pixel_width`, `Y>=up` and `Y+down<map_pixel_height`. Signed16-bit comparisons/conversions are used; map dimensions are unsigned words. A boundary rejection precedes any terrain access.

The rectangle wrapper subtracts half its integer width/height to obtain its top-left bound, and rejects when the far bound is **greater than or equal to** the map size. It derives contour extents `left=(width-1)/2`, `up=(height-1)/2`, `right=width-width/2-1`, `down=height-height/2-1`, with truncating positive division. Odd/asymmetric rectangles therefore need separate treatment; substituting the stock-unit wrapper would change their border checks.

## Point walkability

The point helper indexes the tile at truncated signed division by32. Tile flag`0x400000` forces success. Otherwise, flag`0x20000000` selects a per-minitile check: mask the megatile ID with`0x7fff`, select its4×4 VF4 element from truncated division by8, and test VF4 bit1. Without the partial flag, tile flag`0x10000` decides. Visibility/exploration bits in the low word do not affect this predicate.

The unit/rectangle wrapper still checks its contour footprint after a successful point gate. A walkable center alone therefore does not establish placement clearance. These calls use the ordinary megatile array; the separate alternate array used by [height queries](native-terrain-import.md) is not read by this helper.

## Signed extents and contour overlap

Stock initialization writes inner extents `[up,-right,-down,left]` at context+0x20 and outer extents `[up+1,-right-1,-down-1,left+1]` at+0x30. All228 stock rows are checked. When the low16-bit input is at least`0x8000`, it instead uses radius `input&0x7fff`, writing `[r,-r,-r,r]` and its one-pixel-expanded outer form. Six complete radius initializer calls cover0/1/7/8/31/127; that encoding is an initializer input, not permission to index the ordinary unit wrapper with an invalid DAT ID.

The four directional overlap rules, for normal builder records and supported coordinate range, are:

```text
top:    axis < Y <= axis+up,    start-right <= X <= end+left
right:  axis-right <= X < axis, start-down  <= Y <= end+up
bottom: axis-down <= Y < axis,  start-right <= X <= end+left
left:   axis < X <= axis+left,  start-down  <= Y <= end+up
```

Any matching contour rejects. The native binary searches use doubled coordinate thresholds `2*point-1` for top/left and `2*point+1` for right/bottom, followed by signed WORD comparisons and directional scans. Preserve the strict axis side and inclusive expanded endpoints. These rules are corroborated by the pinned [OpenBW can_fit_at](https://github.com/OpenBW/openbw/blob/4b046d5f65302b10cb0a745f0fecd37ec85b20a8/bwgame.h#L8508-L8541); original calls establish their agreement for the declared input set.

The record's direction byte selects its axis extent. This point-fit helper does not interpret the info byte's corner modes; those matter in the separate [movement sweep](native-terrain-sweep.md). Counts and coordinates are stored as words, with signed loads in the native searches. The comparison model covers actual campaign coordinates and these stock/synthetic bounds; it does not normalize arbitrary malformed arrays or prove doubled-coordinate wrap outside that domain.

## Input authentication and case coverage

| Family | Cases |
|---|---:|
| All228 stock rows × seven synthetic wall/border queries, plus complete initializer checks | 1824 |
| Encoded-radius initializer | 6 |
| Five rectangle sizes × seven synthetic wall/border points | 35 |
| Eight stock types × thirteen selected contour/boundary points ×68 maps | 7072 |
| **Total** | **8937** |

The eight map profiles are Marine0, SCV7, Tank30, Zergling37, Drone41, Zealot65, Command Center106 and Gateway160. Queries around each orientation use the selected contour's extent-shifted boundary with deltas-1/0/+1, plus a map-center point. A unit-type footprint query does not imply that every supplied building can naturally walk.

CHK/CV5/VF4 bytes are hash-checked against [terrain assets](terrain-assets.md). The entire reconstructed tile-flag and megatile-ID arrays match [recorded native import](native-terrain-import.md). The whole reconstructed walk grid matches recorded original output. Geometry is reconstructed, then **sorted by original qsort**; all sorted contour bytes match the prior native contour output hash. These checks concern ordered bytes, not just equal contour counts. Their producers are not rerun in the same instance.

There are276 complete sorting setup calls, bringing total original entry returns to9213. Of the queries,7209 reach the actual contour-fit helper. The boolean footprint results contain1599 successes and7104 rejections. The separately expressed oracle checks map bounds, point flags/VF4 and geometric interval overlap; it does not reproduce the native binary-search implementation. No original-derived arrays are bundled in the project.

## Reproduction and limits

```sh
arch -x86_64 /private/tmp/koprulu-native-x86-venv/bin/python3 \
  tools/research/probe_native_contour_fit_117.py \
  --exe /private/tmp/koprulu-retail.jJ8D4z/extracted/app/StarCraft.exe \
  --terrain-report docs/research/evidence/terrain-assets.json \
  --terrain-import-report docs/research/evidence/native-terrain-import-probes.json \
  --contours-report docs/research/evidence/native-contours-probes.json \
  --terrain-data /private/tmp/koprulu-terrain-data \
  --campaign-data /private/tmp/koprulu-campaign-data \
  --pyms /private/tmp/koprulu-pyms-campaign \
  --static-out docs/research/evidence/native-contour-fit-static.json \
  --execute-original-code --out docs/research/evidence/native-contour-fit-probes.json
```

Default mode reads/disassembles the exact-hash executable. The all-map report uses no `--map-limit`; shorter development runs are not merged into it. Runtime: Python3.9.6, Unicorn2.1.4, pefile2024.8.26, Capstone5.0.6, x86_64 Rosetta; pinned PyMS commit is recorded. Stock units.dat SHA-256 is `da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba`. Probe SHA-256 is `e83c929b1b871ea294d99cbacab517aed419d9534d9747b4ef2dedc9ef8679b1`. The invocation bound is10,000,000 instructions; maximum observed sorting invocation uses4,309,327.

Static witnesses regenerate byte for byte. An altered executable is rejected before execution. Native loader/contour allocation, dynamic doodad/unit changes, collision with other units, local route generation and full movement remain separate obligations in the [completion ledger](completion-ledger.md). These are complete selected footprint-function returns, not world initialization or played-campaign proof.
