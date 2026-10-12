"""Native region graph (`0x490d80` without contours, then `0x48f0e0`).

Contracts: docs/research/native-region-generation.md and native-campaign-regions.md; the
control flow follows the 1.17 disassembly. Sequence:

- `0x490e9f` terrain classes from the imported tile flags;
- `0x490f50` reserved bottom region 0;
- `0x48f060` builder: initial rectangles `0x48f8e0` (`0x490b80` rectangle search,
  `0x490180` fill, `0x48fff0` expansion), refresh `0x48f870` (`0x490330` counts/bounds,
  `0x4902e0`/`0x490500`/`0x490a60` neighbours, `0x4910c0` stable partition, fixed8 centres,
  `0x4901f0`/`0x490e20` groups), merge `0x48fc80`, compaction `0x48feb0`, refresh,
  split cells `0x48f1b0`;
- `0x48f0e0` cut ranks.

The raster is kept with the map width as row stride (the original uses 256). Regions are
indexed by their original ID. The output equals the native snapshots of all 68 campaign
CHKs and the 19 synthetic builder fixtures (converter/tests/test_derive.py).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from .terrain import FLAG_PARTIAL_WALK

ALGORITHM = b"RGN1"
VERSION = 1

CLASS_UNWALKABLE = 0x1FFD
CLASS_MIDDLE = 0x1FF9
CLASS_HIGH = 0x1FFA
CLASS_LOW = 0x1FFB
CLASS_EMPTY = 0x1FFF  # empty record; also the outside-map neighbour marker
REGION_LIMIT = 5000  # raster words >= this are unassigned class markers
SPLIT_BASE = 0x2000
GROUP_RESERVED = 0x4000
INITIAL_PREVIOUS_AREA = 7 * 8
LOOKAHEAD_CANDIDATES = 25
MERGE_THRESHOLD = 6
MERGE_THRESHOLD_STEP = 2
MERGE_REPEAT_LIVE = 2500
MAX_CUT_RANK = 5
BLOCKED = 0x80


class RegionError(ValueError):
    """The map needs more than the native 5000 regions (the original shows an error)."""


@dataclass
class Graph:
    """Final region object state, field names after the native record (64 bytes each)."""

    width: int
    height: int
    classes: list[int]  # +0x00
    groups: list[int]  # +0x02
    tiles: list[int]  # +0x04
    ground: list[int]  # +0x06, ground-neighbour prefix length
    neighbors: list[list[int]]  # +0x0c / +0x07
    zero_neighbors: list[int]  # +0x21
    centers: list[tuple[int, int]]  # +0x10, fixed8 pixels
    bounds: list[tuple[int, int, int, int]]  # +0x18, pixels: left, top, right, bottom
    cut_ranks: list[int]  # +0x20
    raster: list[int]  # width*height region or 0x2000+split IDs
    splits: list[tuple[int, int, int]]  # [mask, walkable side, blocked side], allocation order

    @property
    def count(self) -> int:
        return len(self.classes)


def tile_class(flags: int) -> int:
    """`0x490e9f`: class of one imported tile flag DWORD."""
    if flags & 0x410000 == 0:
        return CLASS_UNWALKABLE
    v = flags & 0x6010000
    if v == 0:
        return CLASS_UNWALKABLE
    if v == 0x2010000:
        return CLASS_MIDDLE
    if v == 0x4010000:
        return CLASS_HIGH
    return CLASS_LOW


def build(width: int, height: int, tile_flags: list[int], megatile_ids: list[int], vf4: bytes,
          walk: bytes) -> Graph:
    """Region graph of a map from imported tile flags/megatile IDs, VF4 and the walk raster
    (`walk.walk_grid`, stride `width*4`)."""
    b = _Builder(width, height, tile_flags, walk)
    b.bottom_region()
    b.initial_rectangles()
    b.refresh(1)
    b.merge()
    b.compact()
    b.refresh(1)
    splits = b.split_cells(tile_flags, megatile_ids, vf4)
    b.cut_ranks()
    n = b.count
    return Graph(width, height, b.cls[:n], b.grp[:n], b.tiles[:n], b.ground[:n], b.nb[:n], b.zero[:n],
                 [(b.cx[i], b.cy[i]) for i in range(n)],
                 [(b.left[i], b.top[i], b.right[i], b.bottom[i]) for i in range(n)],
                 b.rank[:n], b.raster, splits)


class _Builder:
    def __init__(self, width: int, height: int, tile_flags: list[int], walk: bytes):
        self.w, self.h = width, height
        self.walk = walk
        self.ww = width * 4
        self.raster = [tile_class(f) for f in tile_flags]
        self.count = 0
        size = REGION_LIMIT
        self.cls = [0] * size
        self.grp = [0] * size
        self.tiles = [0] * size
        self.ground = [0] * size
        self.nb: list[list[int]] = [[] for _ in range(size)]
        self.zero = [0] * size
        self.cx = [0] * size
        self.cy = [0] * size
        self.left = [0] * size
        self.top = [0] * size
        self.right = [0] * size
        self.bottom = [0] * size
        self.rank = [0] * size

    # 0x490f50
    def bottom_region(self) -> None:
        w, h = self.w, self.h
        i = self.count
        self.count += 1
        self.cls[i] = CLASS_UNWALKABLE
        self.cx[i] = (w * 32 // 2 + 16) * 256  # 16 pixels right of the bounds midpoint
        self.cy[i] = h * 0x2000 - 0x1000
        self.left[i], self.top[i], self.right[i], self.bottom[i] = 0, h * 32 - 32, w * 32, h * 32
        self.raster[(h - 1) * w:h * w] = [i] * w
        self.tiles[i] = w
        self.grp[i] = GROUP_RESERVED

    # 0x490b80
    def find_rectangle(self, x: int, y: int, cls: int) -> tuple[int, int, int, int]:
        r, w = self.raster, self.w
        max_x = min(x + 8, w)
        max_y = min(y + 7, self.h)
        ex, ey = x + 1, y + 1
        x_good = y_good = corner_good = True
        while ex < max_x and ey < max_y:
            if x_good:  # column ex, rows y..ey-1
                x_good = r[y * w + ex:ey * w + ex:w].count(cls) == ey - y
            elif not y_good:
                break
            if y_good:  # row ey, columns x..ex-1
                y_good = r[ey * w + x:ey * w + ex].count(cls) == ex - x
            if r[ey * w + ex] != cls:
                corner_good = False
            if corner_good:
                if y_good:
                    ey += 1
                if x_good:
                    ex += 1
            elif y_good:
                ey += 1
            elif x_good:
                ex += 1
        cw, ch = ex - x, ey - y
        if ch < cw:
            cw = min(cw, ch * 3)
        else:
            ch = min(ch, cw * 3)
        return x, y, x + cw, y + ch

    # 0x48fff0
    def expand(self, index: int, cls: int, left: int, top: int, right: int, bottom: int) -> None:
        r, w, h = self.raster, self.w, self.h
        for _ in range(2):
            for y in range(top, bottom):
                row = y * w
                for x in range(left, right):
                    p = row + x
                    if r[p] == cls and ((y > 0 and r[p - w] == index) or (x > 0 and r[p - 1] == index)
                                        or (x + 1 < w and r[p + 1] == index)
                                        or (y + 1 < h and r[p + w] == index)):
                        r[p] = index

    # 0x48f8e0, for the whole map rectangle
    def initial_rectangles(self) -> None:
        r, w, h = self.raster, self.w, self.h
        x0, y0, x1, y1 = 0, 0, w, h
        initial = self.count
        cur_x, cur_y = x0, y0
        previous = INITIAL_PREVIOUS_AREA
        expanded_all = False
        while True:
            best, best_area, best_cls, candidates = (0, 0, 0, 0), 0, 0, 0
            x, y = cur_x, cur_y
            if x >= x1:
                x, y = x0, y + 1
                if y >= y1:
                    y = y0
            sx, sy = x, y
            while True:
                v = r[y * w + x]
                if v >= REGION_LIMIT:
                    rect = self.find_rectangle(x, y, v)
                    candidates += 1
                    area = (rect[3] - rect[1]) * (rect[2] - rect[0])
                    if best_area < area:
                        best, best_area, best_cls = rect, area, v
                    if best_area >= previous or candidates > LOOKAHEAD_CANDIDATES:
                        cur_x, cur_y = best[2], best[1]
                        break
                    x = rect[2] - 1
                x += 1
                if x >= x1:
                    x, y = x0, y + 1
                    if y >= y1:
                        y = y0
                if x == sx and y == sy:
                    break  # full wrap: the next search restarts from the same position
            if self.count >= REGION_LIMIT:
                raise RegionError("more than 5000 regions")
            left, top, right, bottom = best
            area = (bottom - top) * (right - left)
            previous = area
            if area == 0:
                return
            index = self.count
            for yy in range(top, bottom):
                r[yy * w + left:yy * w + right] = [index] * (right - left)
            self.cx[index] = (right - left) // 2 + left
            self.cy[index] = (bottom - top) // 2 + top
            if left > 0:
                left -= 1
            if right < w:
                right += 1
            if top > 0:
                top -= 1
            if bottom < h:
                bottom += 1
            self.expand(index, best_cls, left, top, right, bottom)
            self.left[index], self.top[index], self.right[index], self.bottom[index] = left, top, right, bottom
            self.cls[index] = best_cls
            self.count += 1
            if area <= 6 and not expanded_all:
                expanded_all = True
                i = initial
                while i < self.count:
                    if self.left[i] > 0:
                        self.left[i] -= 1
                    if self.right[i] < w:
                        self.right[i] += 1
                    if self.top[i] > 0:
                        self.top[i] -= 1
                    if self.bottom[i] < h:
                        self.bottom[i] += 1
                    self.expand(i, self.cls[i], self.left[i], self.top[i], self.right[i], self.bottom[i])
                    i += 1

    # 0x490330
    def counts_bounds(self) -> None:
        n, w, h, r = self.count, self.w, self.h, self.raster
        tiles = [0] * n
        left, top, right, bottom = [0x7FFF] * n, [0x7FFF] * n, [-1] * n, [-1] * n
        for y in range(h):
            row = y * w
            py = y * 32
            for x in range(w):
                v = r[row + x]
                if v < REGION_LIMIT:
                    tiles[v] += 1
                    px = x * 32
                    if py < top[v]:
                        top[v] = py
                    if px < left[v]:
                        left[v] = px
                    if bottom[v] < py + 32:
                        bottom[v] = py + 32
                    if right[v] < px + 32:
                        right[v] = px + 32
        self.tiles[:n] = [t & 0xFFFF for t in tiles]
        self.left[:n], self.top[:n], self.right[:n], self.bottom[:n] = left, top, right, bottom
        for i in range(n):
            if tiles[i] == 0:
                self.cls[i] = CLASS_EMPTY

    def eight(self, x: int, y: int) -> tuple[int, ...]:
        """`0x490a60`: top, left, right, bottom, top-left, top-right, bottom-left, bottom-right."""
        r, w, h = self.raster, self.w, self.h
        p = y * w + x
        t = y > 0
        lf = x > 0
        rt = x + 1 < w
        bt = y + 1 < h
        return (r[p - w] if t else CLASS_EMPTY, r[p - 1] if lf else CLASS_EMPTY,
                r[p + 1] if rt else CLASS_EMPTY, r[p + w] if bt else CLASS_EMPTY,
                r[p - w - 1] if t and lf else CLASS_EMPTY, r[p - w + 1] if t and rt else CLASS_EMPTY,
                r[p + w - 1] if bt and lf else CLASS_EMPTY, r[p + w + 1] if bt and rt else CLASS_EMPTY)

    def _clear2x2(self, p: int) -> bool:
        wk, ww = self.walk, self.ww
        return not ((wk[p] | wk[p + 1] | wk[p + ww] | wk[p + ww + 1]) & BLOCKED)

    def _diagonal(self, k: int, x: int, y: int) -> bool:
        """`0x490500` diagonal walk test between two ordinary regions."""
        ww = self.ww
        p = y * 4 * ww + x * 4
        c = self._clear2x2
        if k == 4:
            return c(p) and c(p - 2 * ww - 2) and (c(p - 2) or c(p - 2 * ww))
        if k == 5:
            return c(p + 2) and c(p - 2 * ww + 4) and (c(p + 4) or c(p - 2 * ww + 2))
        if k == 6:
            return c(p + 2 * ww) and c(p + 4 * ww - 2) and (c(p + 2 * ww - 2) or c(p + 4 * ww))
        return c(p + 2 * ww + 2) and c(p + 4 * ww + 4) and (c(p + 2 * ww + 4) or c(p + 4 * ww + 2))

    # 0x4902e0 / 0x490500 / 0x4910c0
    def neighbor_arrays(self) -> None:
        """Each region scans its own tiles row-major (inside its bounds), so one row-major
        pass over the raster visits every region's tiles in the native order."""
        r, w, h, cls = self.raster, self.w, self.h, self.cls
        n = self.count
        found: list[list[int]] = [[] for _ in range(n)]
        seen: list[set[int]] = [set() for _ in range(n)]
        e = CLASS_EMPTY
        for y in range(h):
            row = y * w
            up, down = y > 0, y + 1 < h
            for x in range(w):
                p = row + x
                i = r[p]
                lf, rt = x > 0, x + 1 < w
                eight = (r[p - w] if up else e, r[p - 1] if lf else e, r[p + 1] if rt else e,
                         r[p + w] if down else e, r[p - w - 1] if up and lf else e,
                         r[p - w + 1] if up and rt else e, r[p + w - 1] if down and lf else e,
                         r[p + w + 1] if down and rt else e)
                if eight.count(i) == 8:
                    continue
                ss, ff = seen[i], found[i]
                accept_all = cls[i] == CLASS_UNWALKABLE
                for k in range(8):
                    v = eight[k]
                    if v == e or v == i or v in ss:
                        continue
                    if k < 4 or accept_all or cls[v] == CLASS_UNWALKABLE or self._diagonal(k, x, y):
                        ss.add(v)
                        ff.append(v)
        for i in range(n):
            if self.tiles[i] == 0:
                continue
            nb = found[i]
            ground = [v for v in nb if v != 0 and cls[v] != CLASS_UNWALKABLE]
            blocked = [v for v in nb if v != 0 and cls[v] == CLASS_UNWALKABLE]
            zeros = len(nb) - len(ground) - len(blocked)
            self.nb[i] = ground + blocked + [0] * zeros
            self.ground[i] = len(ground)
            self.zero[i] = zeros

    # 0x4901f0 / 0x490e20
    def groups(self) -> None:
        grp, cls, nb, ground = self.grp, self.cls, self.nb, self.ground
        n = self.count
        for i in range(n):
            if grp[i] < GROUP_RESERVED:
                grp[i] = 0
        group = 1
        for i in range(n):
            if grp[i] != 0 or self.tiles[i] == 0:
                continue
            grp[i] = group
            stack = [i]
            while stack:
                c = stack.pop()
                adjacent = nb[c][ground[c]:] if cls[c] == CLASS_UNWALKABLE else nb[c][:ground[c]]
                for v in adjacent:
                    if grp[v] == 0:
                        grp[v] = group
                        stack.append(v)
            group += 1

    # 0x48f870
    def refresh(self, first: int) -> None:
        self.counts_bounds()
        self.neighbor_arrays()
        for i in range(first, self.count):
            if self.cx[i] < 0x100:
                self.cx[i] = self.cx[i] * 0x2000 + 0x1000
            if self.cy[i] < 0x100:
                self.cy[i] = self.cy[i] * 0x2000 + 0x1000
        self.groups()

    # 0x48fc80
    def merge(self) -> None:
        r, w = self.raster, self.w
        tiles, grp, cls = self.tiles, self.grp, self.cls
        threshold = MERGE_THRESHOLD
        while True:
            for s in range(self.count - 1, -1, -1):
                if tiles[s] == 0 or grp[s] >= GROUP_RESERVED or tiles[s] >= threshold or not self.nb[s]:
                    continue
                target = -1
                for v in self.nb[s]:
                    if tiles[v] != 0 and grp[v] < GROUP_RESERVED and cls[s] == cls[v] and (
                            target < 0 or tiles[v] < tiles[target]):
                        target = v
                if target < 0:
                    continue
                x_lo, x_hi = _div32(self.left[s]), _div32(self.right[s])
                for y in range(_div32(self.top[s]), _div32(self.bottom[s])):
                    row = y * w
                    for x in range(x_lo, x_hi):
                        if r[row + x] == s:
                            r[row + x] = target
                tiles[target] = (tiles[target] + tiles[s]) & 0xFFFF
                tiles[s] = 0
                cls[s] = CLASS_EMPTY
                self.top[target] = min(self.top[target], self.top[s])
                self.bottom[target] = max(self.bottom[target], self.bottom[s])
                self.left[target] = min(self.left[target], self.left[s])
                self.right[target] = max(self.right[target], self.right[s])
            if self.count < 1 or sum(1 for i in range(self.count) if tiles[i]) < MERGE_REPEAT_LIVE:
                return
            threshold += MERGE_THRESHOLD_STEP

    # 0x48feb0
    def compact(self) -> None:
        n = self.count
        mapping, live = [], 0
        for i in range(n):
            mapping.append(live)
            live += self.tiles[i] != 0
        fields = (self.cls, self.grp, self.tiles, self.ground, self.nb, self.zero, self.cx, self.cy,
                  self.left, self.top, self.right, self.bottom, self.rank)
        j = 0
        for i in range(n):
            if self.tiles[i] == 0:
                continue
            self.nb[i] = [mapping[v] for v in self.nb[i]]
            for f in fields:
                f[j] = f[i]
            j += 1
        self.count = live
        self.raster = [mapping[v] for v in self.raster]

    # 0x48f1b0
    def split_cells(self, tile_flags: list[int], megatile_ids: list[int], vf4: bytes) -> list[tuple[int, int, int]]:
        r, w, ww, wk, cls = self.raster, self.w, self.ww, self.walk, self.cls
        records = list(struct.iter_unpack("<16H", vf4))
        splits: list[tuple[int, int, int]] = []

        def blk(p: int) -> int:
            return wk[p] >> 7

        for y in range(self.h):
            for x in range(w):
                cell = y * w + x
                if not tile_flags[cell] & FLAG_PARTIAL_WALK:
                    continue
                words = records[megatile_ids[cell] & 0x7FFF]
                mask = 0
                for k in range(16):
                    mask |= (~words[k] & 1) << k
                cur = r[cell]
                n8 = self.eight(x, y)
                p = y * 4 * ww + x * 4
                if cls[cur] != CLASS_UNWALKABLE:
                    c0, c3, c12, c15 = blk(p), blk(p + 3), blk(p + 3 * ww), blk(p + 3 * ww + 3)
                    counts = (c0 + blk(p + 1) + blk(p + 2) + c3,
                              c0 + blk(p + ww) + blk(p + 2 * ww) + c12,
                              c3 + blk(p + ww + 3) + blk(p + 2 * ww + 3) + c15,
                              c12 + blk(p + 3 * ww + 1) + blk(p + 3 * ww + 2) + c15,
                              c0, c3, c12, c15)
                    best, best_i = 0, 0
                    for i in range(7, -1, -1):
                        n = counts[i]
                        if n <= best:
                            continue
                        v = n8[i]
                        if v == cur:
                            continue
                        if v < SPLIT_BASE:
                            ok = v < REGION_LIMIT and cls[v] == CLASS_UNWALKABLE
                        else:
                            ok = _beyond(wk, ww, p, i, blocked=True) != 0
                        if ok:
                            best, best_i = n, i
                    if best == 0:
                        rec = (0, cur, cur)
                    else:
                        other = n8[best_i]
                        if other >= SPLIT_BASE:
                            other = splits[other - SPLIT_BASE][2]
                        rec = (0, cur, cur) if other == cur else (mask, cur, other)
                else:
                    counts = (4 - blk(p) - blk(p + 1) - blk(p + 2) - blk(p + 3),
                              4 - blk(p) - blk(p + ww) - blk(p + 2 * ww) - blk(p + 3 * ww),
                              4 - blk(p + 3) - blk(p + ww + 3) - blk(p + 2 * ww + 3) - blk(p + 3 * ww + 3),
                              4 - blk(p + 3 * ww) - blk(p + 3 * ww + 1) - blk(p + 3 * ww + 2) - blk(p + 3 * ww + 3))
                    best, best_i = 0, 0
                    for i in range(3, -1, -1):
                        n = counts[i]
                        if n <= best:
                            continue
                        v = n8[i]
                        if v == cur:
                            continue
                        if v < SPLIT_BASE:
                            ok = v < REGION_LIMIT and cls[v] != CLASS_UNWALKABLE
                        else:
                            ok = _beyond(wk, ww, p, i, blocked=False) != 0
                        if ok:
                            best, best_i = n, i
                    if best == 0:
                        rec = (0xFFFF, cur, cur)
                    else:
                        other = n8[best_i]
                        if other >= SPLIT_BASE:
                            other = splits[other - SPLIT_BASE][1]
                        if other == cur:
                            rec = (0xFFFF, cur, cur)
                        elif cls[other] != CLASS_UNWALKABLE:
                            rec = (mask, other, cur)
                        else:
                            rec = (0xFFFF, other, cur)
                r[cell] = SPLIT_BASE + len(splits)
                splits.append(rec)
        return splits

    # 0x48f0e0
    def cut_ranks(self) -> None:
        for i in range(self.count):
            seen: list[int] = []
            rank = 0
            for v in self.nb[i][self.ground[i]:]:
                if self.tiles[v] <= 3:
                    continue
                g = self.grp[v]
                if g in seen:
                    continue
                rank += 1
                if rank >= MAX_CUT_RANK:
                    break
                seen.append(g)
            else:
                if rank < 2:
                    rank = 0
            self.rank[i] = rank


def _div32(value: int) -> int:
    """Signed division by 32 truncating toward zero, as the original `cdq/and/add/sar`."""
    return -((-value) >> 5) if value < 0 else value >> 5


def _beyond(wk: bytes, ww: int, p: int, i: int, *, blocked: bool) -> int:
    """`0x48f1b0`: blocked (or walkable) mini-cells just outside side/corner `i` of a tile."""
    if i == 0:
        cells = (p - ww, p - ww + 1, p - ww + 2, p - ww + 3)
    elif i == 1:
        cells = (p - 1, p + ww - 1, p + 2 * ww - 1, p + 3 * ww - 1)
    elif i == 2:
        cells = (p + 4, p + ww + 4, p + 2 * ww + 4, p + 3 * ww + 4)
    elif i == 3:
        cells = (p + 4 * ww, p + 4 * ww + 1, p + 4 * ww + 2, p + 4 * ww + 3)
    elif i == 4:
        cells = (p - ww - 1,)
    elif i == 5:
        cells = (p - ww + 4,)
    elif i == 6:
        cells = (p + 4 * ww - 1,)
    else:
        cells = (p + 4 * ww + 4,)
    n = sum(wk[c] >> 7 for c in cells)
    return n if blocked else len(cells) - n


# RGNS payload (docs/formats/kpm.md): header, region records, neighbour pool, raster, splits.
RGNS_HEADER = struct.Struct("<4HI")  # width, height, region count, split count, neighbour words
RGNS_REGION = struct.Struct("<3H4BHI2i4h")  # 32 bytes
SPLIT_RECORD = struct.Struct("<3H")


def encode(g: Graph) -> bytes:
    pool: list[int] = []
    regions = bytearray()
    for i in range(g.count):
        nb = g.neighbors[i]
        regions += RGNS_REGION.pack(g.classes[i], g.groups[i], g.tiles[i], g.ground[i], len(nb), g.zero_neighbors[i],
                                    g.cut_ranks[i], 0, len(pool), *g.centers[i], *g.bounds[i])
        pool += nb
    return b"".join((RGNS_HEADER.pack(g.width, g.height, g.count, len(g.splits), len(pool)), bytes(regions),
                     struct.pack(f"<{len(pool)}H", *pool), struct.pack(f"<{len(g.raster)}H", *g.raster),
                     b"".join(SPLIT_RECORD.pack(*s) for s in g.splits)))


def decode(payload: bytes) -> Graph:
    """Inverse of `encode`."""
    width, height, count, split_count, pool_size = RGNS_HEADER.unpack_from(payload, 0)
    pos = RGNS_HEADER.size
    rows = [RGNS_REGION.unpack_from(payload, pos + i * RGNS_REGION.size) for i in range(count)]
    pos += count * RGNS_REGION.size
    pool = struct.unpack_from(f"<{pool_size}H", payload, pos)
    pos += 2 * pool_size
    raster = list(struct.unpack_from(f"<{width * height}H", payload, pos))
    pos += 2 * width * height
    splits = [SPLIT_RECORD.unpack_from(payload, pos + i * SPLIT_RECORD.size) for i in range(split_count)]
    return Graph(width, height, [r[0] for r in rows], [r[1] for r in rows], [r[2] for r in rows],
                 [r[3] for r in rows], [list(pool[r[8]:r[8] + r[4]]) for r in rows], [r[5] for r in rows],
                 [(r[9], r[10]) for r in rows], [r[11:15] for r in rows], [r[6] for r in rows], raster, splits)
